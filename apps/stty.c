/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
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
	} else if (c == 0x7F) {
		fprintf(stdout, "%s = ^?; ", lbl);
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

static int set_char_(struct termios *t, const char * lbl, int val, const char * cmp, const char * arg, int * i) {
	if (!strcmp(cmp, lbl)) {
		if (!arg) {
			fprintf(stderr, "%s: expected argument\n", lbl);
			exit(1);
		}
		/* Parse arg */
		if (strlen(arg) == 1) {
			/* Assume raw character */
			t->c_cc[val] = *arg;
		} else if (*arg == '^') { /* ^c, etc. */
			int v = toupper(arg[1]);
			if (v == '?') { /* special case */
				t->c_cc[val] = 0x7F;
			} else {
				t->c_cc[val] = v - '@';
			}
		} else {
			/* Assume decimal for now */
			int v = atoi(arg);
			t->c_cc[val] = v;
		}
		(*i)++;

		return 1;
	}

	return 0;
}

#define set_char(lbl,val) if (set_char_(&t, lbl, val, argv[i], argv[i+1], &i)) { i += 2; continue; }

static int setunset_flag(tcflag_t * flag, const char * lbl, int val, const char * cmp) {
	if (*cmp == '-') {
		if (!strcmp(cmp+1, lbl)) {
			(*flag) = (*flag) & (~val);
			return 1;
		}
	} else {
		if (!strcmp(cmp, lbl)) {
			(*flag) = (*flag) | (val);
			return 1;
		}
	}
	return 0;
}

#define set_cflag(lbl,val) if (setunset_flag(&(t.c_cflag), lbl, val, argv[i])) { i++; continue; }
#define set_iflag(lbl,val) if (setunset_flag(&(t.c_iflag), lbl, val, argv[i])) { i++; continue; }
#define set_oflag(lbl,val) if (setunset_flag(&(t.c_oflag), lbl, val, argv[i])) { i++; continue; }
#define set_lflag(lbl,val) if (setunset_flag(&(t.c_lflag), lbl, val, argv[i])) { i++; continue; }

static int set_toggle_(tcflag_t * flag, const char * lbl, int base, int val, const char * cmp) {
	if (!strcmp(cmp, lbl)) {
		(*flag) = (*flag) & ~(base);
		(*flag) = (*flag) | (val);
		return 1;
	}
	return 0;
}

#define set_ctoggle(lbl,base,val) if (set_toggle_(&(t.c_cflag), lbl, base, val, argv[i])) { i++; continue; }
#define set_otoggle(lbl,base,val) if (set_toggle_(&(t.c_oflag), lbl, base, val, argv[i])) { i++; continue; }

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
	print_cc(&t, "erase", VERASE, 0x7F);
	print_cc(&t, "kill",  VKILL,  21);
	print_cc(&t, "eof",   VEOF,   4);
	print_cc(&t, "eol",   VEOL,   0);
	if (printed) { fprintf(stdout, "\n"); printed = 0; }

	print_cc(&t, "start", VSTART, 17);
	print_cc(&t, "stop",  VSTOP,  19);
	print_cc(&t, "susp",  VSUSP,  26);
	print_cc(&t, "lnext", VLNEXT, 22);
	print_cc(&t, "werase",VWERASE, 23);

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

static void show_size(void) {
	struct winsize w;
	ioctl(STDERR_FILENO, TIOCGWINSZ, &w);
	fprintf(stdout, "%d %d\n", w.ws_col, w.ws_row);
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

	struct termios t;
	tcgetattr(STDERR_FILENO, &t);

	while (i < argc) {

		if (!strcmp(argv[i], "sane")) {
			t.c_iflag = ICRNL | BRKINT;
			t.c_oflag = ONLCR | OPOST;
			t.c_lflag = ECHO | ECHOE | ECHOK | ICANON | ISIG | IEXTEN;
			t.c_cflag = CREAD | CS8;
			t.c_cc[VEOF]   =  4; /* ^D */
			t.c_cc[VEOL]   =  0; /* Not set */
			t.c_cc[VERASE] = 0x7F; /* ^? */
			t.c_cc[VINTR]  =  3; /* ^C */
			t.c_cc[VKILL]  = 21; /* ^U */
			t.c_cc[VMIN]   =  1;
			t.c_cc[VQUIT]  = 28; /* ^\ */
			t.c_cc[VSTART] = 17; /* ^Q */
			t.c_cc[VSTOP]  = 19; /* ^S */
			t.c_cc[VSUSP] = 26; /* ^Z */
			t.c_cc[VTIME]  =  0;
			t.c_cc[VLNEXT] = 22; /* ^V */
			t.c_cc[VWERASE] = 23; /* ^W */

			i++;
			continue;
		}

		if (!strcmp(argv[i], "size")) {
			show_size();

			i++;
			continue;
		}

		set_char("eof",   VEOF);
		set_char("eol",   VEOL);
		set_char("erase", VERASE);
		set_char("intr",  VINTR);
		set_char("kill",  VKILL);
		set_char("quit",  VQUIT);
		set_char("start", VSTART);
		set_char("stop",  VSTOP);
		set_char("susp",  VSUSP);
		set_char("lnext", VLNEXT);
		set_char("vwerase",VWERASE);

		set_cflag("parenb", PARENB);
		set_cflag("parodd", PARODD);
		set_cflag("hupcl",  HUPCL);
		set_cflag("hup",    HUPCL); /* alias */
		set_cflag("cstopb", CSTOPB);
		set_cflag("cread",  CREAD);
		set_cflag("clocal", CLOCAL);

		set_ctoggle("cs5", CSIZE, CS5);
		set_ctoggle("cs6", CSIZE, CS6);
		set_ctoggle("cs7", CSIZE, CS7);
		set_ctoggle("cs8", CSIZE, CS8);

		set_iflag("ignbrk", IGNBRK);
		set_iflag("brkint", BRKINT);
		set_iflag("ignpar", IGNPAR);
		set_iflag("parmrk", PARMRK);
		set_iflag("inpck",  INPCK);
		set_iflag("istrip", ISTRIP);
		set_iflag("inlcr",  INLCR);
		set_iflag("igncr",  IGNCR);
		set_iflag("icrnl",  ICRNL);
		set_iflag("ixon",   IXON);
		set_iflag("ixany",  IXANY);
		set_iflag("ixoff",  IXOFF);

		set_oflag("olcuc",  OLCUC);
		set_oflag("opost",  OPOST);
		set_oflag("onlcr",  ONLCR);
		set_oflag("ocrnl",  OCRNL);
		set_oflag("onocr",  ONOCR);
		set_oflag("onlret", ONLRET);
		set_oflag("ofill",  OFILL);
		set_oflag("ofdel",  OFDEL);

		set_otoggle("cr0", CRDLY, CR0);
		set_otoggle("cr1", CRDLY, CR1);
		set_otoggle("cr2", CRDLY, CR2);
		set_otoggle("cr3", CRDLY, CR3);

		set_otoggle("nl0", NLDLY, NL0);
		set_otoggle("nl1", NLDLY, NL1);

		set_otoggle("tab0", TABDLY, TAB0);
		set_otoggle("tab1", TABDLY, TAB1);
		set_otoggle("tab2", TABDLY, TAB2);
		set_otoggle("tab3", TABDLY, TAB3);

		set_otoggle("bs0", BSDLY, BS0);
		set_otoggle("bs1", BSDLY, BS1);

		set_otoggle("ff0", FFDLY, FF0);
		set_otoggle("ff1", FFDLY, FF1);

		set_otoggle("vt0", VTDLY, VT0);
		set_otoggle("vt1", VTDLY, VT1);

		set_lflag("isig",   ISIG);
		set_lflag("icanon", ICANON);
		set_lflag("iexten", IEXTEN);
		set_lflag("echo",   ECHO);
		set_lflag("echoe",  ECHOE);
		set_lflag("echok",  ECHOK);
		set_lflag("echonl", ECHONL);
		set_lflag("noflsh", NOFLSH);
		set_lflag("tostop", TOSTOP);

		fprintf(stderr, "%s: invalid argument '%s'\n", argv[0], argv[i]);
		return 1;
	}

	tcsetattr(STDERR_FILENO, TCSAFLUSH, &t);
	return 0;
}
