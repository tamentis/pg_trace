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

#include "fdcache.h"
#include "utils.h"
#include "xmalloc.h"


/*
 * _size defines the size of the memory pool, _length defines the number of
 * items in this pool.
 */
fd_desc *fd_cache = NULL;
int fd_cache_length = 0;
int fd_cache_size = 0;


/*
 * Wipe an fd_desc item clear, avoiding public urination.
 */
void
fd_desc_invalidate(fd_desc *desc)
{
	desc->fd = FD_CACHE_INVALID;
	xfree(desc->name);
}


/*
 * Remove all the cached items if it already exists, else just create it.
 */
void
fd_cache_clear() {
	int i;

	debug("fd_cache_clear()\n");

	if (fd_cache != NULL) {
		for (i = 0; i < fd_cache_length; i++)
			fd_desc_invalidate(&fd_cache[i]);

		fd_cache_length = 0;
	}
}


/*
 * Return the next free item in the cache. If necessary, grow the pool.
 */
fd_desc *
fd_cache_next()
{
	fd_desc *item;
	fd_cache_length++;

	/* We've outgrown our cache size, 3nl@rg3! */
	if (fd_cache_length > fd_cache_size) {
		fd_cache_size += FD_CACHE_GROWTH;
		debug("fd_cache_next(): growing to %d\n", fd_cache_size);
		fd_cache = xrealloc(fd_cache, fd_cache_size, sizeof(fd_desc));
	}

	item = &fd_cache[fd_cache_length - 1];
	item->fd = FD_CACHE_INVALID;
	item->name = NULL;

	return item;
}


/*
 * Retrieve an fd_desc based on its fd.
 */
fd_desc *
fd_cache_get(int fd)
{
	int i;

	for (i = 0; i < fd_cache_length; i++) {
		if (fd_cache[i].fd == FD_CACHE_INVALID)
			continue;
		if (fd_cache[i].fd == fd)
			return &fd_cache[i];
	}

	return NULL;
}


/*
 * Remove an element from the cache. Technically we just invalidate it. This is
 * not used in the initial load, but only when we receive a close() from the
 * trace program.
 */
void
fd_cache_delete(int fd)
{
	int i;

	for (i = 0; i < fd_cache_length; i++) {
		if (fd_cache[i].fd == fd) {
			fd_desc_invalidate(&fd_cache[i]);
			return;
		}
	}
}


/*
 * Add a file descriptor to the cache. This is used for incremental updates,
 * not for the initial bulk load. It will find empty spots before.
 */
void
fd_cache_add(int fd, char *path)
{
	int i;
	fd_desc *current = NULL;

	/* First try to replace an empty slot. */
	for (i = 0; i < fd_cache_length; i++) {
		if (fd_cache[i].fd == FD_CACHE_INVALID) {
			current = &fd_cache[i];
			break;
		}
	}

	if (current == NULL)
		current = fd_cache_next();

	current->fd = fd;
	current->type = FILE_TYPE_REG;
	current->name = xstrdup(path);
}
