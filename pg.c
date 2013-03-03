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

#include <postgres.h>
#include <c.h>
#include <storage/bufpage.h>
#include <storage/itemid.h>
#include <catalog/pg_class.h>
#include <access/htup.h>

#include "pg_trace.h"
#include "relmapper.h"


extern int debug_flag;

char *current_cluster_path = NULL;
Oid current_database_oid = InvalidOid;


/*
 * Returns the filesystem path of the pg_class table.
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
 * Assumes the file pointer's cursor is one PageHeaderData deep, we'll read
 * only what's left of the page and return the pointer to the rea
 */
Page *
pg_read_page(FILE *fp)
{
	int l;
	PageHeaderData *ph;
	Page *p;

	/* Read the page header */
	ph = xmalloc(SizeOfPageHeaderData);
	if (fread(ph, SizeOfPageHeaderData, 1, fp) != 1)
		errx(1, "unable to read page header");

	/* Fill the rest of the data */
	p = xrealloc(ph, 1, PageGetPageSize(ph));
	l = fread((void *)p + SizeOfPageHeaderData,
			PageGetPageSize(p) - SizeOfPageHeaderData, 1, fp);
	if (l != 1)
		errx(1, "pg_read_page:fread()");

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
pg_get_filenode_from_filepath(char *org_filepath, bool *shared)
{
	int i;
	char *c, *oid, *filepath;
	Oid db_oid = InvalidOid;

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
	/* Skip the part chunk at the end of the OID, TODO: use that for the
	 * progress management... later (each file is 1GB). */
	oid = c;
	c = strchr(oid, '.');
	if (c != NULL)
		*c = '\0';

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

	return (Oid)i;
}


char *
pg_get_relname_from_page(Page *p, Oid filenode, Oid oid)
{
	int i, count;
	PageHeaderData *ph;
	HeapTupleHeaderData *hthd;
	FormData_pg_class *ci;
	ItemIdData *pd_linp;
	Oid id;

	debug("pg_get_relname_from_page(%u, %u)\n", filenode, oid);

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

		if (oid != InvalidOid)
			if (id == oid)
				return xstrdup(NameStr(ci->relname));

		if (filenode != InvalidOid)
			if (ci->relfilenode == filenode)
				return xstrdup(NameStr(ci->relname));
	}

	return NULL;
}


/*
 * Find a relname based either on filenode or oid. This is not super pretty,
 * but until refactored, whichever one is not InvalidOid is the one used for
 * filtering.
 */
char *
pg_get_relname(Oid filenode, Oid oid)
{
	FILE *fp;
	char *pg_class_filepath, *relname;
	Page *p;

	pg_class_filepath = pg_get_pg_class_filepath();
	if (pg_class_filepath == NULL)
		return NULL;

	fp = fopen(pg_class_filepath, "rb");
	while (!feof(fp)) {
		p = pg_read_page(fp);
		relname = pg_get_relname_from_page(p, filenode, oid);
		xfree(p);
		if (relname != NULL)
			break;
	}
	fclose(fp);

	return relname;
}


/*
 * Take any absolute file path and return a relname (possibly a table name).
 * Return NULL if we can't find anything relevant.
 */
char *
pg_get_relname_from_filepath(char *filepath)
{
	Oid filenode, mapped_oid;
	char *relname = NULL;
	bool shared = false;

	filenode = pg_get_filenode_from_filepath(filepath, &shared);
	debug("pg_get_relname_from_filepath(%s) -> filenode_oid=%u\n",
			filepath, filenode);

	if (filenode == InvalidOid)
		return NULL;

	/*
	 * Attempt to get the relname from the relmapper, in case this filepath
	 * belongs to a "special" object that does not have a filenode in the
	 * pg_class table.
	 */
	load_relmap_file(shared);
	mapped_oid = FilenodeToRelationMapOid(filenode, shared);
	if (mapped_oid != InvalidOid) {
		relname = pg_get_relname(InvalidOid, mapped_oid);
		if (relname != NULL)
			return relname;
	}

	relname = pg_get_relname(filenode, InvalidOid);

	return relname;
}
