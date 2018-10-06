/* vim:tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2014 K. Lange
 *
 * mkdir
 *
 * Create a directory.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

int makedir(const char * dir, int mask, int parents) {
	if (!parents) return mkdir(dir,mask);

	char * tmp = strdup(dir);
	char * c = tmp;
	while ((c = strchr(c+1,'/'))) {
		*c = '\0';
		if (mkdir(tmp,mask) < 0) {
			if (errno == EEXIST) {
				*c = '/';
				continue;
			} else {
				return -1;
			}
		}
		*c = '/';
		continue;
	}

	return mkdir(tmp, mask);
}

int main(int argc, char ** argv) {
	int retval = 0;
	int parents = 0;
	int opt;

	while ((opt = getopt(argc, argv, "m:p")) != -1) {
		switch (opt) {
			case 'm':
				fprintf(stderr, "%s: -m unsupported\n", argv[0]);
				return 1;
			case 'p':
				parents = 1;
				break;
		}
	}

	if (optind == argc) {
		fprintf(stderr, "%s: expected argument\n", argv[0]);
		return 1;
	}

	for (int i = optind; i < argc; ++i) {
		if (makedir(argv[i], 0777, parents) < 0) {
			if (parents && errno == EEXIST) continue;
			fprintf(stderr, "%s: %s: %s\n", argv[0], argv[i], strerror(errno));
			retval = 1;
		}
	}

	return retval;
}
