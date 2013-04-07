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
 * This modules contains all the tools the manage the internal cache of file
 * descriptors.
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
int pfd_pool_size = 0;



/*
 * Remove all the items in the cache.
 */
void
pfd_cache_clear() {
	int i;

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
	pfd_t *pfd;
	pfd_count++;

	/* We've outgrown our cache size, 3nl@rg3! */
	if (pfd_count > pfd_pool_size) {
		pfd_pool_size += PFD_CACHE_GROWTH;
		debug("pfd_cache: growing pool to %d pfds\n", pfd_pool_size);
		pfd_pool = xrealloc(pfd_pool, pfd_pool_size, sizeof(pfd_t));
	}

	/* Pick up the next pfd from the grown pool, since it's uninitialized,
	 * clean it up first.  */
	pfd = &pfd_pool[pfd_count - 1];
	pfd->fd = InvalidOid;
	pfd->fd_type = FD_TYPE_INVALID;
	pfd->relname = NULL;
	pfd->filepath = NULL;

	return pfd;
}


/*
 * Retrieve an pfd_t based on its fd.
 *
 * If this entry does not exist, create a new one.
 */
pfd_t *
pfd_cache_get(int fd)
{
	int i;
	pfd_t *pfd;

	for (i = 0; i < pfd_count; i++) {
		pfd = &pfd_pool[i];

		if (pfd->fd_type == FD_TYPE_INVALID)
			continue;

		if (pfd->fd == fd)
			return pfd;
	}

	pfd = pfd_cache_add(fd, NULL);

	return pfd;
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
	pfd_t *pfd;

	for (i = 0; i < pfd_count; i++) {
		pfd = &pfd_pool[i];

		if (pfd->fd_type == FD_TYPE_INVALID)
			continue;

		if (pfd->fd == fd) {
			pfd_clean(pfd);
			return;
		}
	}
}


/*
 * Add a file descriptor to the cache. This is used for incremental updates,
 * not for the initial bulk load. It will find empty spots before.
 */
pfd_t *
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

	/* If a path was provided, attempt to populate the structure. */
	if (path != NULL) {
		current->filepath = xstrdup(path);
		pfd_update_from_filepath(current);

		if (current->filenode != InvalidOid)
			pfd_update_from_pg(current);
	}

	return current;
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


/*
 * Print the content of the pfd_cache to stdout.
 *
 * This is mostly used for debugging values are tab-delimited, headers are on
 * the first line.
 */
void
pfd_cache_print()
{
	int i;
	pfd_t *pfd;

	printf("index\tfd_type\tfd\tfilenode\tfilepath\trelname\n");

	for (i = 0; i < pfd_count; i++) {
		pfd = &pfd_pool[i];
		printf("%i\t%i\t%i\t%i\t%s\t%s\n", i, pfd->fd_type, pfd->fd,
				pfd->filenode, pfd->filepath, pfd->relname);
	}
}
