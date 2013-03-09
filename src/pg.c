/*
 * Copyright (c) 2013 Bertrand Janin <b@janin.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <err.h>

#include <c.h>
#include <postgres.h>
#include <access/htup.h>
#include <catalog/pg_class.h>
#include <storage/bufpage.h>
#include <storage/itemid.h>

#include "rncache.h"
#include "relmapper.h"
#include "utils.h"
#include "xmalloc.h"
#include "pg.h"


/*
 * Since this tool is mostly targetted at backends and backends are not meant
 * to access multiple clusters at the same time, we keep track of the firsts
 * cluster path we find and assume this is the one until the end.
 */
char *current_cluster_path = NULL;

/*
 * And we also make the assumption that a backend will not connect to multiple
 * databases. Experimentations show that \connect will spawn a new backend.
 * TODO: make sure this is not a false assumption.
 */
Oid current_database_oid = InvalidOid;

/*
 * Did we do the initial rn_cache population? This can only happen once we know
 * the cluster path and database oid.
 */
int rn_cache_initial_load = 0;


/*
 * Returns the filesystem path of the pg_class table.
 *
 * This function assumes both current_cluster_path and current_database_oid are
 * valid.
 */
char *
pg_get_pg_class_filepath()
{
	int filenode;
	char buffer[MAXPGPATH];

	/* We haven't found any cluster or database yet, shouldn't be here. */
	if (current_cluster_path == NULL || current_database_oid == InvalidOid)
		return NULL;

	/*
	 * This gives us access to all the mapped file nodes, they are not
	 * available in pg_class. It will also allow us to find the file node
	 * for pg_class since it's a mapped file node itself.
	 */
	load_relmap_file(0);

	filenode = RelationMapOidToFilenode(RelationRelationId, 0);
	snprintf(buffer, MAXPGPATH, "%s/base/%u/%d", current_cluster_path,
			current_database_oid, filenode);

	return xstrdup(buffer);
}


/*
 * Reads one page from the provided file pointer.
 *
 * Returns NULL if we can't read an entire header or an entire page.
 */
Page *
pg_read_page(FILE *fp)
{
	int l;
	PageHeaderData *ph;
	Page *p;

	/* Read the page header */
	ph = xmalloc(SizeOfPageHeaderData);
	if (fread(ph, SizeOfPageHeaderData, 1, fp) != 1) {
		xfree(ph);
		if (feof(fp))
			return NULL;
		errx(1, "unable to read page header");
	}

	/* Fill the rest of the data */
	p = xrealloc(ph, 1, PageGetPageSize(ph));
	l = fread((void *)p + SizeOfPageHeaderData,
			PageGetPageSize(p) - SizeOfPageHeaderData, 1, fp);
	if (l != 1) {
		xfree(p);
		if (feof(fp))
			return NULL;
		errx(1, "pg_read_page:fread()");
	}

	return p;
}


/*
 * Converts the absolute file path into a filenode OID. The best case scenario
 * is that the path ends with one of the following formats:
 *
 *     /base/[0-9]+/[0-9]+
 *     /base/[0-9]+/[0-9]+.[0-9]+
 *     /global/[0-9]+
 *     /global/[0-9]+.[0-9]+
 *
 * Return InvalidOid if no OID was found.
 *
 * This function assumes the path to your database does not contain /base/ or
 * /global/. If it does, you'll need to fix it ;)
 */
Oid
pg_get_filenode_from_filepath(char *org_filepath, bool *shared,
		enum db_file_type *type)
{
	int i;
	char *c, *oid, *filepath;
	Oid db_oid = InvalidOid;
	enum db_file_type prospect_type = DB_FILE_TYPE_UNKNOWN;

	/* Don't touch the original filepath, we still use it. */
	filepath = xstrdup(org_filepath);

	debug("pg_get_filenode_from_filepath(%s, %u)\n", filepath, *shared);

	/* Is this a shared (global) file? */
	c = strstr(filepath, "/global/");
	if (c != NULL) {
		c = strchr(c + 1, '/') + 1;
		*shared = true;
		goto parse_filenode;
	}

	/* It this a database? */
	c = strstr(filepath, "/base/");
	if (c != NULL) {
		c = strchr(c + 1, '/') + 1;
		*shared = false;
		goto parse_database_oid;
	}

	/* If we are getting here, we're not a DB file. */
	return InvalidOid;

parse_database_oid:
	/* Keep reference to the database Oid. We should always be looking at
	 * the same database, but just in case. */
	oid = c;
	c = strchr(c, '/');
	if (c == NULL)
		return InvalidOid;
	*c = '\0';
	db_oid = xatoi_or_zero(oid);
	c++;

	/* Reduce the string before /base/db oid, obtain the cluster path */
	*(oid - 6) = '\0';

parse_filenode:
	oid = c;

