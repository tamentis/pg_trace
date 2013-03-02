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


/*
 * Maximum number of function arguments to parse out. Anything beyond that will
 * cause an error.
 */
#define MAX_FUNCTION_ARGUMENTS	32

/*
 * This is the maximum supported line size when reading lsof and strace.
 */
#define MAX_LINE_LENGTH		1024


#define debug(...) if (debug_flag) fprintf(stderr, __VA_ARGS__)


enum fd_file_type {
	FILE_TYPE_CHR,
	FILE_TYPE_REG,
	FILE_TYPE_FIFO,
	FILE_TYPE_IPV4,
	FILE_TYPE_IPV6,
	FILE_TYPE_UNKNOWN
};

typedef struct _fd_desc {
	int fd;
	enum fd_file_type type;
	char *name;
} fd_desc;


/* strace.c */
int		 strace_open(pid_t);
void		 strace_read_lines(int, void (*func)(char *, int, char **, char*));

/* lsof.c */
void		 lsof_refresh_cache(pid_t);
fd_desc		*lsof_get_fd_desc(int);

/* fdcache.c */
void		 fd_cache_clear();
fd_desc		*fd_cache_next();
fd_desc		*fd_cache_get(int);
void		 fd_cache_delete(int);
void		 fd_cache_add(int, char *);

/* utils.c */
int		 xatoi(char *);
int		 xatoi_or_zero(char *);
char		*xitoa(int);

/* xmalloc.c */
char 		*xstrdup(const char *);
void 		*xcalloc(size_t, size_t);
void 		*xmalloc(size_t);
void 		*xrealloc(void *, size_t, size_t);
void		 xfree(void *);
int		 xasprintf(char **, const char *, ...);
int		 xvasprintf(char **, const char *, va_list);
int		 xsnprintf(char *, size_t, const char *, ...);
int		 xvsnprintf(char *, size_t, const char *, va_list);

/* strlcpy.c */
size_t		 strlcpy(char *, const char *, size_t);

/* pg.c */
char		*pg_get_relname_from_filepath(char *);
