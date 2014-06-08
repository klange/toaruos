/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013 Kevin Lange
 */
#include <stdio.h>
#include <stdint.h>

int main(int argc, char * argv[]) {
	printf("Shutting down...\n");
	/* Nothing to actually do for shutdown, sadly */
	__asm__ __volatile__ ("outw %1, %0" : : "dN" ((uint16_t)0xB004), "a" ((uint16_t)0x2000));
	return 0;
}
