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


// FIXME: nope hardc0de
#define DatabasePath "/mnt/var/lib/postgresql/9.1/main/base/16386"


extern int debug_flag;


/*
 * Returns the filesystem path of the pg_class table.
 */
char *
pg_get_pg_class_filepath()
{
	int filenode;
	char *filepath;
	char buffer[MAXPGPATH];

	/*
	 * This gives us access to all the mapped file nodes, they are not
	 * available in pg_class. It will also allow us to find the file node
	 * for pg_class since it's a mapped file node itself.
	 */
	load_relmap_file(0);

	filenode = RelationMapOidToFilenode(RelationRelationId, 0);
	snprintf(buffer, MAXPGPATH, "%s/%d", DatabasePath, filenode);

	return xstrdup(buffer);
}


/*
 * Assuming the file pointer is positioned at the beginning of a page, this
 * returns a pointer to a populated PageHeaderData structure.
 */
PageHeaderData *
pg_read_page_header(FILE *fp)
{
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

	debug("SizeOfPageHeaderData=%lu\n", SizeOfPageHeaderData);
	debug("ph->pd_lower=0x%04X\n", ph->pd_lower);
	debug("ph->pd_upper=0x%04X\n", ph->pd_upper);
	debug("ph->pd_page_size=%lu\n", PageGetPageSize(ph));
	debug("ph->pd_page_version=%d\n", PageGetPageLayoutVersion(ph));
	debug("ph->pd_special=0x%04X\n", ph->pd_special);
	debug("ph->pd_prune_xid=0x%08X\n", ph->pd_prune_xid);

	p = xrealloc(ph, 1, PageGetPageSize(ph));
	l = fread((void *)p + SizeOfPageHeaderData,
			PageGetPageSize(p) - SizeOfPageHeaderData, 1, fp);
	if (l != 1)
		errx(1, "pg_read_page:fread()");

	return p;
}

void
pg_do_stuff()
{
	FILE *fp;
	int i, count;
	char *pg_class_filepath;
	char *d;
	Page *p;
	PageHeaderData *ph;
	HeapTupleHeaderData *hthd;
	ItemIdData *pd_linp;
	FormData_pg_class *ci;

	pg_class_filepath = pg_get_pg_class_filepath();

	fp = fopen(pg_class_filepath, "rb");

	p = pg_read_page(fp);
	ph = (PageHeaderData *)p;
	count = (ph->pd_lower - SizeOfPageHeaderData) / sizeof(ItemIdData);
	debug("item count: %d\n", count);

	for (i = 0; i < count; i++) {
		pd_linp = PageGetItemId(p, i + 1);

		/* Strip out dead, redirects, etc. */
		if (pd_linp->lp_flags != LP_NORMAL)
			continue;

		debug("pd_linp[%d]->lp_off=0x%04X\n", i, pd_linp->lp_off);
		debug("pd_linp[%d]->lp_flags=0x%02X\n", i, pd_linp->lp_flags);
		debug("pd_linp[%d]->lp_len=0x%04X\n", i, pd_linp->lp_len);

		hthd = (HeapTupleHeaderData *)PageGetItem(p, pd_linp);
		debug("hthd[%d]->t_hoff=%04X\n", i, hthd->t_hoff);

		ci = (FormData_pg_class *)((void *)hthd + hthd->t_hoff);
		debug("ci[%d]->relname->data=%s\n", i, NameStr(ci->relname));

		debug("\n");
	}


#if 0
	/* Load the array of ItemIdData */
	iids = xmalloc(p->pd_lower - SizeOfPageHeaderData);
	s = fread(iids, sizeof(ItemIdData), num_items, fp);
	if (s != num_items)
		errx(1, "unable to read item id data");

	/* Position the cursor at the beginning of the tuples and start to read
	 * the item id data from the back, since they are reversed. */
	if (fseek(fp, p->pd_upper, SEEK_SET) == -1)
		err(1, "fseek()");

	debug("sizeof pg_class stuff %d\n", sizeof(FormData_pg_class));

	return;

	for (i = num_items - 1; i >= 0; i--) {
		iid = &iids[i];
		if (iid->lp_len == 0)
			continue;
		ci = xmalloc(iid->lp_len);
		s = fread(tuple, iid->lp_len, 1, fp);
		if (s != 1)
			errx(1, "fread(item)");
	}
#endif

	fclose(fp);
}

