/*-------------------------------------------------------------------------
 *
 * relmapper.c
 *	  Catalog-to-filenode mapping
 *
 * pg_trace notes: This file was taken directly from the PostgreSQL 9.1.2
 * distribution.
 *
 * For most tables, the physical file underlying the table is specified by
 * pg_class.relfilenode.  However, that obviously won't work for pg_class
 * itself, nor for the other "nailed" catalogs for which we have to be able
 * to set up working Relation entries without access to pg_class.  It also
 * does not work for shared catalogs, since there is no practical way to
 * update other databases' pg_class entries when relocating a shared catalog.
 * Therefore, for these special catalogs (henceforth referred to as "mapped
 * catalogs") we rely on a separately maintained file that shows the mapping
 * from catalog OIDs to filenode numbers.  Each database has a map file for
 * its local mapped catalogs, and there is a separate map file for shared
 * catalogs.  Mapped catalogs have zero in their pg_class.relfilenode entries.
 *
 * Relocation of a normal table is committed (ie, the new physical file becomes
 * authoritative) when the pg_class row update commits.  For mapped catalogs,
 * the act of updating the map file is effectively commit of the relocation.
 * We postpone the file update till just before commit of the transaction
 * doing the rewrite, but there is necessarily a window between.  Therefore
 * mapped catalogs can only be relocated by operations such as VACUUM FULL
 * and CLUSTER, which make no transactionally-significant changes: it must be
 * safe for the new file to replace the old, even if the transaction itself
 * aborts.	An important factor here is that the indexes and toast table of
 * a mapped catalog must also be mapped, so that the rewrites/relocations of
 * all these files commit in a single map file update rather than being tied
 * to transaction commit.
 *
 * Portions Copyright (c) 1996-2011, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/utils/cache/relmapper.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <err.h>

#include "access/xact.h"
#include "catalog/catalog.h"
#include "catalog/pg_tablespace.h"
#include "catalog/storage.h"
#include "miscadmin.h"
#include "storage/fd.h"
#include "storage/lwlock.h"
#include "utils/inval.h"
#include "utils/pg_crc.h"
#include "utils/relmapper.h"
#include "pg_crc32_table.h"


extern char *current_cluster_path;
extern Oid current_database_oid;


/*
 * The map file is critical data: we have no automatic method for recovering
 * from loss or corruption of it.  We use a CRC so that we can detect
 * corruption.	To minimize the risk of failed updates, the map file should
 * be kept to no more than one standard-size disk sector (ie 512 bytes),
 * and we use overwrite-in-place rather than playing renaming games.
 * The struct layout below is designed to occupy exactly 512 bytes, which
 * might make filesystem updates a bit more efficient.
 *
 * Entries in the mappings[] array are in no particular order.	We could
 * speed searching by insisting on OID order, but it really shouldn't be
 * worth the trouble given the intended size of the mapping sets.
 */
#define RELMAPPER_FILENAME		"pg_filenode.map"

#define RELMAPPER_FILEMAGIC		0x592717		/* version ID value */

#define MAX_MAPPINGS			62		/* 62 * 8 + 16 = 512 */

typedef struct RelMapping
{
	Oid			mapoid;			/* OID of a catalog */
	Oid			mapfilenode;	/* its filenode number */
} RelMapping;

typedef struct RelMapFile
{
	int32		magic;			/* always RELMAPPER_FILEMAGIC */
	int32		num_mappings;	/* number of valid RelMapping entries */
	RelMapping	mappings[MAX_MAPPINGS];
	int32		crc;			/* CRC of all above */
	int32		pad;			/* to make the struct size be 512 exactly */
} RelMapFile;

/*
 * The currently known contents of the shared map file and our database's
 * local map file are stored here.	These can be reloaded from disk
 * immediately whenever we receive an update sinval message.
 */
static RelMapFile shared_map;
static RelMapFile local_map;

/*
 * We use the same RelMapFile data structure to track uncommitted local
 * changes in the mappings (but note the magic and crc fields are not made
 * valid in these variables).  Currently, map updates are not allowed within
 * subtransactions, so one set of transaction-level changes is sufficient.
 *
 * The active_xxx variables contain updates that are valid in our transaction
 * and should be honored by RelationMapOidToFilenode.  The pending_xxx
 * variables contain updates we have been told about that aren't active yet;
 * they will become active at the next CommandCounterIncrement.  This setup
 * lets map updates act similarly to updates of pg_class rows, ie, they
 * become visible only at the next CommandCounterIncrement boundary.
 */
static RelMapFile active_shared_updates;
static RelMapFile active_local_updates;


void load_relmap_file(bool shared);


