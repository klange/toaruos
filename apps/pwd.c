/**
 * @brief Print the current working directory
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018-2026 K. Lange
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <err.h>
#include <sys/stat.h>

int main(int argc, char * argv[]) {
	int opt;
	int logical = 0;

	while ((opt = getopt(argc, argv, "LP")) != -1) {
		switch (opt) {
			case 'L':
				logical = 1;
				break;
			case 'P':
				logical = 0;
				break;
			default:
				fprintf(stderr, "usage: %s [-L|-P]\n", argv[0]);
				return 1;
		}
	}

	if (optind != argc) warnx("ignoring extra arguments");

	if (logical) {
		do {
			char * pwd = getenv("PWD");
			if (!pwd) break;
			if (*pwd != '/') break;
			if (strstr(pwd, "/../") || strstr(pwd, "/./")) break;
			if (strlen(pwd) > 2 && !strcmp(pwd + strlen(pwd) - 2, "/.")) break;
			if (strlen(pwd) > 3 && !strcmp(pwd + strlen(pwd) - 3, "/..")) break;

			struct stat st_pwd, st_dot;
			if (stat(pwd, &st_pwd)) break;
			if (stat(".", &st_dot)) break;

			if (st_dot.st_ino != st_pwd.st_ino || st_dot.st_dev != st_pwd.st_dev) break;

			puts(pwd);
			return 0;

		} while (0);
	}

	char *wd = getcwd(NULL, 0);
	if (!wd) err(1, "getcwd");
	puts(wd);
	return 0;
}
