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
 * This module contains all the functions relative to spawning, streaming and
 * crudely parsing lsof output. The output from lsof is used to initially
 * populate the pfd_cache.
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <err.h>

#include <postgres.h>

#include "pfd.h"
#include "pfd_cache.h"
#include "lsof.h"
#include "utils.h"
#include "xmalloc.h"
#include "pg.h"
#include "which.h"


pid_t latest_pid = 0;
char *lsof_path = NULL;


/*
 * Load the list of file descriptors for the current process.
 *
 * Returns an open file descriptor pointed at the output of lsof.
 */
int
lsof_open(pid_t pid)
{
	int pipefd[2], pipe_r, pipe_w;
	pid_t lsof_pid;
	char *cpid;

	cpid = xitoa((int)pid);

	if (pipe(pipefd) == -1)
		err(1, "lsof_open:pipe()");

	pipe_r = pipefd[0];
	pipe_w = pipefd[1];

	lsof_pid = fork();

	if (lsof_pid == -1) {
		err(1, "lsof_open:fork()");
	} else if (lsof_pid == 0) {
		if (dup2(pipe_w, STDOUT_FILENO) == -1)
			err(1, "lsof_open:dup2(pipe_w, stdout)");
		if (close(pipe_r) == -1)
			err(1, "lsof_open:close(pipe_r)");
		if (execl(lsof_path, "lsof",
					"-Faftn",	/* parser-friendly see
							   lsof(8) */
					"-p", cpid,	/* target pid */
					(char*)NULL) == -1)
			err(1, "lsof_open:execl()");
	}

	if (close(pipe_w) == -1)
		err(1, "lsof_open:close(pipe_w)");

	return pipe_r;
}


/*
 * Read lsof lines and feed the pfd_cache with the values.
 *
 * Every time we hit a field of type 'f', we move on to the next record.
 */
void
lsof_read_lines(int fd)
{
	FILE *fp;
	char line[MAX_LINE_LENGTH], type, *c;
	pfd_t *current = NULL;
	int fd_field;

	fp = fdopen(fd, "r");
	if (fp == NULL)
		err(1, "lsof_read_lines:fdopen()");

	while (fgets(line, sizeof(line), fp)) {
		/* Strip new-lines */
		c = strchr(line, '\n');
		if (c != NULL)
			*c = '\0';

		/* Separate the field type from the value. */
		type = line[0];
		c = line + 1;

		/* Records start with 'f' fields. */
		if (type == 'f') {
			fd_field = xatoi_or_zero(c);

			/* Not a numeric file descriptor, ignore it. */
			if (fd_field == 0) {
				continue;
			}

			/* 
			 * Before we move on to the next file descriptor, make
			 * sure the previous one is valid.
			 */
			if (current == NULL || (current->fd_type == FD_TYPE_REG
						&& current->filepath != NULL)) {
				current = pfd_cache_next();
			}

			pfd_clean(current);
			current->fd = fd_field;
		}

		/*
		 * This should only be true if we are going through the first
		 * few lines.
		 */
		if (current == NULL) {
			continue;
		}

		switch (type) {
		/* access mode */
		case 'a':
			break;
		/* pid, the first record */
		case 'p':
			break;
		/* fd number (handled above) */
		case 'f':
			break;
		/* file type */
		case 't':
			if (strcmp(c, "CHR") == 0)
				current->fd_type = FD_TYPE_CHR;
			else if (strcmp(c, "REG") == 0)
				current->fd_type = FD_TYPE_REG;
			else if (strcmp(c, "DIR") == 0)
				current->fd_type = FD_TYPE_DIR;
			else if (strcmp(c, "FIFO") == 0)
				current->fd_type = FD_TYPE_FIFO;
			else if (strcmp(c, "IPv4") == 0)
				current->fd_type = FD_TYPE_IPV4;
			else if (strcmp(c, "IPv6") == 0)
				current->fd_type = FD_TYPE_IPV6;
			else {
				current->fd_type = FD_TYPE_UNKNOWN;
			}
			break;
		/* file name */
		case 'n':
			current->filepath = xstrdup(c);
			pfd_update_from_filepath(current);
			break;
		default:
			errx(1, "lsof_read_lines() unknown type '%c'", type);
			break;
		}
	}
}


/*
 * Resolve the lsof path.
 *
 * Throw an error if it is not found.
 */
void
lsof_resolve_path(void)
{
	lsof_path = which("lsof");

	if (lsof_path == NULL)
		errx(1, "lsof is not in your PATH");
}
