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
 * Given a directory name, randomly pick files within it and read a random
 * number of bytes from them. When the path is a database within a PostgreSQL
 * cluster, this makes debugging on pg_trace more convenient than running
 * random queries on a database with caching disabled.
 */

#include <sys/param.h>

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <err.h>


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

	fp = fopen(filename, "rb");
	if (fp == NULL)
		errx(1, "fopen(%s)", filename);

	printf("pid=%u size=%u filename=%s\n", getpid(), read_size, filename);
	fread(buf, read_size, 1, fp);

	if (fclose(fp) != 0)
		errx(1, "fclose()");
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
	DIR *dp;
	struct dirent *ep;
	char fullpath[MAXPATHLEN];

	if (argc != 2) {
		printf("usage: random_reads path\n");
		exit(1);
	}

	dp = opendir(argv[1]);
	if (dp == NULL)
		err(1, "opendir(%s)", argv[1]);

	/* Loop for eternity and feed filenames to pick_file. */
	for (;;) {
		rewinddir(dp);

		while ((ep = readdir(dp)) != NULL) {
			snprintf(fullpath, sizeof(fullpath), "%s/%s",
					argv[1], ep->d_name);
			pick_file(fullpath);
		}
	}

	if (closedir(dp) != 0)
		err(1, "closedir(dp)");

	return 0;
}
