/**
 * @brief mktemp - create a temporary directory and print its name
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018-2026 K. Lange
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>

int main(int argc, char * argv[]) {
	int opt;
	int dry_run = 0;
	int quiet = 0;
	int directory = 0;

	while ((opt = getopt(argc,argv,"duq")) != -1) {
		switch (opt) {
			case 'd':
				directory = 1;
				break;
			case 'u':
				dry_run = 1;
				break;
			case 'q':
				quiet = 1;
				break;
			case '?':
				fprintf(stderr, "usage: %s [-d] [-u] [-q] [template]\n", argv[0]);
				return 1;
		}
	}

	char * template = strdup(optind == argc ? "/tmp/tmp.XXXXXX" : argv[optind]);

	if (dry_run) {
		char * res = mktemp(template);
		if (*res) {
			if (!quiet) fprintf(stdout, "%s\n", res);
			return 0;
		}
	} else if (directory) {
		char * res = mkdtemp(template);
		if (res) {
			if (!quiet) fprintf(stdout, "%s\n", res);
			return 0;
		}
	} else {
		int fd = mkstemp(template);
		if (fd != -1) {
			if (!quiet) fprintf(stdout, "%s\n", template);
			return 0;
		}
	}

	fprintf(stderr, "%s: %s\n", argv[0], strerror(errno));
	return 1;
}
