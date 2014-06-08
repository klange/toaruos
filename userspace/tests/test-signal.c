/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013 Kevin Lange
 */
#include <stdio.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <signal.h>

void sig_winch(int signum) {
	struct winsize w;
	int width, height;
	ioctl(0, TIOCGWINSZ, &w);
	width = w.ws_col;
	height = w.ws_row;
	printf("Terminal is %dx%d\n", width, height);
}

int main(int argc, char * argv[]) {
	signal(SIGWINCH, sig_winch);
	while (1) {
		int c = fgetc(stdin);
		if (c == 'q') {
			break;
		}
	}
	return 0;
}
