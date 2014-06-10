/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014 Kevin Lange
 *
 * mount
 *
 * Mount a filesystem.
 */

#include <stdio.h>

/* Probably should go somewhere */
extern int mount(char* src,char* tgt,char* typ,unsigned long,void*);

int main(int argc, char ** argv) {
	if (argc < 4) {
		fprintf(stderr, "Usage: %s type device mountpoint\n", argv[0]);
		return 1;
	}

	if (mount(argv[2], argv[3], argv[1], 0, NULL) < 0) {
		perror("mount");
		return 1;
	}

	return 0;
}
