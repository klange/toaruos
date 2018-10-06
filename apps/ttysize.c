/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 *
 * ttysize - Magically divine terminal size
 *
 * This is called by getty to determine the size of foreign
 * terminals, such as ones attached over serial.
 *
 * It works by placing the cursor in the lower right of the
 * screen and requesting its position. Note that typing things
 * while this happens can cause problems. Maybe we can flush
 * stdin before doing this to try to avoid any conflicting data?
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/fswait.h>

static struct termios old;

static void set_unbuffered() {
	tcgetattr(fileno(stdin), &old);
	struct termios new = old;
	new.c_lflag &= (~ICANON & ~ECHO);
	tcsetattr(fileno(stdin), TCSAFLUSH, &new);
}

static void set_buffered() {
	tcsetattr(fileno(stdin), TCSAFLUSH, &old);
}

static int getc_timeout(FILE * f, int timeout) {
	int fds[1] = {fileno(f)};
	int index = fswait2(1,fds,timeout);
	if (index == 0) {
		return fgetc(f);
	} else {
		return -1;
	}
}

static void divine_size(int * width, int * height) {
	set_unbuffered();

	*width = 80;
	*height = 24;

	fprintf(stderr, "\033[s\033[1000;1000H\033[6n\033[u");
	fflush(stderr);

	char buf[1024] = {0};
	size_t i = 0;
	while (1) {
		char c = getc_timeout(stdin, 200);
		if (c == 'R') break;
		if (c == -1) goto _done;
		if (c == '\033') continue;
		if (c == '[') continue;
		buf[i++] = c;
	}

	char * s = strstr(buf, ";");
	if (s) {
		*(s++) = '\0';

		*height = atoi(buf);
		*width = atoi(s);
	}

_done:
	fflush(stderr);
	set_buffered();
}

int main(int argc, char * argv[]) {
	int width, height;
	int opt;
	int quiet = 0;

	while ((opt = getopt(argc, argv, "q")) != -1) {
		switch (opt) {
			case 'q':
				quiet = 1;
				break;
		}
	}

	if (optind + 2 == argc) {
		width = atoi(argv[optind]);
		height = atoi(argv[optind+1]);
	} else {
		divine_size(&width, &height);
	}

	struct winsize w;
	w.ws_col = width;
	w.ws_row = height;
	w.ws_xpixel = 0;
	w.ws_ypixel = 0;
	ioctl(0, TIOCSWINSZ, &w);

	if (!quiet) {
		fprintf(stderr, "%dx%d\n", width, height);
	}

	return 0;
}
