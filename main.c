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


int debug_flag = 0;


#define MAX_HUMAN_FD_LENGTH	256


/*
 * Get human-readable file descriptor, if possible.
 */
char *
get_human_fd(int fd)
{
	fd_desc *desc;
	char buffer[MAX_HUMAN_FD_LENGTH];
	char *relname;

	desc = lsof_get_fd_desc(fd);

	if (desc == NULL) {
		snprintf(buffer, sizeof(buffer), "fd=%u", fd);
	} else {
		relname = pg_get_relname_from_filepath(desc->name);

		if (relname == NULL) {
			snprintf(buffer, sizeof(buffer), "filenode=%s", desc->name);
		} else {
			snprintf(buffer, sizeof(buffer), "relname=%s", relname);
		}
	}

	return xstrdup(buffer);
}


/*
 * Take any function with the file descriptor as first argument.
 */
void
process_fd_func(char *func_name, int argc, char **argv, char *result)
{
	int fd;
	char *human_fd;

	fd = xatoi(argv[0]);

	human_fd = get_human_fd(fd);
	printf("%s(%s)\n", func_name, human_fd);
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
	// } else if (strcmp(func_name, "recvfrom") == 0) {
	// 	process_fd_func(func_name, argc, argv, result);
	// } else if (strcmp(func_name, "sendto") == 0) {
	// 	process_fd_func(func_name, argc, argv, result);
	} else if (strcmp(func_name, "lseek") == 0) {
		process_fd_func(func_name, argc, argv, result);
	} else {
		debug("unknown func: %s, argc: %d\n", func_name, argc);
	}
}


void
usage()
{
	fprintf(stderr, "usage: pg_trace [-h] [-d] [-p pid]\n");
	exit(1);
}


int
main(int argc, char **argv)
{
	int pipe, opt;
	extern char *optarg;
	pid_t pid = 0;

	while ((opt = getopt(argc, argv, "hp:d")) != -1) {
		switch (opt) {
		case 'p':
			pid = xatoi(optarg);
			break;
		case 'd':
			debug_flag = 1;
			break;
		case 'h':
		default:
			usage();
		}
	}

	if (pid == 0)
		usage();

	lsof_refresh_cache(pid);

	pipe = strace_open(pid);
	strace_read_lines(pipe, process_func);

	return 0;
}