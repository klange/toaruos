/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 */
#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>

static void print_cc(struct termios * t, const char * lbl, int val) {
	int c = t->c_cc[val];
	if (!c) {
		fprintf(stdout, "%s = <undef>; ", lbl);
	} else if (c < 32) {
		fprintf(stdout, "%s = ^%c; ", lbl, '@' + c);
	}
}

static void print_flag(struct termios * t, const char * lbl, int val) {
	int c = t->c_cflag & val;
	fprintf(stdout, "%s%s ", c ? "" : "-", lbl);
}

static void print_iflag(struct termios * t, const char * lbl, int val) {
	int c = t->c_iflag & val;
	fprintf(stdout, "%s%s ", c ? "" : "-", lbl);
}

static void print_oflag(struct termios * t, const char * lbl, int val) {
	int c = t->c_oflag & val;
	fprintf(stdout, "%s%s ", c ? "" : "-", lbl);
}

static void print_lflag(struct termios * t, const char * lbl, int val) {
	int c = t->c_lflag & val;
	fprintf(stdout, "%s%s ", c ? "" : "-", lbl);
}

static int show_settings(int all) {
	/* Size */
	struct winsize w;
	ioctl(STDERR_FILENO, TIOCGWINSZ, &w);
	fprintf(stdout, "rows %d; columns %d; ypixels %d; xpixels %d;\n", w.ws_row, w.ws_col, w.ws_ypixel, w.ws_xpixel);

	struct termios t;
	tcgetattr(STDERR_FILENO, &t);

	/* Keys */
	print_cc(&t, "intr", VINTR);
	print_cc(&t, "quit", VQUIT);
	print_cc(&t, "erase", VERASE);
	print_cc(&t, "kill", VKILL);
	print_cc(&t, "eof", VEOF);
	print_cc(&t, "eol", VEOL);
	fprintf(stdout, "\n");

	print_cc(&t, "start", VSTART);
	print_cc(&t, "stop", VSTOP);
	print_cc(&t, "susp", VSUSP);

	/* MIN, TIME */
	fprintf(stdout, "min = %d; time = %d;\n", t.c_cc[VMIN], t.c_cc[VTIME]);

	print_flag(&t, "parenb", PARENB);
	print_flag(&t, "parodd", PARODD);

	switch ((t.c_cflag & CSIZE)) {
		case CS5: fprintf(stdout, "cs5 "); break;
		case CS6: fprintf(stdout, "cs6 "); break;
		case CS7: fprintf(stdout, "cs7 "); break;
		case CS8: fprintf(stdout, "cs8 "); break;
	}

	print_flag(&t, "hupcl",  HUPCL);
	print_flag(&t, "cstopb", CSTOPB);
	print_flag(&t, "cread",  CREAD);
	print_flag(&t, "clocal", CLOCAL);
	fprintf(stdout, "\n");

	print_iflag(&t, "ignbrk", IGNBRK);
	print_iflag(&t, "brkint", BRKINT);
	print_iflag(&t, "ignpar", IGNPAR);
	print_iflag(&t, "parmrk", PARMRK);
	print_iflag(&t, "inpck",  INPCK);
	print_iflag(&t, "istrip", ISTRIP);
	print_iflag(&t, "inlcr",  INLCR);
	print_iflag(&t, "ixon",   IXON);
	print_iflag(&t, "ixany",  IXANY);
	print_iflag(&t, "ixoff",  IXOFF);
	fprintf(stdout, "\n");

	print_oflag(&t, "opost",  OPOST);
	print_oflag(&t, "onlcr",  ONLCR);
	print_oflag(&t, "ocrnl",  OCRNL);
	print_oflag(&t, "onocr",  ONOCR);
	print_oflag(&t, "onlret", ONLRET);
	print_oflag(&t, "ofill",  OFILL);
	print_oflag(&t, "ofdel",  OFDEL);

	switch ((t.c_oflag & CRDLY)) {
		case CR0: fprintf(stdout, "cr0 "); break;
		case CR1: fprintf(stdout, "cr1 "); break;
		case CR2: fprintf(stdout, "cr2 "); break;
		case CR3: fprintf(stdout, "cr3 "); break;
	}

	switch ((t.c_oflag & NLDLY)) {
		case NL0: fprintf(stdout, "nl0 "); break;
		case NL1: fprintf(stdout, "nl1 "); break;
	}

	switch ((t.c_oflag & TABDLY)) {
		case TAB0: fprintf(stdout, "tab0 "); break;
		case TAB1: fprintf(stdout, "tab1 "); break;
		case TAB2: fprintf(stdout, "tab2 "); break;
		case TAB3: fprintf(stdout, "tab3 "); break;
	}

	switch ((t.c_oflag & BSDLY)) {
		case BS0: fprintf(stdout, "bs0 "); break;
		case BS1: fprintf(stdout, "bs1 "); break;
	}

	switch ((t.c_oflag & FFDLY)) {
		case FF0: fprintf(stdout, "ff0 "); break;
		case FF1: fprintf(stdout, "ff1 "); break;
	}

	switch ((t.c_oflag & VTDLY)) {
		case VT0: fprintf(stdout, "vt0\n"); break;
		case VT1: fprintf(stdout, "vt1\n"); break;
	}

	print_lflag(&t, "isig",   ISIG);
	print_lflag(&t, "icanon", ICANON);
	print_lflag(&t, "iexten", IEXTEN);
	print_lflag(&t, "echo",   ECHO);
	print_lflag(&t, "echoe",  ECHOE);
	print_lflag(&t, "echok",  ECHOK);
	print_lflag(&t, "echonl", ECHONL);
	print_lflag(&t, "noflsh", NOFLSH);
	print_lflag(&t, "tostop", TOSTOP);
	fprintf(stdout, "\n");

	return 0;
}

int main(int argc, char * argv[]) {
	if (argc < 2) {
		return show_settings(0);
	}

	/* TODO: Process arguments */
	fprintf(stderr, "%s: argument processing not yet available\n", argv[0]);
	return 1;
}
