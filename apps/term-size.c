/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013 K. Lange
 *
 * term-size - Print the size of the tty
 *
 * This is mostly redundant with ttysize, and should probably
 * be replaced with setting COLUMNS and LINES in the shell,
 * like bash does, but it's left here for historical reasons
 */
#include <stdio.h>
#include <sys/ioctl.h>
#include <termios.h>

int main(int argc, char * argv[]) {
	struct winsize w;
	ioctl(0, TIOCGWINSZ, &w);
	printf("Terminal is %dx%d (%d px x %d px)\n", w.ws_col, w.ws_row, w.ws_xpixel, w.ws_ypixel);
	return 0;
}
