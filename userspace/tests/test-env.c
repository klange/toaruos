/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013 Kevin Lange
 */
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char * argv) {
	printf("hello world\n");
	char * term = getenv("TERM");
	if (term) {
		printf("TERM=%s\n", term);
	} else {
		printf("TERM is not set.\n");
	}
	return 0;
}
