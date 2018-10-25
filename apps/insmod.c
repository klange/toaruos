/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2016 K. Lange
 *
 * insmod - Load kernel module
 *
 */
#include <stdio.h>
#include <syscall.h>

int main(int argc, char * argv[]) {
	if (argc < 2) {
		fprintf(stderr, "Usage: %s <modulepath>\n", argv[0]);
		return 1;
	}
	int ret = 0;
	for (int i = 1; i < argc; ++i) {
		int status = syscall_system_function(8, &argv[i]);
		if (status) {
			fprintf(stderr, "%s: %s: kernel returned %d\n", argv[0], argv[i], status);
			ret = 1;
		}
	}
	return ret;
}
