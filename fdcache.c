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

#include "pg_trace.h"


/* How much to realloc when the cache is too tight. */
#define FD_CACHE_GROWTH		64


extern int debug;


/*
 * _size defines the size of the memory pool, _length defines the number of
 * items in this pool.
 */
fd_desc *fd_cache = NULL;
int fd_cache_length = 0;
int fd_cache_size = 0;


/*
 * Remove all the cached items if it already exists, else just create it.
 */
void
fd_cache_clear() {
	int i;

	debug("fd_cache_clear()\n");

	if (fd_cache != NULL) {
		for (i = 0; i < fd_cache_length; i++) {
			xfree(fd_cache[i].name);
		}
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
		if (fd_cache[i].fd == fd)
			return &fd_cache[i];
	}

	return NULL;
}
