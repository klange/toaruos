/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013 Kevin Lange
 */
/*
 * touch
 *
 * Creates a file or updates its last-modified date.
 * (in theory)
 */
#include <stdio.h>
#include <string.h>

int main(int argc, char * argv[]) {
	if (argc < 2) {
		fprintf(stderr, "%s: argument expected\n", argv[0]);
		return 1;
	}

	FILE * f = fopen(argv[1], "a");
	fclose(f);

	return 0;
}
