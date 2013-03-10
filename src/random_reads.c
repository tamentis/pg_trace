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

/*
 * Given a list of directory names, randomly pick files within them and read a
 * random number of bytes. When the paths is a database within a PostgreSQL
 * cluster, this makes debugging on pg_trace more convenient than running
 * random queries on a database with caching disabled.
 *
 * The following example will simulate random reads on the internal files of
 * both a local database and the "global" (shared) database.
 *
 * 	pg_cluster="/var/lib/postgresql/9.2"
 * 	random_reads "$pg_cluster/base/16384" "$pg_cluster/global"
 */

#include <sys/param.h>

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <err.h>



/* Keep a number of file descriptors open at the same time. */
FILE *fps[50] = {NULL};
int fps_cursor = 0;


/*
 * Read a random number of bytes off the given file.
 */
void
random_read(char *filename)
{
	int read_size;
	char buf[65536];
	FILE *fp;

	read_size = ((double)random() / (double)RAND_MAX) * sizeof(buf);

	/* Choose our fd slot randomly based on the previous random. */
	fps_cursor += (read_size  & 0xF);
	if (fps_cursor >= 50)
		fps_cursor -= 50;

	/* Close the previously declared file pointer. */
	fp = fps[fps_cursor];
	if (fp != NULL && fclose(fp) != 0)
		errx(1, "fclose()");

	fps[fps_cursor] = fopen(filename, "rb");
	fp = fps[fps_cursor];
	if (fp == NULL)
		errx(1, "fopen(%s)", filename);

	printf("pid=%u size=%u filename=%s\n", getpid(), read_size, filename);
	fread(buf, read_size, 1, fp);
}


/*
 * One out of 64 changes of picking the given file and calling the random_read
 * function.
 */
void
pick_file(char *filename)
{
	int pick;

	pick = random();

	if (pick < RAND_MAX / 64) {
		random_read(filename);
		sleep(1);
	}
}


int
main(int argc, char **argv)
{
	int i;
	DIR *dp;
	struct dirent *ep;
	char fullpath[MAXPATHLEN];

	if (argc < 2) {
		printf("usage: random_reads path ...\n");
		exit(1);
	}

	/* Loop for eternity and feed filenames to pick_file. */
	for (;;) {
		for (i = 1; i < argc; i++) {
			dp = opendir(argv[i]);
			if (dp == NULL)
				err(1, "opendir(%s)", argv[i]);

			while ((ep = readdir(dp)) != NULL) {
				if (ep->d_name[0] == '.')
					continue;

				snprintf(fullpath, sizeof(fullpath), "%s/%s",
						argv[i], ep->d_name);
				pick_file(fullpath);
			}

			if (closedir(dp) != 0)
				err(1, "closedir(dp)");
		}
	}

	/* You will never get here (hit ^C to exit). */
	return 0;
}
