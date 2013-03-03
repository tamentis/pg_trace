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
 * Marks the first parenthesis as a NUL byte to delimite the func_name and
 * return a pointer to the beginning of the arguments.
 */
char *
_skip_func_name(char *s)
{
	char *c;

	c = strchr(s, '(');
	if (c == NULL)
		errx(1, "process_line(): not a function: %s", s);

	*c = '\0';
	c++;

	return c;
}


/*
 * Returns 1 if the number of backslashes before the double-quote is odd.
 */
int
_is_escaped(char *s)
{
	int count = 0;

	while (*(s - 1) == '\\') {
		count++;
		s--;
	}

	if ((count & 1) == 1)
		return 1;

	return 0;
}


/*
 * Find the next comma or parenthesis, sets it as NUL byte to delimit the
 * possible previous argument.
 *
 * If the first character of *s is a double quote or '{' try to find the
 * matching character.
 *
 * FIXME: this can be more robust (escape characters?) and we don't do a good
 * job on cropped data (keep stuff at the end...)
 */
char *
_extract_argument(char **startp)
{
	char *start = *startp;
	char *end, *valueend;

	/* Strip spaces. */
	while (*start == ' ')
		start++;

	/* This is a quoted argument, find the final double-quote. */
	if (*start == '"') {
		start++;
		end = start;
		for (;;) {
			end = strchr(end, '"');
			if (!_is_escaped(end)) {
				*end = '\0';
				break;
			}
			end++;
		}
		valueend = end + 1;
	} else if (*start == '{') {
		start++;
		end = strchr(start, '}');
		*end = '\0';
		valueend = end + 1;
	} else {
		valueend = start;
	}

	/* More arguments. */
	end = strchr(valueend, ',');
	if (end == NULL)
		end = strchr(valueend, ')');
	if (end == NULL)
		return NULL;

	/* No arguments left, return NULL. */
	if (end == start)
		start = NULL;

	/* At this point 'end' should point to a comma or parenthesis. */
	*end = '\0';
	*startp = end + 1;

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
	char *argv[MAX_FUNCTION_ARGUMENTS] = { 0 };
	char *c, *a;
	int argc = 0;

	func_name = line;

	c = _skip_func_name(line);

	/* Extract all the arguments. */
	while ((a = _extract_argument(&c)) != NULL) {
		argv[argc] = a;
		argc++;
	}

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

