/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>

static int hide_defaults = 1;
static int printed = 0;

static void print_cc(struct termios * t, const char * lbl, int val, int def) {
	int c = t->c_cc[val];
	if (hide_defaults && c == def) return;
	if (!c) {
		fprintf(stdout, "%s = <undef>; ", lbl);
	} else if (c < 32) {
		fprintf(stdout, "%s = ^%c; ", lbl, '@' + c);
	} else {
		fprintf(stdout, "%s = %c; ", lbl, c);
	}
	printed = 1;
}

static void print_(int flags, const char * lbl, int val, int def) {
	int c = !!(flags & val);
	if (!hide_defaults || c != def) {
		fprintf(stdout, "%s%s ", c ? "" : "-", lbl);
		printed = 1;
	}
}

#define print_cflag(lbl,val,def) print_(t.c_cflag, lbl, val, def)
#define print_iflag(lbl,val,def) print_(t.c_iflag, lbl, val, def)
#define print_oflag(lbl,val,def) print_(t.c_oflag, lbl, val, def)
#define print_lflag(lbl,val,def) print_(t.c_lflag, lbl, val, def)

static int show_settings(int all) {
	/* Size */
	struct winsize w;
	ioctl(STDERR_FILENO, TIOCGWINSZ, &w);
	fprintf(stdout, "rows %d; columns %d; ypixels %d; xpixels %d;\n", w.ws_row, w.ws_col, w.ws_ypixel, w.ws_xpixel);
	printed = 0;

	struct termios t;
	tcgetattr(STDERR_FILENO, &t);

	/* Keys */
	print_cc(&t, "intr",  VINTR,  3);
	print_cc(&t, "quit",  VQUIT,  28);
	print_cc(&t, "erase", VERASE, '\b');
	print_cc(&t, "kill",  VKILL,  21);
	print_cc(&t, "eof",   VEOF,   4);
	print_cc(&t, "eol",   VEOL,   0);
	if (printed) { fprintf(stdout, "\n"); printed = 0; }

	print_cc(&t, "start", VSTART, 17);
	print_cc(&t, "stop",  VSTOP,  19);
	print_cc(&t, "susp",  VSUSP,  26);

	/* MIN, TIME */
	if (!hide_defaults || t.c_cc[VMIN]  != 1) { fprintf(stdout, "min = %d; ",  t.c_cc[VMIN]);  printed = 1; }
	if (!hide_defaults || t.c_cc[VTIME] != 0) { fprintf(stdout, "time = %d; ", t.c_cc[VTIME]); printed = 1; }
	if (printed) { fprintf(stdout, "\n"); printed = 0; }

	switch ((t.c_cflag & CSIZE)) {
		case CS5: fprintf(stdout, "cs5 "); printed = 1; break;
		case CS6: fprintf(stdout, "cs6 "); printed = 1; break;
		case CS7: fprintf(stdout, "cs7 "); printed = 1; break;
		case CS8: if (!hide_defaults) { fprintf(stdout, "cs8 "); printed = 1; } break;
	}

	print_cflag("cstopb", CSTOPB, 0);
	print_cflag("cread",  CREAD,  1);
	print_cflag("parenb", PARENB, 0);
	print_cflag("parodd", PARODD, 0);
	print_cflag("hupcl",  HUPCL,  0);
	print_cflag("clocal", CLOCAL, 0);
	if (printed) { fprintf(stdout, "\n"); printed = 0; }

	print_iflag("brkint", BRKINT, 1);
	print_iflag("icrnl",  ICRNL,  1);
	print_iflag("ignbrk", IGNBRK, 0);
	print_iflag("igncr",  IGNCR,  0);
	print_iflag("ignpar", IGNPAR, 0);
	print_iflag("inlcr",  INLCR,  0);
	print_iflag("inpck",  INPCK,  0);
	print_iflag("istrip", ISTRIP, 0);
	print_iflag("ixany",  IXANY,  0);
	print_iflag("ixoff",  IXOFF,  0);
	print_iflag("ixon",   IXON,   0);
	print_iflag("parmrk", PARMRK, 0);
	if (printed) { fprintf(stdout, "\n"); printed = 0; }

	print_oflag("opost",  OPOST,  1);
	print_oflag("olcuc",  OLCUC,  0);
	print_oflag("onlcr",  ONLCR,  1);
	print_oflag("ocrnl",  OCRNL,  0);
	print_oflag("onocr",  ONOCR,  0);
	print_oflag("onlret", ONLRET, 0);
	print_oflag("ofill",  OFILL,  0);
	print_oflag("ofdel",  OFDEL,  0);

	switch ((t.c_oflag & CRDLY)) {
		case CR0: if (!hide_defaults) { fprintf(stdout, "cr0 "); printed = 1; } break;
		case CR1: fprintf(stdout, "cr1 "); printed = 1; break;
		case CR2: fprintf(stdout, "cr2 "); printed = 1; break;
		case CR3: fprintf(stdout, "cr3 "); printed = 1; break;
	}

	switch ((t.c_oflag & NLDLY)) {
		case NL0: if (!hide_defaults) { fprintf(stdout, "nl0 "); printed = 1; } break;
		case NL1: fprintf(stdout, "nl1 "); printed = 1; break;
	}

	switch ((t.c_oflag & TABDLY)) {
		case TAB0: if (!hide_defaults) { fprintf(stdout, "tab0 "); printed = 1; } break;
		case TAB1: fprintf(stdout, "tab1 "); printed = 1; break;
		case TAB2: fprintf(stdout, "tab2 "); printed = 1; break;
		case TAB3: fprintf(stdout, "tab3 "); printed = 1; break;
	}

	switch ((t.c_oflag & BSDLY)) {
		case BS0: if (!hide_defaults) { fprintf(stdout, "bs0 "); printed = 1; } break;
		case BS1: fprintf(stdout, "bs1 "); printed = 1; break;
	}

	switch ((t.c_oflag & FFDLY)) {
		case FF0: if (!hide_defaults) { fprintf(stdout, "ff0 "); printed = 1; } break;
		case FF1: fprintf(stdout, "ff1 "); printed = 1; break;
	}

	switch ((t.c_oflag & VTDLY)) {
		case VT0: if (!hide_defaults) { fprintf(stdout, "vt0"); printed = 1; } break;
		case VT1: fprintf(stdout, "vt1"); printed = 1; break;
	}
	if (printed) { fprintf(stdout, "\n"); printed = 0; }

	print_lflag("isig",   ISIG,   1);
	print_lflag("icanon", ICANON, 1);
	print_lflag("xcase",  XCASE,  0);
	print_lflag("echo",   ECHO,   1);
	print_lflag("echoe",  ECHOE,  1);
	print_lflag("echok",  ECHOK,  1);
	print_lflag("echonl", ECHONL, 0);
	print_lflag("noflsh", NOFLSH, 0);
	print_lflag("tostop", TOSTOP, 0);
	print_lflag("iexten", IEXTEN, 1);
	if (printed) { fprintf(stdout, "\n"); printed = 0; }

	return 0;
}

int main(int argc, char * argv[]) {

	int i = 1;

	if (i < argc && !strcmp(argv[i], "-a")) {
		hide_defaults = 0;
		i++;
	}

	if (i == argc) {
		return show_settings(0);
	}

	/* TODO: Process arguments */
	fprintf(stderr, "%s: argument processing not yet available\n", argv[0]);
	return 1;
}
