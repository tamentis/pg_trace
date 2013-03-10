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
 *
 *
 * Representation of a PostgreSQL file descriptor with all its tools.
 */

#include <stdlib.h>
#include <err.h>

#include <postgres.h>

#include "xmalloc.h"
#include "utils.h"
#include "rn_cache.h"
#include "relmapper.h"
#include "pg.h"
#include "pfd.h"


extern char *current_cluster_path;
extern Oid current_database_oid;


/*
 * Did we do the initial rn_cache population? This can only happen once we know
 * the cluster path and database oid.
 */
int rn_cache_initial_load = 0;


/*
 * Free all the properties of this object.
 *
 * This will not free the item itself, this is the responsibility of the
 * caller.
 */
void
pfd_clean(pfd_t *pfd)
{
	pfd->fd_type = FD_TYPE_INVALID;

	if (pfd->relname != NULL) {
		xfree(pfd->relname);
		pfd->relname = NULL;
	}

	if (pfd->filepath != NULL) {
		xfree(pfd->filepath);
		pfd->filepath = NULL;
	}
}


/*
 * Extrapolate information based on the filepath.
 *
 * Based on the path, we can figure out:
 *
 *  - current_cluster_path
 *  - current_database_oid
 *  - shared (/global/ path)
 *  - database oid
 *  - file oid
 *  - filetype (vm, fsm, table)
 *
 * Anything before /base/ or /global/ is the cluster path. /global/ is used to
 * detect shared databases. The database OID is the first integer after /base/,
 * the filenode (file OID) is right after.
 *
 * Some files will have a suffix after their filenode (_vm, _fsm) indicating
 * the type of file.
 *
 * Some large table will be split in multiple large files, the .# prefix is
 * used where # is an integer to identify the parts.
 *
 * This function assumes the path to your database does not contain /base/ or
 * /global/. If it does, you'll need to fix it ;)
 */
void
pfd_update_from_filepath(pfd_t *pfd)
{
	int i;
	char *c, *oid, *filepath;
	Oid db_oid = InvalidOid;

	/* Do not butcher the original filepath. */
	filepath = xstrdup(pfd->filepath);

	/* Is this a shared (global) file? */
	c = strstr(filepath, "/global/");
	if (c != NULL) {
		c = strchr(c + 1, '/') + 1;
		pfd->shared = true;
		goto parse_filenode;
	}

	/* It this a database? */
	c = strstr(filepath, "/base/");
	if (c != NULL) {
		c = strchr(c + 1, '/') + 1;
		pfd->shared = false;
		goto parse_database_oid;
	}

	/* If we are getting here, we're not a DB file. */
	pfd->filenode = InvalidOid;
	return;

parse_database_oid:
	/* Keep reference to the database Oid. We should always be looking at
	 * the same database, but just in case. */
	oid = c;
	c = strchr(c, '/');
	if (c == NULL) {
		pfd->filenode = InvalidOid;
		return;
	}
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
		pfd->file_type = FILE_TYPE_VM;
	} else if ((c = strstr(oid, "_fsm")) != NULL) {
		*c = '\0';
		pfd->file_type = FILE_TYPE_FSM;
	} else {
		pfd->file_type = FILE_TYPE_TABLE;
	}

	/* Whatever's in oid at this point, has got to be an int, if the
	 * conversion fail, this is not the droid we're looking for. */
	i = xatoi_or_zero(oid);
	if (i == 0) {
		pfd->file_type = FILE_TYPE_UNKNOWN;
		pfd->filenode = InvalidOid;
		return;
	}

	/* Now that we know the path is valid, save the current cluster path
	 * and current database oid. */
	if (current_database_oid == InvalidOid && db_oid != InvalidOid) {
		current_database_oid = db_oid;
	} else if (!(pfd->shared) && current_database_oid != db_oid) {
		errx(1, "error: one backend shouldn't switch database");
	}

	if (current_cluster_path == NULL && db_oid != InvalidOid) {
		*(oid - 1) = '\0';
		current_cluster_path = xstrdup(filepath);
	}

	xfree(filepath);

	pfd->filenode = (Oid)i;
}




/*
 * Given a pfd's relname.
 *
 * If we have none in file, attempt to discover it.
 */
void
pfd_update_from_pg(pfd_t *pfd)
{
	Oid mapped_oid;

	if (pfd->relname != NULL)
		return;

	if (pfd->filenode == InvalidOid)
		errx(1, "got in pfd_update_from_pg without filenode");

	/*
	 * If we the rn cache is empty at this point, fill it, we should have
	 * all the path required to load pg_class.
	 */
	if (rn_cache_initial_load == 0) {
		pg_load_rn_cache_from_pg_class();
		rn_cache_initial_load = 1;
		rn_cache_print();
	}

	/*
	 * Attempt to get the relname from the relmapper, in case this filepath
	 * belongs to a "special" object that does not have a filenode in the
	 * pg_class table.
	 */
	load_relmap_file(pfd->shared);
	mapped_oid = FilenodeToRelationMapOid(pfd->filenode, pfd->shared);
	if (mapped_oid != InvalidOid) {
		pfd->relname = rn_cache_get_from_oid(mapped_oid);
		if (pfd->relname != NULL)
			return;
	}

	pfd->relname = rn_cache_get_from_filenode(pfd->filenode);
}


char *
pfd_get_repr(pfd_t *pfd)
{
	char buffer[MAX_HUMAN_FD_LENGTH];
	char suffix[7] = "";
	char *repr = NULL;

	if (pfd->relname) {
		switch (pfd->file_type) {
			case FILE_TYPE_VM:
				strlcpy(suffix, "(vm)", sizeof(suffix));
				break;
			case FILE_TYPE_FSM:
				strlcpy(suffix, "(fsm)", sizeof(suffix));
				break;
			case FILE_TYPE_UNKNOWN:
				strlcpy(suffix, "(?!?)", sizeof(suffix));
			default:
				break;
		}

		snprintf(buffer, sizeof(buffer), "relname=%s%s", pfd->relname,
				suffix);
		repr = xstrdup(buffer);
	}

	/* Can't get a relname, display the filepath. */
	if (repr == NULL && pfd->filepath != NULL) {
		snprintf(buffer, sizeof(buffer), "filepath=%s",
				pfd->filepath);
		repr = xstrdup(buffer);
	}

	/* Can't get a filepath, display the fd. */
	if (repr == NULL) {
		snprintf(buffer, sizeof(buffer), "fd=%u", pfd->fd);
		repr = xstrdup(buffer);
	}

	return repr;
}
