/**
 * @brief Create directories
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2026 K. Lange
 */
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <toaru/modecalc.h>

int makedir(const char * dir, mode_t mode, mode_t mask, mode_t parent_mask, int parents) {
	char * tmp = strdup(dir);
	if (parents) {
		umask(parent_mask);
		char * c = tmp;
		while ((c = strchr(c+1,'/'))) {
			*c = '\0';
			if (mkdir(tmp,0777) < 0) {
				if (errno == EEXIST) {
					*c = '/';
					continue;
				} else {
					free(tmp);
					return -1;
				}
			}
			*c = '/';
			continue;
		}
	}

	umask(mask);
	int out = mkdir(tmp, mode);
	free(tmp);
	return out;
}

int main(int argc, char ** argv) {
	int retval = 0;
	int parents = 0;
	int opt;

	mode_t mask = umask(0);
	mode_t parent_mask = mask & ~(S_IWUSR | S_IXUSR);
	mode_t mode = 0777;

	while ((opt = getopt(argc, argv, "m:p")) != -1) {
		switch (opt) {
			case 'm':
				mode = mode_calc(optarg, 0777, mask, 0);
				mask = mask & ~mode;
				break;
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
		if (makedir(argv[i], mode, mask, parent_mask, parents) < 0) {
			if (parents && errno == EEXIST) continue;
			fprintf(stderr, "%s: %s: %s\n", argv[0], argv[i], strerror(errno));
			retval = 1;
		}
	}

	return retval;
}
