/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 *
 * basename - print file name
 */
#include <stdio.h>
#include <string.h>

int main(int argc, char * argv[]) {
	if (argc < 2) {
		fprintf(stderr, "%s: expected argument\n", argv[0]);
		return 1;
	}

	char * s = argv[1];
	char * c = NULL;
	do {
		while (*s == '/') {
			*s = '\0'; s++;
			if (!*s) goto _done;
		}
		c = s;
		s = strchr(c,'/');
	} while (s);

_done:
	if (!c) {
		/* Special case */
		fprintf(stdout, "/\n");
		return 0;
	}

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
