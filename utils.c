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
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <err.h>

#include "xmalloc.h"


/*
 * Converts a char to int, with fatal error if anything goes wrong. This
 * function is also able to convert an hexadecimal value if it starts with \x
 * or 0x.
 */
int
xatoi(char *c)
{
	int base = 10;
	long l;
	char *endptr;

	if (strncmp(c, "0x", 2) == 0 || strncmp(c, "\\x", 2) == 0) {
		c += 2;
		base = 16;
	}

	l = strtol(c, &endptr, base);

	if (*endptr != '\0')
		errx(1, "xatoi() invalid number");

	if (l == LONG_MAX || l == LONG_MIN) {
		err(1, "xatoi()");
	}

	if (l < (long)INT_MIN || l > (long)INT_MAX) {
		errx(1, "xatoi() too large for int");
	}

	return (int)l;
}


/*
 * Converts a char to int, returns 0 if anything goes wrong. This function is
 * also able to convert an hexadecimal value if it starts with \x or 0x.
 */
int
xatoi_or_zero(char *c)
{
	int base = 10;
	long l;
	char *endptr;

	if (strncmp(c, "0x", 2) == 0 || strncmp(c, "\\x", 2) == 0) {
		c += 2;
		base = 16;
	}

	l = strtol(c, &endptr, base);

	if (*endptr != '\0')
		return 0;

	if (l == LONG_MAX || l == LONG_MIN)
		return 0;

	if (l < (long)INT_MIN || l > (long)INT_MAX) {
		return 0;
	}

	return (int)l;
}


/*
 * Converts an int to char*.
 */
char *
xitoa(int i)
{
	char buf[16];

	snprintf(buf, sizeof(buf), "%d", i);

	return xstrdup(buf);
}
