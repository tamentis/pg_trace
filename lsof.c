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
#include <string.h>
#include <err.h>

#include "pg_trace.h"


extern int debug_flag;


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

	debug("lsof_open(pid=%d)\n", pid);

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
		if (execl("/usr/bin/lsof", "lsof",
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


void
lsof_read_lines(int fd)
{
	FILE *fp;
	char line[MAX_LINE_LENGTH], type, *c;
	fd_desc *current = NULL;

	debug("lsof_read_lines(fd=%d)\n", fd);

	fp = fdopen(fd, "r");
	if (fp == NULL)
		err(1, "lsof_read_lines:fdopen()");

	while (fgets(line, sizeof(line), fp)) {
		/* Strip new-lines */
		c = strchr(line, '\n');
		if (c != NULL)
			*c = '\0';

		/* Remove the type off the line. */
		type = line[0];
		c = line + 1;

		/* Access mode fields help us filter io specific file
		 * descriptors. */
		if (type == 'a') {
			if (c[0] == ' ') {
				debug("lsof_read_lines() skipping "
				      "uninterresting descriptor\n");
				current = NULL;
			} else {
				debug("lsof_read_lines() found record\n");
				current = fd_cache_next();
			}
			continue;
		}

		if (current == NULL)
			continue;

		switch (type) {
		/* access mode */
		case 'a':
			break;
		/* pid, the first record */
		case 'p':
			continue;
			break;
		/* fd number */
		case 'f':
			current->fd = xatoi(c);
			break;
		/* file type */
		case 't':
			if (strcmp(line, "CHR") == 0)
				current->type = FILE_TYPE_CHR;
			else if (strcmp(line, "REG") == 0)
				current->type = FILE_TYPE_REG;
			else if (strcmp(line, "FIFO") == 0)
				current->type = FILE_TYPE_FIFO;
			else if (strcmp(line, "IPv4") == 0)
				current->type = FILE_TYPE_IPV4;
			else if (strcmp(line, "IPv6") == 0)
				current->type = FILE_TYPE_IPV6;
			else {
				current->type = FILE_TYPE_UNKNOWN;
			}
			break;
		/* file name */
		case 'n':
			current->name = xstrdup(c);
			break;
		default:
			errx(1, "lsof_read_lines() unknown type '%c'", type);
			break;
		}
	}
}


void
lsof_refresh_cache(pid_t pid)
{
	int pipe;

	debug("lsof_refresh_cache(pid=%d)\n", pid);

	fd_cache_clear();

	pipe = lsof_open(pid);
	lsof_read_lines(pipe);
}

