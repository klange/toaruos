/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013 Kevin Lange
 */
#include <stdio.h>
#include <sys/ioctl.h>

int main(int argc, char * argv) {
	struct winsize w;
	ioctl(0, TIOCGWINSZ, &w);

	printf("This terminal has %d rows and %d columns.\n", w.ws_col, w.ws_row);
	return 0;
}
