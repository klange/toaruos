/**
 * @brief Test the popen function
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

int main(int argc, char * argv[]) {
	int opt;

	char * mode = "r";

	while ((opt = getopt(argc, argv, "rw")) != -1) {
		switch (opt) {
			case 'r':
				mode = "r";
				break;
			case 'w':
				mode = "w";
				break;
			case '?':
				return 1;
		}
	}

	if (optind == argc) return 1;

	FILE * p = popen(argv[optind],mode);

	if (!p) {
		fprintf(stderr, "%s: %s(%s): %s\n", argv[0], argv[optind], mode, strerror(errno));
		return 2;
	}

	if (*mode == 'r') {
		while (!feof(p)) {
			int c = fgetc(p);
			if (c == EOF) {
				fprintf(stderr, "eof\n");
			} else {
				fprintf(stderr, "[%c]\n", c);
			}
		}
	} else if (*mode == 'w') {
		fprintf(p, "hello world\n");
	}

	int ret = pclose(p);
	fprintf(stderr, "ret=%d\n", ret);

	return 0;
}
