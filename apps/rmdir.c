/**
 * @brief rmdir - remove empty directories
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2026 K. Lange
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>
#include <errno.h>

static int usage(char * argv[]) {
#define _I "\033[3m"
#define _E "\033[0m\n"
	fprintf(stderr, "usage: %s [-p] path...\n"
		"\n"
		"Deletes empty directories.\n"
		"\n"
		"  -p   " _I "Remove parents if also empty" _E
		"  -v   " _I "Print directory names when they are successfully removed" _E
		"\n", argv[0]);
#undef _I
#undef _E
	return 1;
}

int main(int argc, char * argv[]) {
	int parents = 0;
	int verbose = 0;
	int opt;
	while ((opt = getopt(argc, argv, "pv")) != -1) {
		switch (opt) {
			case 'p':
				parents = 1;
				break;
			case 'v':
				verbose = 1;
				break;
			default:
				return usage(argv);
		}
	}

	if (optind == argc) return usage(argv);

	int ret = 0;
	while (optind < argc) {
		if (rmdir(argv[optind]) < 0) {
			fprintf(stderr, "%s: %s: %s\n", argv[0], argv[optind], strerror(errno));
			ret |= 1;
			optind++;
			continue;
		}

		if (verbose) fprintf(stdout, "%s\n", argv[optind]);

		if (parents) {
			char * parent = dirname(argv[optind]);
			while (parent && strcmp(parent,".") && strcmp(parent,"/")) {
				if (rmdir(parent) < 0) {
					fprintf(stderr, "%s: %s: %s\n", argv[0], parent, strerror(errno));
					ret |= 1;
					break;
				}

				if (verbose) fprintf(stdout, "%s\n", parent);

				parent = dirname(parent);
			}
		}
		optind++;
	}

	return ret;

}
