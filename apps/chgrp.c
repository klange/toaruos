/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 *
 * chgrp
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

int main(int argc, char * argv[]) {
	if (argc != 3) {
		fprintf(stderr, "usage: chgrp GID FILE\n");
		return 1;
	}

	int gid = atoi(argv[1]);

	if (chown(argv[2], getuid(), gid)) {
		fprintf(stderr, "chgrp: %s: %s\n", argv[2], strerror(errno));
		return 1;
	}
	return 0;
}
