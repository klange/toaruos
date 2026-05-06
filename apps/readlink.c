/**
 * @brief Examine symlinks and print the path they point to.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2015 Mike Gerow
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#define MAX_LINK_SIZE 4096

int main(int argc, char * argv[]) {
	if (argc != 2) return fprintf(stderr, "usage: %s LINK\n", argv[0]), 2;
	char * name = argv[1];

	char buf[MAX_LINK_SIZE + 1];
	ssize_t len = readlink(name, buf, sizeof(buf) - 1);
	if (len < 0) return fprintf(stderr, "%s: %s: %s\n", argv[0], name, strerror(errno)), 1;
	buf[len] = '\0';
	puts(buf);
	return 0;
}
