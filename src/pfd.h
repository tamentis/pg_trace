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


#define MAX_HUMAN_FD_LENGTH	256


/*
 * CHR to IPV6 types are directly mirrored from the lsof listing. New entries
 * coming from open are typically REG and only REG are used for relname
 * resolution.
 *
 * UNKNOWN is flagged on file descriptor that exist but without a type.
 *
 * INVALID is used to flag the item as invalid, this is the default type of a
 * pfd, it also returns to this type when free'd.
 */
enum fd_type {
	FD_TYPE_CHR,
	FD_TYPE_REG,
	FD_TYPE_DIR,
	FD_TYPE_FIFO,
	FD_TYPE_IPV4,
	FD_TYPE_IPV6,
	FD_TYPE_UNKNOWN,
	FD_TYPE_INVALID
};


/*
 * Assuming the fd is REG, the file_type defines what sort of file the fd
 * points to within the realm of a postgres cluster.
 */
enum file_type {
	FILE_TYPE_TABLE,
	FILE_TYPE_VM,
	FILE_TYPE_FSM,
	FILE_TYPE_XLOG,
	FILE_TYPE_UNKNOWN
};


typedef struct _pfd_t {
	Oid		 database_oid;
	Oid		 oid;
	Oid		 filenode;
	int		 fd;
	bool		 shared;
	enum fd_type	 fd_type;
	enum file_type	 file_type;
	char		*filepath;
	char		*relname;
} pfd_t;


pfd_t		*pfd_new(int);
void		 pfd_clean(pfd_t *);
char		*pfd_get_repr(pfd_t *);
void		 pfd_update_from_filepath(pfd_t *);
void		 pfd_update_from_pg(pfd_t *);
