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

#include <postgres.h>

#include "xmalloc.h"
#include "pg.h"
#include "pfd.h"


/*
 * Allocate a new pfd.
 *
 * All values are initialized to Invalid/NULL.
 */
pfd_t *
pfd_new(int fd)
{
	pfd_t *new;

	new = xmalloc(sizeof(pfd_t));
	new->fd = fd;
	new->fd_type = FD_TYPE_INVALID;
	new->database_oid = InvalidOid;
	new->oid = InvalidOid;
	new->filenode = InvalidOid;
	new->relname = NULL;
	new->filename = NULL;

	return new;
}


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

	if (pfd->filename != NULL) {
		xfree(pfd->filename);
		pfd->filename = NULL;
	}
}
