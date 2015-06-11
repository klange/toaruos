/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013 Kevin Lange
 */
/*
 * stat
 *
 * Displays information on a file's inode.
 */
#include <stdio.h>
#include <sys/stat.h>
#include <syscall.h>
#include <stdint.h>

#include <sys/time.h>

int main(int argc, char ** argv) {
	int dereference = 0;
	if (argc < 2) {
		fprintf(stderr,"%s: expected argument\n", argv[0]);
		return 1;
	}

	char * file = argv[1];

	if (argc > 2) {
		if (!strcmp(argv[1],"-L")) {
			dereference = 1;
		}
		file = argv[2];
	}

	struct stat _stat;
	if (dereference) {
		if (stat(file, &_stat) < 0) return 1;
	} else {
		if (lstat(file, &_stat) < 0) return 1;
	}

	printf("0x%x bytes\n", _stat.st_size);

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

	printf("st_mode  0x%x %d\n", (uint32_t)f->st_mode  , sizeof(f->st_mode  ));
	printf("st_nlink 0x%x %d\n", (uint32_t)f->st_nlink , sizeof(f->st_nlink ));
	printf("st_uid   0x%x %d\n", (uint32_t)f->st_uid   , sizeof(f->st_uid   ));
	printf("st_gid   0x%x %d\n", (uint32_t)f->st_gid   , sizeof(f->st_gid   ));
	printf("st_rdev  0x%x %d\n", (uint32_t)f->st_rdev  , sizeof(f->st_rdev  ));
	printf("st_size  0x%x %d\n", (uint32_t)f->st_size  , sizeof(f->st_size  ));

	printf("0x%x\n", ((uint32_t *)f)[0]);

	return 0;
}

/*
 * vim:tabstop=4
 * vim:noexpandtab
 * vim:shiftwidth=4
 */
