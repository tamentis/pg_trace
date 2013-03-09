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
#include <unistd.h>

#include <postgres.h>

#include "pfd.h"
#include "pfd_cache.h"
#include "lsof.h"
#include "utils.h"
#include "xmalloc.h"


/* Realloc'd array of pfd_t's, NULL == uninitialized. */
pfd_t *pfd_pool = NULL;

/* Number of items in the pool. */
int pfd_count = 0;

/* Actual byte size of the pool, this can only grow. */
int pool_size = 0;



/*
 * Remove all the items in the cache.
 */
void
pfd_cache_clear() {
	int i;

	debug("pfd_cache_clear()\n");

	if (pfd_pool != NULL) {
		for (i = 0; i < pfd_count; i++)
			pfd_clean(&pfd_pool[i]);

		pfd_count = 0;
	}
}


/*
 * Return the next free slot in the cache. If necessary, grow the pool.
 */
pfd_t *
pfd_cache_next()
{
	pfd_t *item;
	pfd_count++;

	/* We've outgrown our cache size, 3nl@rg3! */
	if (pfd_count > pool_size) {
		pool_size += PFD_CACHE_GROWTH;
		debug("pfd_cache: growing pool to %d items\n", pool_size);
		pfd_pool = xrealloc(pfd_pool, pool_size, sizeof(pfd_t));
	}

	/* Pick up the next item from the grown pool, since it's uninitialized,
	 * clean it up first.  */
	item = &pfd_pool[pfd_count - 1];
	pfd_clean(item);

	return item;
}


/*
 * Retrieve an pfd_t based on its fd.
 */
pfd_t *
pfd_cache_get(int fd)
{
	int i;

	for (i = 0; i < pfd_count; i++) {
		if (pfd_pool[i].fd_type == FD_TYPE_INVALID)
			continue;
		if (pfd_pool[i].fd == fd)
			return &pfd_pool[i];
	}

	return NULL;
}


/*
 * Remove an element from the cache. Technically we just invalidate it. This is
 * not used in the initial load, but only when we receive a close() from the
 * trace program.
 */
void
pfd_cache_delete(int fd)
{
	int i;

	for (i = 0; i < pfd_count; i++) {
		if (pfd_pool[i].fd == fd) {
			pfd_clean(&pfd_pool[i]);
			return;
		}
	}
}


/*
 * Add a file descriptor to the cache. This is used for incremental updates,
 * not for the initial bulk load. It will find empty spots before.
 */
void
pfd_cache_add(int fd, char *path)
{
	int i;
	pfd_t *current = NULL;

	/* First try to replace an empty slot. */
	for (i = 0; i < pfd_count; i++) {
		if (pfd_pool[i].fd_type == FD_TYPE_INVALID) {
			current = &pfd_pool[i];
			break;
		}
	}

	if (current == NULL)
		current = pfd_cache_next();

	current->fd = fd;
	current->fd_type = FD_TYPE_REG;
	current->filename = xstrdup(path);
}


/*
 * Pre-load the pfd_cache using the output from lsof.
 */
void
pfd_cache_preload_from_lsof(pid_t pid)
{
	int fd;

	debug("pfd_cache: load from lsof (pid=%d)\n", pid);

	lsof_resolve_path();

	pfd_cache_clear();

	fd = lsof_open(pid);
	lsof_read_lines(fd);
	close(fd);
}

