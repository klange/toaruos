/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013 Kevin Lange
 */
#include <stdio.h>

int main(int argc, char ** argv) {
	printf("Exploring the stack...\n");
	unsigned int x = 0;
	unsigned int nulls = 0;
	while (1) {
		if (!argv[x]) { ++nulls; }
		printf("argv[%.2d] = [%p] %s\n", x, argv[x], argv[x]);
		++x;
		if (nulls == 2) { break; }
	}
	printf("[ELF AuxV]\n");
	while (1) {
		printf("auxv[%.2d] = %.2d -> 0x%x\n", x, (unsigned int)argv[x], (unsigned int)argv[x+1]);
		if (argv[x] == 0) { break; } /* AT_NULL, technically, but that's 0 */
		x += 2;
	}
}
