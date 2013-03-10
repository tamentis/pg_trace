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


/* How much to realloc when the cache is too tight. */
#define RN_CACHE_GROWTH		256


/* Defines if a record originates from relmapper or pg_class */
enum rn_origin {
	RN_ORIGIN_RELMAPPER,
	RN_ORIGIN_PGCLASS
};


typedef struct _rn_record {
	enum rn_origin origin;
	Oid oid;
	Oid filenode;
	bool shared;
	char *relname;
} rn_record;


void		 rn_record_invalidate(rn_record *);
void		 rn_cache_clear();
rn_record	*rn_cache_next();
char		*rn_cache_get_from_oid(Oid);
char		*rn_cache_get_from_filenode(Oid);
void		 rn_cache_delete(Oid);
void		 rn_cache_add(enum rn_origin, Oid, Oid, char *);
