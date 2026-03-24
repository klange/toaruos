/**
 * @brief dirname - print directory name from path string
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>

static int usage(char * argv[]) {
	fprintf(stderr, "usage: %s [-z] string...\n", argv[0]);
	return 1;
}

int main(int argc, char * argv[]) {
	int endchr = '\n';
	int opt;
	while ((opt = getopt(argc, argv, "?z")) != -1) {
		switch (opt) {
			case 'z':
				endchr = '\0';
				break;
			case '?':
				return usage(argv);
		}
	}

	if (optind == argc) return usage(argv);

	for (int i = optind; i < argc; ++i) {
		printf("%s%c", dirname(argv[i]), endchr);
	}

	return 0;
}

