/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014 Kevin Lange
 */
/*
 * Prepend text in front of output lines
 */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#define LINE_SIZE 4096

int main(int argc, char ** argv) {
	if (argc < 2) {
		fprintf(stderr, "usage: %s text-to-prepend\n", argv[0]);
		return 1;
	}

	char * needle = argv[1];
	char buf[LINE_SIZE];

	while (fgets(buf, LINE_SIZE, stdin)) {
		fprintf(stdout, "%s: %s", argv[1], buf);
	}

	return 0;
}

/*
 * vim:tabstop=4
 * vim:noexpandtab
 * vim:shiftwidth=4
 */
