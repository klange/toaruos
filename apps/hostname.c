/* vim: ts=4 sw=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2018 K. Lange
 *
 * hostname
 *
 * Prints or sets the system hostname.
 */
#include <stdio.h>
#include <unistd.h>
#include <string.h>

int main(int argc, char * argv[]) {
	if ((argc > 1 && argv[1][0] == '-') || (argc < 2)) {
		char tmp[256] = {0};
		gethostname(tmp, 255);
		printf("%s\n", tmp);
		return 0;
	} else {
		if (getuid() != 0) {
			fprintf(stderr,"Must be root to set hostname.\n");
			return 1;
		} else {
			sethostname(argv[1], strlen(argv[1]));
			FILE * file = fopen("/etc/hostname", "w");
			if (!file) {
				return 1;
			} else {
				fprintf(file, "%s\n", argv[1]);
				fclose(file);
				return 0;
			}
		}
	}
	return 0;
}
