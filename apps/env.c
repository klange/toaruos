/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2018 K. Lange
 *
 * env - Print or set environment
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

extern int _environ_size;

int main(int argc, char ** argv) {
	int start = 1;

	if (start < argc && !strcmp(argv[start],"-i")) {
		for (int i = 0; i < _environ_size; ++i) {
			environ[i] = NULL;
		}
		start++;
	}

	for (; start < argc; ++start) {
		if (!strchr(argv[start],'=')) {
			break;
		} else {
			putenv(argv[start]);
		}
	}

	if (start < argc) {
		/* Execute command */
		if (execvp(argv[start], &argv[start])) {
			fprintf(stderr, "%s: %s: %s\n", argv[0], argv[start], strerror(errno));
		}
	} else {
		char ** env = environ;

		while (*env) {
			printf("%s\n", *env);
			env++;
		}
	}

	return 0;
}
