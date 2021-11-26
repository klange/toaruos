/**
 * @brief Exceute "extended system function" syscalls.
 *
 * Most of these are deprecated, and the ones that are useful
 * to call manually are all behind other utilities.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013 K. Lange
 */
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include <sys/sysfunc.h>

int main(int argc, char ** argv) {
	if (argc < 2) return 1;
	int ret = sysfunc(atoi(argv[1]), &argv[2]);
	if (ret < 0) {
		fprintf(stderr, "%s: %s\n", argv[0], strerror(errno));
	}
	return ret;
}
