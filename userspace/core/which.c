/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2014 Kevin Lange
 *
 * which
 *
 * Searches through PATH to find an executable.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define DEFAULT_PATH ".:/bin:/usr/bin"

int main(int argc, char * argv[]) {

	if (argc < 2) {
		return 1;
	}

	if (strstr(argv[1], "/")) {
		struct stat t;
		if (!stat(argv[1], &t)) {
			if ((t.st_mode & 0111)) {
				printf("%s\n", argv[1]);
				return 0;
			}
		}
	} else {
		char * file = argv[1];
		char * path = getenv("PATH");
		if (!path) {
			path = DEFAULT_PATH;
		}

		char * xpath = strdup(path);
		int found = 0;
		char * p, * tokens[10], * last;
		int i = 0;
		for ((p = strtok_r(xpath, ":", &last)); p; p = strtok_r(NULL, ":", &last)) {
			int r;
			struct stat stat_buf;
			char * exe = malloc(strlen(p) + strlen(file) + 2);
			strcpy(exe, p);
			strcat(exe, "/");
			strcat(exe, file);

			r = stat(exe, &stat_buf);
			if (r != 0) {
				continue;
			}
			if (!(stat_buf.st_mode & 0111)) {
				continue; /* XXX not technically correct; need to test perms */
			}
			printf("%s\n", exe);
			return 0;
		}
		free(xpath);
		return 1;
	}
	return 1;
}
