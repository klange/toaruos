/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 *
 * mktemp - create a temporary directory and print its name
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
		}
	}

	char * template;
	int i = optind;

	if (i == argc) {
		template = strdup("/tmp/tmp.XXXXXX");
	} else {
		template = strdup(argv[i]);
	}

	char * result = mktemp(template);

	if (!result) {
		fprintf(stderr, "%s: %s\n", argv[0], strerror(errno));
		return 1;
	}

	if (!quiet) {
		fprintf(stdout, "%s\n", result);
	}

	if (!dry_run) {
		if (directory) {
			if (mkdir(result,0777) < 0) {
				fprintf(stderr, "%s: mkdir: %s: %s\n", argv[0], result, strerror(errno));
				return 1;
			}
		} else {
			FILE * f = fopen(result,"w");
			if (!f) {
				fprintf(stderr, "%s: open: %s: %s\n", argv[0], result, strerror(errno));
				return 1;
			}
		}
	}

	return 0;
}
