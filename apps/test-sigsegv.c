/**
 * @brief Test tool for producing segmentation faults.
 *
 * Useful for testing the debugger.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021 K. Lange
 */
#include <stdio.h>
#include <string.h>

int main(int argc, char * argv[]) {
	if (argc > 1) {
		fprintf(stderr, "%s\n", (char*)(uintptr_t)strtoul(argv[1],NULL,0));
	} else {
		*(volatile int*)0x12345 = 42;
	}
	return 0;
}
