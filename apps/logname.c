/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 *
 * logname - Effectively the same as whoami, but for compliance
 *           with POSIX, this uses getlogin().
 */
#include <unistd.h>
#include <stdio.h>

int main(int argc, char ** argv) {
	char * name = getlogin();
	if (!name) {
		fprintf(stderr, "%s: failed to determine login name\n", argv[0]);
		return 1;
	}

	fprintf(stdout, "%s\n", name);
	return 0;
}

