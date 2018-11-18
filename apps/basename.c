/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 *
 * basename - print file name
 */
#include <stdio.h>
#include <string.h>
#include <libgen.h>

int main(int argc, char * argv[]) {
	if (argc < 2) {
		fprintf(stderr, "%s: expected argument\n", argv[0]);
		return 1;
	}

	char * c = basename(argv[1]);

	if (argc > 2) {
		char * suffix = argv[2];
		char * found = strstr(c + strlen(c) - strlen(suffix), suffix);
		if (found && (found - c == (int)(strlen(c)-strlen(suffix)))) {
			*found = '\0';
		}
	}

	fprintf(stdout, "%s\n", c);
	return 0;
}
