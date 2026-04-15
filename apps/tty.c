/**
 * @brief tty - print terminal name
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char * argv[]) {
	if (!isatty(STDIN_FILENO)) {
		fprintf(stdout, "not a tty\n");
		return 1;
	}

	char buf[PATH_MAX];

	if (ttyname_r(STDIN_FILENO, buf, PATH_MAX) == -1) {
		perror(argv[0]);
		return 2;
	}

	fprintf(stdout, "%s\n", buf);

	return 0;
}
