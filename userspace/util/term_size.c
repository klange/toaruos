/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013 Kevin Lange
 */
#include <stdio.h>
#include <sys/ioctl.h>
#include <termios.h>

int main(int argc, char * argv[]) {
	struct winsize w;
	int width, height;
	ioctl(0, TIOCGWINSZ, &w);
	width = w.ws_col;
	height = w.ws_row;
	printf("Terminal is %dx%d\n", width, height);
}
