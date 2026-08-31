/**
 * @brief touch - Create or update file timestamps
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013 K. Lange
 */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

int main(int argc, char * argv[]) {
	if (argc < 2) {
		fprintf(stderr, "%s: argument expected\n", argv[0]);
		return 1;
	}

	int out = 0;
	for (int i = 1; i < argc; ++i) {
		FILE * f = fopen(argv[i], "a");
		if (!f) {
			fprintf(stderr, "%s: %s: %s\n", argv[0], argv[i], strerror(errno));
			out = 1;
			continue;
		}
		struct timespec times[2];
		times[0].tv_nsec = UTIME_NOW;
		times[1].tv_nsec = UTIME_NOW;
		futimens(fileno(f), times);
		fclose(f);
	}

	return out;
}
