/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013 Kevin Lange
 */
/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * yes
 *
 * Continually prints its first argument, followed by a newline.
 */
#include <stdio.h>

int main(int argc, char * argv[]) {
	char * yes_string = "y";
	if (argc > 1) {
		yes_string = argv[1];
	}
	while (1) {
		printf("%s\n", yes_string);
	}
	return 0;
}