/*
 * RelationMapOidToFilenode
 *
 * The raison d' etre ... given a relation OID, look up its filenode.
 *
 * Although shared and local relation OIDs should never overlap, the caller
 * always knows which we need --- so pass that information to avoid useless
 * searching.
 *
 * Returns InvalidOid if the OID is not known (which should never happen,
 * but the caller is in a better position to report a meaningful error).
 */
Oid
RelationMapOidToFilenode(Oid relationId, bool shared)
{
	const RelMapFile *map;
	int32		i;

	/* If there are active updates, believe those over the main maps */
	if (shared)
	{
		map = &active_shared_updates;
		for (i = 0; i < map->num_mappings; i++)
		{
			if (relationId == map->mappings[i].mapoid)
				return map->mappings[i].mapfilenode;
		}
		map = &shared_map;
		for (i = 0; i < map->num_mappings; i++)
		{
			if (relationId == map->mappings[i].mapoid)
				return map->mappings[i].mapfilenode;
		}
	}
	else
	{
		map = &active_local_updates;
		for (i = 0; i < map->num_mappings; i++)
		{
			if (relationId == map->mappings[i].mapoid)
				return map->mappings[i].mapfilenode;
		}
		map = &local_map;
		for (i = 0; i < map->num_mappings; i++)
		{
			if (relationId == map->mappings[i].mapoid)
				return map->mappings[i].mapfilenode;
		}
	}

	return InvalidOid;
}


/*
 * load_relmap_file -- load data from the shared or local map file
 *
 * Because the map file is essential for access to core system catalogs,
 * failure to read it is a fatal error.
 */
void
load_relmap_file(bool shared)
{
	RelMapFile *map;
	char		mapfilename[MAXPGPATH];
	pg_crc32	crc;
	int			fd;

	if (shared)
	{
		snprintf(mapfilename, sizeof(mapfilename), "%s/global/%s",
				 current_cluster_path, RELMAPPER_FILENAME);
		map = &shared_map;
	}
	else
	{
		snprintf(mapfilename, sizeof(mapfilename), "%s/base/%u/%s",
				 current_cluster_path, current_database_oid,
				 RELMAPPER_FILENAME);
		map = &local_map;
	}

	/* Read data ... */
	fd = open(mapfilename, O_RDONLY | PG_BINARY, S_IRUSR | S_IWUSR);
	if (fd < 0)
		err(1, "could not open relation mapping file \"%s\"",
						mapfilename);

	/*
	 * Note: we could take RelationMappingLock in shared mode here, but it
	 * seems unnecessary since our read() should be atomic against any
	 * concurrent updater's write().  If the file is updated shortly after we
	 * look, the sinval signaling mechanism will make us re-read it before we
	 * are able to access any relation that's affected by the change.
	 */
	if (read(fd, map, sizeof(RelMapFile)) != sizeof(RelMapFile))
		err(1, "could not read relation mapping file \"%s\"",
						mapfilename);

	close(fd);

	/* check for correct magic number, etc */
	if (map->magic != RELMAPPER_FILEMAGIC ||
		map->num_mappings < 0 ||
		map->num_mappings > MAX_MAPPINGS)
		err(1, "relation mapping file \"%s\" contains invalid data",
						mapfilename);

	/* verify the CRC */
	INIT_CRC32(crc);
	COMP_CRC32(crc, (char *) map, offsetof(RelMapFile, crc));
	FIN_CRC32(crc);

	if (!EQ_CRC32(crc, map->crc))
		err(1, "relation mapping file \"%s\" contains incorrect checksum",
				  mapfilename);
}


/*
 * FilenodeToRelationMapOid
 *
 * This is the inverse of RelationMapOidToFilenode. This is not imported from
 * Postgresql.
 *
 * Returns InvalidOid if the OID is not known (which should never happen,
 * but the caller is in a better position to report a meaningful error).
 */
Oid
FilenodeToRelationMapOid(Oid filenode, bool shared)
{
	const RelMapFile *map;
	int32		i;

	/* If there are active updates, believe those over the main maps */
	if (shared)
	{
		map = &active_shared_updates;
		for (i = 0; i < map->num_mappings; i++)
		{
			if (filenode == map->mappings[i].mapfilenode)
				return map->mappings[i].mapoid;
		}
		map = &shared_map;
		for (i = 0; i < map->num_mappings; i++)
		{
			if (filenode == map->mappings[i].mapfilenode)
				return map->mappings[i].mapoid;
		}
	}
	else
	{
		map = &active_local_updates;
		for (i = 0; i < map->num_mappings; i++)
		{
			if (filenode == map->mappings[i].mapfilenode)
				return map->mappings[i].mapoid;
		}
		map = &local_map;
		for (i = 0; i < map->num_mappings; i++)
		{
			if (filenode == map->mappings[i].mapfilenode)
				return map->mappings[i].mapoid;
		}
	}

	return InvalidOid;
}


