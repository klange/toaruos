/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2018 K. Lange
 *
 * rm
 *
 * unlink a file
 * (in theory)
 *
 * TODO: Support recursive, directory removal, etc.
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

int main(int argc, char * argv[]) {
	if (argc < 2) {
		fprintf(stderr, "usage: %s FILE...\n", argv[0]);
		return 1;
	}

	int ret = 0;

	for (int i = 1; i < argc; ++i) {
		if (unlink(argv[i]) < 0) {
			fprintf(stderr, "%s: %s: %s\n", argv[0], argv[i], strerror(errno));
			ret = 1;
		}
	}

	return ret;
}
