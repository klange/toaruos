/**
 * @brief realpath - print resolved absolute path
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2026 K. Lange
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

static int usage(char * argv[]) {
#define X_S "\033[3m"
#define X_E "\033[0m"
	fprintf(stderr,
		"usage: %s [-E|-e] " X_S "file" X_E "...\n"
		"\n"
		"  -E  " X_S "don't treat it as an error if the path doesn't exist" X_E "\n"
		"  -e  " X_S "do treat it as an error if the path doesn't exist" X_E "\n"
		"\n", argv[0]);
	return 1;
}

int main(int argc, char * argv[]) {
	int is_error = 0;
	int ret = 0;
	int opt;
	while ((opt = getopt(argc, argv, "eE")) != -1) {
		switch (opt) {
			case 'e':
				is_error = 1;
				break;
			case 'E':
				is_error = 0;
				break;
			case '?':
				return usage(argv);
		}
	}

	if (optind == argc) {
		return usage(argv);
	}

	char buf[PATH_MAX];
	for (; optind < argc; optind++) {
		char * res = realpath(argv[optind], buf);
		/* TODO if errno was ENOENT already we need to do this manually,
		 *      but our realpath(3) doesn't do that yet anyway. */
		if (!res || (is_error && access(res, F_OK))) {
			fprintf(stderr, "%s: %s: %s\n", argv[0], argv[optind], strerror(errno));
			ret |= 1;
			continue;
		}
		fprintf(stdout, "%s\n", res);
	}

	return ret;
}