	/* Skip the part chunk at the end of the OID, TODO: use that for the
	 * progress management... later (each file is 1GB). */
	c = strchr(oid, '.');
	if (c != NULL)
		*c = '\0';

	/* If the file is a visibility map or a free space map, we still want
	 * to resolve the table. */
	if ((c = strstr(oid, "_vm")) != NULL) {
		*c = '\0';
		prospect_type = DB_FILE_TYPE_VM;
	} else if ((c = strstr(oid, "_fsm")) != NULL) {
		*c = '\0';
		prospect_type = DB_FILE_TYPE_FSM;
	} else {
		prospect_type = DB_FILE_TYPE_TABLE;
	}

	/* Whatever's in oid at this point, has got to be an int, if the
	 * conversion fail, this is not the droid we're looking for. */
	i = xatoi_or_zero(oid);
	if (i == 0)
		return InvalidOid;

	/* Now that we know the path is valid, save the current cluster path
	 * and current database oid. */
	if (current_database_oid == InvalidOid && db_oid != InvalidOid) {
		current_database_oid = db_oid;
	} else if (current_database_oid != db_oid) {
		errx(1, "error: one backend shouldn't switch database");
	}

	if (current_cluster_path == NULL && db_oid != InvalidOid) {
		*(oid - 1) = '\0';
		current_cluster_path = xstrdup(filepath);
	}

	xfree(filepath);

	*type = prospect_type;

	return (Oid)i;
}


void
pg_load_rn_cache_from_page(Page *p)
{
	int i, count;
	PageHeaderData *ph;
	HeapTupleHeaderData *hthd;
	FormData_pg_class *ci;
	ItemIdData *pd_linp;
	Oid id;

	debug("pg_load_rn_cache_from_page(%p)\n", p);

	ph = (PageHeaderData *)p;
	count = (ph->pd_lower - SizeOfPageHeaderData) / sizeof(ItemIdData);

	for (i = 0; i < count; i++) {
		pd_linp = PageGetItemId(p, i + 1);

		/* Strip out dead, redirects, etc. */
		if (pd_linp->lp_flags != LP_NORMAL)
			continue;

		hthd = (HeapTupleHeaderData *)PageGetItem(p, pd_linp);

		ci = (FormData_pg_class *)((void *)hthd + hthd->t_hoff);

		/* If this tuple has an OID, that's the OID of our table. */
		if (hthd->t_infomask & HEAP_HASOID)
			id = *((Oid *)((void *)ci - sizeof(Oid)));
		else
			id = 0;

		ci = (FormData_pg_class *)((void *)hthd + hthd->t_hoff);

		rn_cache_add(RN_ORIGIN_PGCLASS, id, ci->relfilenode,
				NameStr(ci->relname));
	}
}


/*
 * Find a relname based either on filenode or oid. This is not super pretty,
 * but until refactored, whichever one is not InvalidOid is the one used for
 * filtering.
 */
void
pg_load_rn_cache_from_pg_class()
{
	FILE *fp;
	char *pg_class_filepath;
	Page *p;

	pg_class_filepath = pg_get_pg_class_filepath();
	if (pg_class_filepath == NULL)
		return;

	debug("pg_load_rn_cache_from_pg_class() path:%s\n", pg_class_filepath);

	fp = fopen(pg_class_filepath, "rb");
	while ((p = pg_read_page(fp)) != NULL) {
		pg_load_rn_cache_from_page(p);
		xfree(p);
	}
	fclose(fp);
}


/*
 * Take any absolute file path and return a relname (possibly a table name).
 * Return NULL if we can't find anything relevant.
 */
char *
pg_get_relname_from_filepath(char *filepath, enum db_file_type *type)
{
	Oid filenode, mapped_oid;
	char *relname = NULL;
	bool shared = false;

	filenode = pg_get_filenode_from_filepath(filepath, &shared, type);
	debug("pg_get_relname_from_filepath(%s) -> filenode_oid=%u\n",
			filepath, filenode);

	if (filenode == InvalidOid)
		return NULL;

	/*
	 * If we the rn cache is empty at this point, fill it, we should have
	 * all the path required to load pg_class.
	 */
	if (rn_cache_initial_load == 0) {
		pg_load_rn_cache_from_pg_class();
		rn_cache_initial_load = 1;
	}

	/*
	 * Attempt to get the relname from the relmapper, in case this filepath
	 * belongs to a "special" object that does not have a filenode in the
	 * pg_class table.
	 */
	load_relmap_file(shared);
	mapped_oid = FilenodeToRelationMapOid(filenode, shared);
	if (mapped_oid != InvalidOid) {
		relname = rn_cache_get_from_oid(mapped_oid);
		if (relname != NULL)
			return relname;
	}

	relname = rn_cache_get_from_filenode(filenode);

	return relname;
}
