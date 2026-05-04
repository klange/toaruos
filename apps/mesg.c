/**
 * @brief mesg - set g+w / g-w,o-w on a tty
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2026 K. Lange
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static int usage(char * argv[]) {
	fprintf(stderr, "usage: %s [n | y]\n", argv[0]);
	return 2;
}

int main(int argc, char * argv[]) {
	/* Find a TTY as specified in POSIX */
	int fd;
	for (fd = 0; fd < 3; fd++) if (isatty(fd)) break;
	if (fd == 3) return 2;

	/* Get current mode */
	struct stat st;
	if (fstat(fd, &st) == -1) return perror("stat"), 2;

	if (argc < 2) {
		if (st.st_mode & (S_IWGRP | S_IWOTH)) return puts("is y"), 0;
		puts("is n");
		return 1;
	}

	if (!strcmp(argv[1],"y")) {
		if (fchmod(fd, st.st_mode | S_IWGRP) == -1) return perror("fchmod"), 2;
		return 0;
	}

	if (!strcmp(argv[1],"n")) {
		if (fchmod(fd, st.st_mode & ~(S_IWGRP | S_IWOTH)) == -1) return perror("fchmod"), 2;
		return 1;
	}

	return usage(argv);
}
