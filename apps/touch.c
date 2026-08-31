/**
 * @brief touch - Create or update file timestamps
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2026 K. Lange
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

static int usage(char * argv[]) {
	fprintf(stderr, "usage: %s [-acm] [-r ref_file|-t time|-d date_Time] file...\n", argv[0]);
	return 1;
}

int main(int argc, char * argv[]) {
	int set_times = 0;
	int dont_create = 0;
	int opt;

	while ((opt = getopt(argc, argv, "acmd:r:t:")) != -1) {
		switch (opt) {
			case 'a':
				set_times |= 1;
				break;
			case 'm':
				set_times |= 2;
				break;
			case 'c':
				dont_create = 1;
				break;
			case 'r':
			case 't':
			case 'd':
				return fprintf(stderr, "%s: -%c not supported\n", argv[0], opt), 2;
			default:
				return usage(argv);
		}
	}

	if (optind == argc) return usage(argv);

	struct timespec times[2];
	times[0].tv_nsec = (set_times == 0 || (set_times & 1)) ? UTIME_NOW : UTIME_OMIT;
	times[1].tv_nsec = (set_times == 0 || (set_times & 2)) ? UTIME_NOW : UTIME_OMIT;

	int out = 0;

	for (int i = optind; i < argc; ++i) {
		if (access(argv[i], F_OK) && errno == ENOENT) {
			if (dont_create) continue;
			int fd = creat(argv[i], S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH);
			if (fd == -1 || futimens(fd, times) == -1) {
				fprintf(stderr, "%s: %s: %s\n", argv[0], argv[i], strerror(errno));
				out |= 1;
			}
			if (fd != -1) close(fd);
		} else {
			if (utimensat(AT_FDCWD, argv[i], times, 0) == -1) {
				fprintf(stderr, "%s: %s: %s\n", argv[0], argv[i], strerror(errno));
				out |= 1;
			}
		}
	}

	return out;
}
