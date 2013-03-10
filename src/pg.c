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

#include "rn_cache.h"
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
