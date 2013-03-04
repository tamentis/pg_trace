/*
 * Copyright (c) 2013 Bertrand Janin <b@janin.com>
 * Copyright (c) 1997 Todd C. Miller <Todd.Miller@courtesan.com>
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
 * The original file was:
 * $OpenBSD: which.c,v 1.17 2011/03/11 04:30:21 guenther Exp $
 */

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/sysctl.h>

#include <err.h>
#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "xmalloc.h"


char *findprog(char *, char *);


char *
which(char *progname)
{
	char *path;

	if ((path = getenv("PATH")) == NULL)
		err(1, "can't get $PATH from environment");

	return findprog(progname, path);
}



char *
findprog(char *prog, char *path)
{
	char *p, filename[MAXPATHLEN];
	int proglen, plen;
	struct stat sbuf;
	char *pathcpy;

	/* Special case if prog contains '/' */
	if (strchr(prog, '/')) {
		if ((stat(prog, &sbuf) == 0) && S_ISREG(sbuf.st_mode) &&
		    access(prog, X_OK) == 0) {
			return xstrdup(prog);
		} else {
			return NULL;
		}
	}

	path = xstrdup(path);
	pathcpy = path;

	proglen = strlen(prog);
	while ((p = strsep(&pathcpy, ":")) != NULL) {
		if (*p == '\0')
			p = ".";

		plen = strlen(p);
		while (p[plen-1] == '/')
			p[--plen] = '\0';	/* strip trailing '/' */

		if (plen + 1 + proglen >= sizeof(filename)) {
			warnx("%s/%s: %s", p, prog, strerror(ENAMETOOLONG));
			free(path);
			return NULL;
		}

		snprintf(filename, sizeof(filename), "%s/%s", p, prog);
		if ((stat(filename, &sbuf) == 0) && S_ISREG(sbuf.st_mode) &&
		    access(filename, X_OK) == 0) {
			free(path);
			return xstrdup(filename);
		}
	}
	(void)free(path);

	return NULL;
}

