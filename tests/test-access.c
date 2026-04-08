/**
 * @brief Test the access and eaccess functions.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2026 K. Lange
 */
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

extern int eaccess(const char * pathname, int mode);

int main(int argc, char * argv[]) {
	int opt;
	int mode = 0;
	int (*func)(const char*,int) = access;

	while ((opt = getopt(argc, argv, "fxwre")) != -1) {
		switch (opt) {
			case 'f':
				mode |= F_OK;
				break;
			case 'w':
				mode |= W_OK;
				break;
			case 'r':
				mode |= R_OK;
				break;
			case 'x':
				mode |= X_OK;
				break;
			case 'e':
				func = eaccess;
				break;
		}
	}

	if (optind == argc) return fprintf(stderr, "expected argument\n"), 1;

	int ret = func(argv[optind], mode);

	fprintf(stdout, "%s(%d): %d (%s)\n", argv[optind], mode, ret, strerror(errno));

	return 0;
}
