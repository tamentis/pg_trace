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


#define MAX_FUNCTION_ARGUMENTS	32


int
strace_open(pid_t pid)
{
	int pipefd[2], pipe_r, pipe_w;
	int strace_pid;
	char *cpid;

	cpid = xitoa((int)pid);

	if (pipe(pipefd) == -1)
		err(1, "strace_open:pipe()");

	pipe_r = pipefd[0];
	pipe_w = pipefd[1];

	strace_pid = fork();
	if (strace_pid == -1) {
		err(1, "strace_open:fork()");
	} else if (strace_pid == 0) {
		if (dup2(pipe_w, STDERR_FILENO) == -1)
			err(1, "strace_open:dup2(pipe_w, stderr)");
		if (close(pipe_r) == -1)
			err(1, "strace_open:close(pipe_r)");
		if (execl("/usr/bin/strace", "strace",
					"-q",		/* quiet */
					"-s", "8",	/* no need for data */
					"-p", cpid,	/* pid to spy on */
					(char*)NULL) == -1)
			err(1, "strace_open:execl()");
	}

	if (close(pipe_w) == -1)
		err(1, "strace_open:close(pipe_w)");

	return pipe_r;
}


/*
 * If the first character of s is a double quote, return a pointer one byte
 * further and pull the final NUL byte closer.
 *
 * FIXME: this can be more robust (escape characters?)
 */
char *
_extract_argument(char *start, char *end)
{
	if (*start == '"') {
		start++;
		end--;
	}

	*end = '\0';

	return start;
}


/*
 * Parses out the function name and its argument, calling the provided function
 * with the broken down line elements.
 *
 * Since this function drops NUL bytes everywhere, you shouldn't use line after
 * calling this function.
 */
void
strace_process_line(char *line, void (*func_handler)(char *, int, char **, char*))
{
	char *func_name;
	char *result = NULL;
	char *argv[MAX_FUNCTION_ARGUMENTS];
	char *c, *a;
	int argc = 0;

	/* Extract function name. */
	func_name = line;
	c = strchr(line, '(');
	if (c == NULL)
		errx(1, "process_line(): not a function: %s", line);

	*c = '\0';
	c++;

	// TODO check for NUL byte here, in case the ( was the last char.

	/* Extract the arguments (anything before a comma) */
	while ((a = strchr(c, ',')) != NULL) {
		argv[argc] = _extract_argument(c, a);
		c = a + 1;
		argc++;
	}

	/* Extract the final argument. */
	a = strchr(c, ')');
	if (a == NULL) {
		errx(1, "process_line(): wrong last param syntax");
	} else if (a != c) {
		argv[argc] = _extract_argument(c, a);
		argc++;
	}
	c = a + 1;

	/* Extract a function return if any. */
	a = strchr(c, '=');
	if (a != NULL) {
		/* Skip the space (FIXME: skip all blanks) */
		a++;
		result = a;

		/* Wipe the new-line. */
		a = strchr(a, '\n');
		if (a != NULL)
			*a = '\0';
	}

	func_handler(func_name, argc, argv, result);
}


/*
 * Read through the file descriptor, passing each parsed line to
 * strace_process_line.
 */
void
strace_read_lines(int fd, void (*func_handler)(char *, int, char **, char*))
{
	FILE *fp;
	char line[MAX_LINE_LENGTH];

	fp = fdopen(fd, "r");
	if (fp == NULL)
		err(1, "strace_read_lines:fdopen()");

	while (fgets(line, sizeof(line), fp)) {
		strace_process_line(line, func_handler);
	}
}

