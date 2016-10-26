/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2016 Kevin Lange
 */
#include <stdio.h>
#include <syscall.h>

int main(int argc, char * argv[]) {
	if (argc < 2) {
		fprintf(stderr, "Usage: %s <modulepath>\n", argv[0]);
		return 1;
	}
	return syscall_system_function(8, &argv[1]);
}
