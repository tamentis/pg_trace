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
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <err.h>

#include "pg_trace.h"


int debug = 0;


/*
 * Take any function with the file descriptor as first argument.
 */
void
process_fd_func(char *func_name, int argc, char **argv, char *result)
{
	int fd;
	fd_desc *desc;

	fd = xatoi(argv[0]);

	desc = fd_cache_get(fd);

	printf("%s fd=%d %s\n", func_name, fd, desc->name);
}


/*
 * Check if we have a handler for this function and run it.
 */
void
process_func(char *func_name, int argc, char **argv, char *result)
{
	if (strcmp(func_name, "read") == 0) {
		process_fd_func(func_name, argc, argv, result);
	} else if (strcmp(func_name, "write") == 0) {
		process_fd_func(func_name, argc, argv, result);
	} else if (strcmp(func_name, "recvfrom") == 0) {
		process_fd_func(func_name, argc, argv, result);
	} else if (strcmp(func_name, "sendto") == 0) {
		process_fd_func(func_name, argc, argv, result);
	} else if (strcmp(func_name, "lseek") == 0) {
		process_fd_func(func_name, argc, argv, result);
	} else {
		printf("unknown func: %s, argc: %d\n", func_name, argc);
	}
}


int
main(int argc, const char **argv)
{
	int pipe;
	pid_t pid;

	// TODO - getopt
	if (argc != 2) errx(1, "usage?");

	debug = 1;

	pid = xatoi((char*)argv[1]);

	lsof_refresh_cache(pid);

	pipe = strace_open(pid);
	strace_read_lines(pipe, process_func);

	return 0;
}
