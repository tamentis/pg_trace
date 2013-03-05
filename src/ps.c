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
 * Since there is no better way to get the current working directory of a
 * process (in a cross-platform fashion), we use 'ps e pid' at the start of
 * pg_trace and assume this value remain the same. This path is used when
 * pg_trace encounters relative path and needs to resolve them.
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <err.h>

#include "utils.h"
#include "xmalloc.h"
#include "which.h"


char *ps_path = NULL;


int
ps_open(char *args, pid_t pid)
{
	int pipefd[2], pipe_r, pipe_w;
	int ps_pid;
	char *cpid;

	cpid = xitoa((int)pid);

	if (pipe(pipefd) == -1)
		err(1, "ps_open:pipe()");

	pipe_r = pipefd[0];
	pipe_w = pipefd[1];

	ps_pid = fork();
	if (ps_pid == -1) {
		err(1, "ps_open:fork()");
	} else if (ps_pid == 0) {
		if (dup2(pipe_w, STDOUT_FILENO) == -1)
			err(1, "ps_open:dup2(pipe_w, stdout)");

		if (close(pipe_r) == -1)
			err(1, "ps_open:close(pipe_r)");

		if (execl(ps_path, "ps", args, cpid, (char*)NULL) == -1) {
			err(1, "ps_open:execl()");
		}
	}

	debug("ps_open() pid: %u\n", ps_pid);

	if (close(pipe_w) == -1)
		err(1, "ps_open:close(pipe_w)");

	return pipe_r;
}


/*
 * Given a PID, attempt to extract the PWD from ps e and return a newly
 * allocated char* to be free'd.
 */
char *
ps_get_pwd(pid_t pid)
{
	int fd;
	FILE *fp;
	char line[4096]; // capture as much as possible
	char *c, *start;

	fd = ps_open("e", pid);
	fp = fdopen(fd, "r");

	/* Drop the header. */
	c = fgets(line, sizeof(line), fp);
	if (c == NULL)
		err(1, "ps_get_pwd:fgets() (header)");

	/* Get the line we need. */
	c = fgets(line, sizeof(line), fp);
	if (c == NULL)
		err(1, "ps_get_pwd:fgets() (record)");

	close(fd);

	/* Do we have PWD= in there? */
	c = strstr(line, "PWD=");
	if (c == NULL)
		return NULL;

	/*
	 * Since ps' output is not particularly nice to parse, the files with
	 * spaces (thank you Apple) are not escaped. One hack to get the full
	 * PWD is to find the following variable or end of line and backtrack
	 * from there.
	 */
	start = c + 4; // strlen("PWD=")
	c = strstr(start, "=");

	/* PWD is presumably the last var, let's remove the new-line. */
	if (c == NULL) {
		c = strchr(c, '\n');
		if (c != NULL)
			*c = '\0';
		return xstrdup(start);
	}

	/*
	 * PWD is not the last var, find the first space before the next
	 * variable name and stop the string there.
	 */
	while (*c != ' ')
		c--;

	*c = '\0';

	return xstrdup(start);
}


/*
 * Resolve the ps path, it's likely to be in /bin but you never know.
 */
void
ps_resolve_path(void)
{
	ps_path = which("ps");

	if (ps_path == NULL)
		errx(1, "ps is not in your PATH (good luck)");
}
