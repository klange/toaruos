/**
 * @brief sleep - Do nothing, efficiently.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013 K. Lange
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char ** argv) {
	if (argc < 2) {
		fprintf(stderr, "%s: missing operand\n", argv[0]);
		return 1;
	}

	char * arg = strdup(argv[1]);

	float time = atof(arg);
	unsigned int seconds = (unsigned int)time;
	unsigned int subsecs = (unsigned int)((time - (float)seconds) * 100);

	useconds_t usecs = (seconds * 1000L + subsecs) * 1000L;

	if (usleep(usecs) < 0) {
		perror(argv[0]);
		return 1;
	}

	return 0;
}

