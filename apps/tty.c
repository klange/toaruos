/**
 * @brief tty - print terminal name
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 */
#include <stdio.h>
#include <unistd.h>

int main(int argc, char * argv[]) {
	if (!isatty(STDIN_FILENO)) {
		fprintf(stdout, "not a tty\n");
		return 1;
	}
	fprintf(stdout,"%s\n",ttyname(STDIN_FILENO));
	return 0;
}
