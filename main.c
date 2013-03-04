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
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <err.h>

#include "fdcache.h"
#include "trace.h"
#include "lsof.h"
#include "utils.h"


#define _DEBUG_FLAG
int debug_flag = 0;


/*
 * Take any function with the file descriptor as first argument.
 */
void
process_fd_func(char *func_name, int argc, char **argv, char *result)
{
	int fd;
	char *human_fd, *size;

	fd = xatoi(argv[0]);
	size = argv[2];
	human_fd = lsof_get_human_fd(fd);

	printf("%s(%s, %s)\n", func_name, human_fd, size);
}


/*
 * Take any function with the file descriptor as first argument.
 */
void
process_func_seek(int argc, char **argv, char *result)
{
	int fd;
	char *human_fd, *offset, *whence;

	fd = xatoi(argv[0]);
	offset = argv[1];
	whence = argv[2];

	human_fd = lsof_get_human_fd(fd);
	printf("lseek(%s, %s, %s)\n", human_fd, offset, whence);
}


/*
 * Handle an 'open' call, update the fdcache accordingly.
 */
void
process_func_open(int argc, char **argv, char *result)
{
	int fd;

	if (argc != 2)
		errx(1, "error: open() with %u args", argc);

	fd = xatoi(result);

	fd_cache_add(fd, argv[0]);
}


/*
 * Handle a 'close' call, delete this fd from fdcache.
 */
void
process_func_close(int argc, char **argv, char *result)
{
	int fd;

	if (argc != 1)
		errx(1, "error: close() with %u args", argc);

	fd = xatoi(argv[0]);

	fd_cache_delete(fd);
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
	} else if (strcmp(func_name, "open") == 0) {
		process_func_open(argc, argv, result);
	} else if (strcmp(func_name, "close") == 0) {
		process_func_close(argc, argv, result);
	// } else if (strcmp(func_name, "recvfrom") == 0) {
	// 	process_fd_func(func_name, argc, argv, result);
	// } else if (strcmp(func_name, "sendto") == 0) {
	// 	process_fd_func(func_name, argc, argv, result);
	} else if (strcmp(func_name, "lseek") == 0) {
		process_func_seek(argc, argv, result);
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


void
sigint_handler(int sig)
{
	fprintf(stderr, "Interrupted\n");
	exit(1);
}


int
main(int argc, char **argv)
{
	int fd, opt;
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

	trace_resolve_path();
	lsof_resolve_path();
	lsof_refresh_cache(pid);


	signal(SIGINT, sigint_handler);

	fd = trace_open(pid);
	trace_read_lines(fd, process_func);
	close(fd);

	return 0;
}
