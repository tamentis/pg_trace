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


#define MAX_FUNCTION_ARGUMENTS	32


int
strace_open()
{
	int pipefd[2], pipe_r, pipe_w;
	int pid;

	if (pipe(pipefd) == -1)
		err(1, "open_strace:pipe()");

	pipe_r = pipefd[0];
	pipe_w = pipefd[1];

	pid = fork();
	if (pid == -1) {
		err(1, "open_strace:fork()");
	} else if (pid == 0) {
		if (dup2(pipe_w, STDERR_FILENO) == -1)
			err(1, "open_strace:dup2(pipe_w, stdin)");
		if (close(pipe_r) == -1)
			err(1, "open_strace:close(pipe_r)");
		if (execl("/usr/bin/strace", "strace",
					"-q",		/* quiet */
					"-s", "8",	/* no need for data */
					"-p", "29514",	/* pid to spy on */
					(char*)NULL) == -1)
			err(1, "-%s-", "open_strace:execl");
	}

	if (close(pipe_w) == -1)
		err(1, "open_strace:close(pipe_w)");

	return pipe_r;
}


/*
 * Drops a series of NUL bytes on the line and assign the various components to
 * the right pointers.
 */
void
strace_process_line(char *line, void (*func_handler)(char *, int, char **, char*))
{
	char *func_name;
	char *result = NULL;
	char *argv[MAX_FUNCTION_ARGUMENTS];
	char *c, *a;
	int argc = 0;

	// printf("LINE: %s", line);

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
		*a = '\0';
		argv[argc] = c;
		c = a + 1;
		argc++;
	}

	/* Extra the final argument (anything before the last parenthesis). */
	a = strchr(c, ')');
	if (a == NULL)
		errx(1, "process_line(): wrong last param syntax");
	*a = '\0';
	if (argc > 0) {
		argv[argc] = c;
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


void
strace_read_lines(int fd, void (*func_handler)(char *, int, char **, char*))
{
	FILE *fp;
	char line[1024];

	fp = fdopen(fd, "r");
	if (fp == NULL)
		err(1, "read_lines:fdopen()");

	while (fgets(line, sizeof(line), fp)) {
		strace_process_line(line, func_handler);
	}
}

