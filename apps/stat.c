/* vim: ts=4 sw=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2018 K. Lange
 *
 * stat
 *
 * Display file status.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <sys/stat.h>
#include <sys/time.h>

static void show_usage(int argc, char * argv[]) {
	printf(
			"stat - display file status\n"
			"\n"
			"usage: %s [-Lq] PATH\n"
			"\n"
			" -L     \033[3mdereference symlinks\033[0m\n"
			" -q     \033[3mdon't print anything, just return 0 if file exists\033[0m\n"
			" -?     \033[3mshow this help text\033[0m\n"
			"\n", argv[0]);
}

static int dereference = 0, quiet = 0;

static int stat_file(char * file) {

	struct stat _stat;
	int result;

	if (dereference) {
		result = stat(file, &_stat);
	} else {
		result = lstat(file, &_stat);
	}

	if (result == -1) {
		if (!quiet) {
			fprintf(stderr, "stat: %s: %s\n", file, strerror(errno));
		}
		return 1;
	}

	if (quiet) return 0;

	printf("0x%x bytes\n", (unsigned int)_stat.st_size);

	if (S_ISDIR(_stat.st_mode)) {
		printf("Is a directory.\n");
	} else if (S_ISFIFO(_stat.st_mode)) {
		printf("Is a pipe.\n");
	} else if (S_ISLNK(_stat.st_mode)) {
		printf("Is a symlink.\n");
	} else if (_stat.st_mode & 0111) {
		printf("Is executable.\n");
	}

	struct stat * f = &_stat;

	printf("st_dev   0x%x %d\n", (unsigned int)f->st_dev   , (unsigned int)sizeof(f->st_dev  ));
	printf("st_ino   0x%x %d\n", (unsigned int)f->st_ino   , (unsigned int)sizeof(f->st_ino  ));
	printf("st_mode  0x%x %d\n", (unsigned int)f->st_mode  , (unsigned int)sizeof(f->st_mode  ));
	printf("st_nlink 0x%x %d\n", (unsigned int)f->st_nlink , (unsigned int)sizeof(f->st_nlink ));
	printf("st_uid   0x%x %d\n", (unsigned int)f->st_uid   , (unsigned int)sizeof(f->st_uid   ));
	printf("st_gid   0x%x %d\n", (unsigned int)f->st_gid   , (unsigned int)sizeof(f->st_gid   ));
	printf("st_rdev  0x%x %d\n", (unsigned int)f->st_rdev  , (unsigned int)sizeof(f->st_rdev  ));
	printf("st_size  0x%x %d\n", (unsigned int)f->st_size  , (unsigned int)sizeof(f->st_size  ));

	printf("0x%x\n", (unsigned int)((uint32_t *)f)[0]);

	return 0;

}

int main(int argc, char ** argv) {
	int opt;

	while ((opt = getopt(argc, argv, "?Lq")) != -1) {
		switch (opt) {
			case 'L':
				dereference = 1;
				break;
			case 'q':
				quiet = 1;
				break;
			case '?':
				show_usage(argc,argv);
				return 1;
		}
	}

	if (optind >= argc) {
		show_usage(argc, argv);
		return 1;
	}

	int ret = 0;

	while (optind < argc) {
		ret |= stat_file(argv[optind]);
		optind++;
	}

	return ret;

}

