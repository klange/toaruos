/**
 * @brief hostname - Prints or sets the system hostname.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2018 K. Lange
 */
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#define HOSTNAME_FILE "/etc/hostname"

static int usage(char * argv[]) {
	fprintf(stderr, "usage: %s [-fsd] [hostname]\n", argv[0]);
	return 1;
}

int main(int argc, char * argv[]) {
	int opt;
	while ((opt = getopt(argc, argv, "dfs")) != -1) {
		switch (opt) {
			case 'd':
			case 'f':
			case 's':
				break; /* ignore all of these */
			case '?':
				return usage(argv);
		}
	}

	if (optind + 1 < argc) usage(argv);

	if (optind < argc) {
		if (sethostname(argv[optind], strlen(argv[optind])) == -1) return fprintf(stderr, "%s: sethostname: %s\n", argv[0], strerror(errno)), 1;
		FILE * f = fopen(HOSTNAME_FILE, "w");
		if (!f) return fprintf(stderr, "%s: %s: %s\n", argv[0], HOSTNAME_FILE, strerror(errno)), 1;
		fprintf(f, "%s\n", argv[optind]);
		fclose(f);
		return 0;
	}

	char tmp[256] = {0};
	if (gethostname(tmp, 255) == -1) return fprintf(stderr, "%s: gethostname: %s\n", argv[0], strerror(errno)), 1;
	printf("%s\n", tmp);
	return 0;
}
