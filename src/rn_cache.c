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
 * The 'rn_cache' facility is meant to avoid linear lookups to the pg_class and
 * relmap on-disk data. It is essentially an in-memory table of the following
 * fields:
 *
 *  - origin		enum (relmap or pgclass)
 *  - oid		Oid
 *  - filenode		Oid
 *  - relname		char *
 *
 * Random ideas for improvements:
 *
 *  - constant time lookup (btree, rbtree, etc.)
 *  - when we see a 'write' call on either relmap or pg_class files, we should
 *    do a refresh of this cache.
 */

#include <stdio.h>
#include <unistd.h>

#include <postgres.h>

#include "rn_cache.h"
#include "utils.h"
#include "xmalloc.h"


/*
 * _size defines the size of the memory pool, _length defines the number of
 * items in this pool.
 */
rn_record *rn_pool = NULL;
int rn_count = 0;
int rn_pool_size = 0;


/*
 * Wipe an rn_record item clear, avoiding public urination.
 */
void
rn_record_invalidate(rn_record *rec)
{
	rec->oid = InvalidOid;
	rec->filenode = InvalidOid;
	xfree(rec->relname);
	rec->relname = NULL;
}


/*
 * Remove all the cached items if it already exists, else just create it.
 */
void
rn_cache_clear() {
	int i;

	debug("rn_cache_clear()\n");

	if (rn_pool == NULL)
		return;

	for (i = 0; i < rn_count; i++)
		rn_record_invalidate(&rn_pool[i]);

	rn_count = 0;
}


/*
 * Return the next free item in the cache. If necessary, grow the pool.
 */
rn_record *
rn_cache_next()
{
	rn_record *item;
	rn_count++;

	/* We've outgrown our cache size, 3nl@rg3! */
	if (rn_count > rn_pool_size) {
		rn_pool_size += RN_CACHE_GROWTH;
		debug("rn_cache_next(): growing to %d\n", rn_pool_size);
		rn_pool = xrealloc(rn_pool, rn_pool_size, sizeof(rn_record));
	}

	item = &rn_pool[rn_count - 1];
	item->relname = NULL;

	return item;
}


/*
 * Retrieve an rn_record based on its oid.
 */
char *
rn_cache_get_from_oid(Oid oid)
{
	int i;

	for (i = 0; i < rn_count; i++) {
		if (rn_pool[i].oid == InvalidOid)
			continue;
		if (rn_pool[i].oid == oid)
			return rn_pool[i].relname;
	}

	return NULL;
}


/*
 * Retrieve an rn_record based on its filenode.
 */
char *
rn_cache_get_from_filenode(Oid filenode)
{
	int i;

	for (i = 0; i < rn_count; i++) {
		if (rn_pool[i].filenode == InvalidOid)
			continue;
		if (rn_pool[i].filenode == filenode)
			return rn_pool[i].relname;
	}

	return NULL;
}


/*
 * Remove an element from the cache. Technically we just invalidate it. This is
 * not used in the initial load, but only when we receive a close() from the
 * trace program.
 */
void
rn_cache_delete(Oid oid)
{
	int i;

	for (i = 0; i < rn_count; i++) {
		if (rn_pool[i].oid == oid) {
			rn_record_invalidate(&rn_pool[i]);
			return;
		}
	}
}


/*
 * Add a file descriptor to the cache. This is used for incremental updates,
 * not for the initial bulk load. It will find empty spots before.
 */
void
rn_cache_add(enum rn_origin origin, Oid oid, Oid filenode, char *relname)
{
	int i;
	rn_record *current = NULL;

	/* First try to replace an empty slot. */
	for (i = 0; i < rn_count; i++) {
		if (rn_pool[i].oid == InvalidOid) {
			current = &rn_pool[i];
			break;
		}
	}

	if (current == NULL)
		current = rn_cache_next();

	current->origin = origin;
	current->oid = oid;
	current->filenode = filenode;
	current->relname = xstrdup(relname);
}


/*
 * Debugging function dumping the content of the rn_cache to stdout.
 */
void
rn_cache_print()
{
	int i;
	rn_record *rn = NULL;

	/* First try to replace an empty slot. */
	for (i = 0; i < rn_count; i++) {
		rn = &rn_pool[i];

		printf("%i\t%i\t%i\t%i\t%s\n", i, rn->origin, rn->oid,
				rn->filenode, rn->relname);
	}
}
