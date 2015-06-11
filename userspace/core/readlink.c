/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2015 Mike Gerow
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define MAX_LINK_SIZE 4096

static const char usage[] =
"Usage: %s LINK\n";

int main(int argc, char * argv[]) {
	if (argc != 2) {
		fprintf(stderr, usage, argv[0]);
		exit(EXIT_FAILURE);
	}
	char * name = argv[1];

	char buf[MAX_LINK_SIZE];
	if (readlink(name, buf, sizeof(buf)) < 0) {
		perror("link");
		exit(EXIT_FAILURE);
	}
	fprintf(stdout, "%s\n", buf);
	exit(EXIT_SUCCESS);
}
