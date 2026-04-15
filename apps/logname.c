/**
 * @brief Display the user's name, as returned by getlogin()
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018-2026 K. Lange
 */
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

int main(int argc, char ** argv) {
	char buf[NAME_MAX];

	if (getlogin_r(buf, NAME_MAX) == -1) {
		fprintf(stderr, "%s: failed to determine login name: %s\n", argv[0], strerror(errno));
		return 1;
	}

	fprintf(stdout, "%s\n", buf);
	return 0;
}

