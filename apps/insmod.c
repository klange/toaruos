/**
 * @brief insmod - Load kernel module
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2016 K. Lange
 */
#include <stdio.h>
#include <errno.h>
#include <sys/sysfunc.h>

int main(int argc, char * argv[]) {
	if (argc < 2) {
		fprintf(stderr, "Usage: %s <modulepath> [ARGS...]\n", argv[0]);
		return 1;
	}
	int status = sysfunc(TOARU_SYS_FUNC_INSMOD, &argv[1]);
	if (status != 0) {
		fprintf(stderr, "%s: %s: %s\n", argv[0], argv[1], strerror(errno));
		return status;
	}
	return 0;
}
