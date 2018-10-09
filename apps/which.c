/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2014 K. Lange
 *
 * which - Figure out which binary will be used
 *
 * Searches through $PATH to find a matching binary, just like
 * how execp* family does it. (Except does our execp actually
 * bother checking permissions? Look into this...)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define DEFAULT_PATH "/bin:/usr/bin"

int main(int argc, char * argv[]) {

	int ret_val = 0;
	int i = 1;
	int print_all = 0;

	if (i < argc && !strcmp(argv[i],"-a")) {
		print_all = 1;
		i++;
	}

	if (i == argc) {
		return 1;
	}

	for (; i < argc; ++i) {

		if (strstr(argv[i], "/")) {
			struct stat t;
			if (!stat(argv[i], &t)) {
				if ((t.st_mode & 0111)) {
					printf("%s\n", argv[1]);
				}
			}
		} else {
			char * file = argv[i];
			char * path = getenv("PATH");
			if (!path) {
				path = DEFAULT_PATH;
			}

			char * xpath = strdup(path);
			char * p, * last;
			int found = 0;
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
				found = 1;
				printf("%s\n", exe);
				if (print_all) continue;
				break;
			}
			free(xpath);
			if (!found) ret_val = 1;
		}
	}
	return ret_val;
}
