/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013 Kevin Lange
 */
/*
 * Argument processing test tool.
 *
 * Usage:
 *    Run as ./argv-tester herp derp etc.
 *    Evaluate the arguments as they are printed to verify
 *    they match the input that you provided.
 */
#include <stdio.h>

int main(int argc, char * argv[]) {
	printf("argc = %d\n", argc);
	for (int i = 0; i < argc; ++i) {
		printf("%p argv[%d]= %s\n", argv[i], i, argv[i]);
	}
	printf("continuing until I hit a 0\n");
	int i = argc;
	while (1) {
		printf("argv[%d] = 0x%x\n", i, argv[i]);
		if (argv[i] == 0) {
			break;
		}
		i++;
	}
	return 0;
}
