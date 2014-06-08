/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013 Kevin Lange
 */
#include <stdio.h>

int main (int argc, char * argv) {
	printf("I'm going to move the cursor here >");
	fflush(stdout);
	printf("\033[6n");
	fflush(stdout);
	int x, y;
	scanf("\033[%d;%dR", &y, &x);
	printf("\n\nThe cursor was at %d, %d\n", x, y);
	printf("I will now put ◯ where the cursor was.\n");
	char derp[1];
	gets(derp);
	printf("\033[%d;%dH◯\n\n\n\n", y, x);

	printf("Done!\n");
	return 0;
}
