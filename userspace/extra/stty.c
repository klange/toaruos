/*
 * This is the Minix implementation of stty.
 *
 * Author: Andy Tanenbaum
 * Adapted to POSIX 1003.2 by: Philip Homburg
 *
 * Copyright (c) 1987,1997, 2006, Vrije Universiteit, Amsterdam, The
 * Netherlands All rights reserved. Redistribution and use of the MINIX 3
 * operating system in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.  Redistributions in binary
 * form must reproduce the above copyright notice, this list of conditions and
 * the following disclaimer in the documentation and/or other materials
 * provided with the distribution.  Neither the name of the Vrije Universiteit
 * nor the names of the software authors or contributors may be used to
 * endorse or promote products derived from this software without specific
 * prior written permission.  Any deviations from these conditions require
 * written permission from the copyright holder in advance
 */

#ifdef __minix_vmd
#define _MINIX_SOURCE
#endif

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#ifdef __minix
#include <sys/types.h>
#include <sys/ioctl.h>
#endif
#ifdef __NBSD_LIBC
#include <unistd.h>
#endif

/* Default settings, the Minix ones are defined in <termios.h> */

#ifndef TCTRL_DEF
#define TCTRL_DEF	(PARENB | CREAD | CS7)
#endif

#ifndef TSPEED_DEF
#define TSPEED_DEF	B1200
#endif

#ifndef TINPUT_DEF
#define TINPUT_DEF	(BRKINT | IGNPAR | ISTRIP | ICRNL)
#endif

#ifndef TOUTPUT_DEF
#define TOUTPUT_DEF	OPOST
#endif

#ifndef TLOCAL_DEF
#define TLOCAL_DEF	(ISIG | IEXTEN | ICANON | ECHO | ECHOE)
#endif

#ifndef TEOF_DEF
#define TEOF_DEF	'\4'	/* ^D */
#endif
#ifndef TEOL_DEF
#ifdef _POSIX_VDISABLE
#define TEOL_DEF	_POSIX_VDISABLE
#else
#define TEOL_DEF	'\n'
#endif
#endif
#ifndef TERASE_DEF
#define TERASE_DEF	'\10'	/* ^H */
#endif
#ifndef TINTR_DEF
#define TINTR_DEF	'\177'	/* ^? */
#endif
#ifndef TKILL_DEF
#define TKILL_DEF	'\25'	/* ^U */
#endif
#ifndef TQUIT_DEF
#define TQUIT_DEF	'\34'	/* ^\ */
#endif
#ifndef TSUSP_DEF
#define TSUSP_DEF	'\32'	/* ^Z */
#endif
#ifndef TSTART_DEF
#define TSTART_DEF	'\21'	/* ^Q */
#endif
#ifndef TSTOP_DEF
#define TSTOP_DEF	'\23'	/* ^S */
#endif
#ifndef TMIN_DEF
#define TMIN_DEF	1
#endif
#ifndef TTIME_DEF
#define TTIME_DEF	0
#endif



char *prog_name;
struct termios termios;
int column= 0, max_column=80;		/* Assume 80 character terminals. */
#ifdef __minix
struct winsize winsize;
#endif

#define PROTO(a) a

int main PROTO(( int argc, char **argv ));
void report PROTO(( int flags ));
int option PROTO(( char *opt, char *next ));
int match PROTO(( char *s1, char *s2 ));
void prctl PROTO(( int c ));
speed_t long2speed PROTO(( long num ));
long speed2long PROTO(( unsigned long speed ));
void print_flags PROTO(( unsigned long flags, unsigned long flag,
				unsigned long def, char *string, int all ));
void output PROTO(( char *s ));
void do_print_char PROTO(( unsigned chr, unsigned def, char *name, int all ));
void do_print_num PROTO(( unsigned num, unsigned def, char *name, int all ));
void set_saved_settings PROTO(( char *opt ));
void set_control PROTO(( int option, char *value ));
void set_min_tim PROTO(( int option, char *value ));

#define print_char(c,d,n,a) (do_print_char((unsigned)(c),(unsigned)(d),(n),(a)))
#define print_num(m,d,n,a) (do_print_num((unsigned)(m),(unsigned)(d),(n),(a)))

int main(argc, argv)
int argc;
char *argv[];
{
  int flags, k;

  prog_name= argv[0];
  flags= 0;

  /* Stty with no arguments just reports on current status. */
  if (tcgetattr(0, &termios) == -1)
  {
	fprintf(stderr, "%s: can't read ioctl parameters from stdin: %s\n",
		prog_name, strerror(errno));
	exit(1);
  }
#ifdef __minix
  if (ioctl(0, TIOCGWINSZ, &winsize) == -1)
  {
	fprintf(stderr, "%s: can't get screen size from stdin: %s\n",
		prog_name, strerror(errno));
	exit(1);
  }
  if (winsize.ws_col != 0)
	max_column= winsize.ws_col;
#endif

  if (argc == 2)
  {
	if (!strcmp(argv[1], "-a"))
		flags |= 1;
	else if (!strcmp(argv[1], "-g"))
		flags |= 2;
  }
  if (argc == 1 || flags) {
	report(flags);
	exit(0);
  }

  /* Process the options specified. */
  for (k= 1; k < argc; k++)
	k += option(argv[k], k+1 < argc ? argv[k+1] : "");

#ifdef __minix
  if (ioctl(0, TIOCSWINSZ, &winsize) == -1)
  {
	fprintf(stderr, "%s: can't set screen size to stdin: %s\n",
		prog_name, strerror(errno));
	exit(1);
  }
#endif
  if (tcsetattr(0, TCSANOW, &termios) == -1)
  {
	fprintf(stderr, "%s: can't set terminal parameters to stdin: %s\n",
		prog_name, strerror(errno));
	exit(1);
  }
  exit(0);
}



void report(flags)
int flags;
{
	int i, all;
	tcflag_t c_cflag, c_iflag, c_oflag, c_lflag;
	char line[80];
	speed_t ispeed, ospeed;

	if (flags & 2)
	{	/* We have to write the termios structure in a encoded form
		 * to stdout.
		 */
		printf(":%x:%x:%x:%x:%x:%x", termios.c_iflag, termios.c_oflag,
			termios.c_cflag, termios.c_lflag, cfgetispeed(&termios),
			cfgetospeed(&termios));
		for (i= 0; i<NCCS; i++)
			printf(":%x", termios.c_cc[i]);
		printf(":\n");
		return;
	}

	all= !!flags;

	/* Start with the baud rate. */
	ispeed= cfgetispeed(&termios);
	ospeed= cfgetospeed(&termios);
	if (ispeed != ospeed)
	{
		sprintf(line, "ispeed %lu baud; ospeed %lu baud;", 
			speed2long(ispeed), speed2long(ospeed));
		output(line);
	}
	else if (all || ospeed != TSPEED_DEF)
	{
		sprintf(line, "speed %lu baud;", speed2long(ospeed));
		output(line);
	}

	/* The control modes. */

	c_cflag= termios.c_cflag;
	if (all || (c_cflag & CSIZE) != (TCTRL_DEF & CSIZE))
	{
		switch (c_cflag & CSIZE)
		{
		case CS5: output("cs5"); break;
		case CS6: output("cs6"); break;
		case CS7: output("cs7"); break;
		case CS8: output("cs8"); break;
		default: output("cs??"); break;
		}
	}
	print_flags(c_cflag, PARENB, TCTRL_DEF, "-parenb", all);
	print_flags(c_cflag, PARODD, TCTRL_DEF, "-parodd", all);
	print_flags(c_cflag, HUPCL, TCTRL_DEF, "-hupcl", all);
	print_flags(c_cflag, CSTOPB, TCTRL_DEF, "-cstopb", all);
	print_flags(c_cflag, CREAD, TCTRL_DEF, "-cread", all);
	print_flags(c_cflag, CLOCAL, TCTRL_DEF, "-clocal", all);
		
	if (all)
	{
		printf("\n");
		column= 0;
	}

	/* The input flags. */

	c_iflag= termios.c_iflag;

	print_flags(c_iflag, IGNBRK, TINPUT_DEF, "-ignbrk", all);
	print_flags(c_iflag, BRKINT, TINPUT_DEF, "-brkint", all);
	print_flags(c_iflag, IGNPAR, TINPUT_DEF, "-ignpar", all);
	print_flags(c_iflag, PARMRK, TINPUT_DEF, "-parmrk", all);
	print_flags(c_iflag, INPCK, TINPUT_DEF, "-inpck", all);
	print_flags(c_iflag, ISTRIP, TINPUT_DEF, "-istrip", all);
	print_flags(c_iflag, INLCR, TINPUT_DEF, "-inlcr", all);
	print_flags(c_iflag, IGNCR, TINPUT_DEF, "-igncr", all);
	print_flags(c_iflag, ICRNL, TINPUT_DEF, "-icrnl", all);
	print_flags(c_iflag, IXON, TINPUT_DEF, "-ixon", all);
	print_flags(c_iflag, IXOFF, TINPUT_DEF, "-ixoff", all);
	print_flags(c_iflag, IXANY, TINPUT_DEF, "-ixany", all);
	
	if (all)
	{
		printf("\n");
		column= 0;
	}

	/* The output flags. */

	c_oflag= termios.c_oflag;

	print_flags(c_oflag, OPOST, TOUTPUT_DEF, "-opost", all);
	print_flags(c_oflag, ONLCR, TOUTPUT_DEF, "-onlcr", all);
#ifdef __minix
	print_flags(c_oflag, XTABS, TOUTPUT_DEF, "-xtabs", all);
	print_flags(c_oflag, ONOEOT, TOUTPUT_DEF, "-onoeot", all);
#endif
	if (all)
	{
		printf("\n");
		column= 0;
	}

	/* The local flags. */

	c_lflag= termios.c_lflag;

	print_flags(c_lflag, ISIG, TLOCAL_DEF, "-isig", all);
	print_flags(c_lflag, ICANON, TLOCAL_DEF, "-icanon", all);
	print_flags(c_lflag, IEXTEN, TLOCAL_DEF, "-iexten", all);
	print_flags(c_lflag, ECHO, TLOCAL_DEF, "-echo", all);
	print_flags(c_lflag, ECHOE, TLOCAL_DEF, "-echoe", all);
	print_flags(c_lflag, ECHOK, TLOCAL_DEF, "-echok", all);
	print_flags(c_lflag, ECHONL, TLOCAL_DEF, "-echonl", all);
	print_flags(c_lflag, NOFLSH, TLOCAL_DEF, "-noflsh", all);
#ifdef TOSTOP
	print_flags(c_lflag, TOSTOP, TLOCAL_DEF, "-tostop", all);
#endif
#ifdef __minix
	print_flags(c_lflag, LFLUSHO, TLOCAL_DEF, "-lflusho", all);
#endif

	if (all)
	{
		printf("\n");
		column= 0;
	}

	/* The special control characters. */

	print_char(termios.c_cc[VEOF], TEOF_DEF, "eof", all);
	print_char(termios.c_cc[VEOL], TEOL_DEF, "eol", all);
	print_char(termios.c_cc[VERASE], TERASE_DEF, "erase", all);
	print_char(termios.c_cc[VINTR], TINTR_DEF, "intr", all);
	print_char(termios.c_cc[VKILL], TKILL_DEF, "kill", all);
	print_char(termios.c_cc[VQUIT], TQUIT_DEF, "quit", all);
	print_char(termios.c_cc[VSUSP], TSUSP_DEF, "susp", all);
	print_char(termios.c_cc[VSTART], TSTART_DEF, "start", all);
	print_char(termios.c_cc[VSTOP], TSTOP_DEF, "stop", all);
#ifdef __minix
	print_char(termios.c_cc[VREPRINT], TREPRINT_DEF, "rprnt", all);
	print_char(termios.c_cc[VLNEXT], TLNEXT_DEF, "lnext", all);
	print_char(termios.c_cc[VDISCARD], TDISCARD_DEF, "flush", all);
#endif
	print_num(termios.c_cc[VMIN], TMIN_DEF, "min", all);
	print_num(termios.c_cc[VTIME], TTIME_DEF, "time", all);
	if (all)
	{
		printf("\n");
		column= 0;
	}

#ifdef __minix
	/* Screen size */
	if (all || winsize.ws_row != 0 || winsize.ws_col != 0)
	{
		sprintf(line, "%d rows %d columns", winsize.ws_row, 
			winsize.ws_col);
		output(line);
	}

	if (all || winsize.ws_ypixel != 0 || winsize.ws_xpixel != 0)
	{
		sprintf(line, "%d ypixels %d xpixels", winsize.ws_ypixel, 
			winsize.ws_xpixel);
		output(line);
	}

	if (all)
	{
		printf("\n");
		column= 0;
	}
#endif

	if (column != 0)
	{
		printf("\n");
		column= 0;
	}
}

int option(opt, next)
char *opt, *next;
{
  char *check;
  speed_t speed;
  long num;

  /* The control options. */

  if (match(opt, "clocal")) {
	termios.c_cflag |= CLOCAL;
	return 0;
  }
  if (match(opt, "-clocal")) {
	termios.c_cflag &= ~CLOCAL;
	return 0;
  }

  if (match(opt, "cread")) {
	termios.c_cflag |= CREAD;
	return 0;
  }
  if (match(opt, "-cread")) {
	termios.c_cflag &= ~CREAD;
	return 0;
  }

  if (match(opt, "cs5")) {
	termios.c_cflag &= ~CSIZE;
	termios.c_cflag |= CS5;
	return 0;
  }
  if (match(opt, "cs6")) {
	termios.c_cflag &= ~CSIZE;
	termios.c_cflag |= CS6;
	return 0;
  }
  if (match(opt, "cs7")) {
	termios.c_cflag &= ~CSIZE;
	termios.c_cflag |= CS7;
	return 0;
  }
  if (match(opt, "cs8")) {
	termios.c_cflag &= ~CSIZE;
	termios.c_cflag |= CS8;
	return 0;
  }

  if (match(opt, "cstopb")) {
	termios.c_cflag |= CSTOPB;
	return 0;
  }
  if (match(opt, "-cstopb")) {
	termios.c_cflag &= ~CSTOPB;
	return 0;
  }

  if (match(opt, "hupcl") || match(opt, "hup")) {
	termios.c_cflag |= HUPCL;
	return 0;
  }
  if (match(opt, "-hupcl") || match(opt, "-hup")) {
	termios.c_cflag &= ~HUPCL;
	return 0;
  }

  if (match(opt, "parenb")) {
	termios.c_cflag |= PARENB;
	return 0;
  }
  if (match(opt, "-parenb")) {
	termios.c_cflag &= ~PARENB;
	return 0;
  }

  if (match(opt, "parodd")) {
	termios.c_cflag |= PARODD;
	return 0;
  }
  if (match(opt, "-parodd")) {
	termios.c_cflag &= ~PARODD;
	return 0;
  }

  num= strtol(opt, &check, 10);
  if (check[0] == '\0')
  {
	speed= long2speed(num);
	if (speed == (speed_t)-1)
	{
		fprintf(stderr, "%s: illegal speed: '%s'\n", prog_name, opt);
		return 0;
	}
	/* Speed OK */
	cfsetispeed(&termios, speed);
	cfsetospeed(&termios, speed);
	return 0;
  }

  if (match(opt, "ispeed")) {
	num= strtol(next, &check, 10);
	if (check != '\0')
	{
		speed= long2speed(num);
		if (speed == (speed_t)-1)
		{
			fprintf(stderr, "%s: illegal speed: '%s'\n", prog_name, 
									opt);
			return 1;
		}
		cfsetispeed(&termios, speed);
		return 1;
	}
	else
	{
		fprintf(stderr, "%s: invalid argument to ispeed: '%s'\n", 
			prog_name, next);
		return 1;
	}
  }

  if (match(opt, "ospeed")) {
	num= strtol(next, &check, 10);
	if (check != '\0')
	{
		speed= long2speed(num);
		if (speed == (speed_t)-1)
		{
			fprintf(stderr, "%s: illegal speed: '%s'\n", prog_name, 
									opt);
			return 1;
		}
		cfsetospeed(&termios, speed);
		return 1;
	}
	else
	{
		fprintf(stderr, "%s: invalid argument to ospeed: %s\n", 
			prog_name, next);
		return 1;
	}
  }

  /* Input modes. */

  if (match(opt, "brkint")) {
	termios.c_iflag |= BRKINT;
	return 0;
  }
  if (match(opt, "-brkint")) {
	termios.c_iflag &= ~BRKINT;
	return 0;
  }

  if (match(opt, "icrnl")) {
	termios.c_iflag |= ICRNL;
	return 0;
  }
  if (match(opt, "-icrnl")) {
	termios.c_iflag &= ~ICRNL;
	return 0;
  }

  if (match(opt, "ignbrk")) {
	termios.c_iflag |= IGNBRK;
	return 0;
  }
  if (match(opt, "-ignbrk")) {
	termios.c_iflag &= ~IGNBRK;
	return 0;
  }

  if (match(opt, "igncr")) {
	termios.c_iflag |= IGNCR;
	return 0;
  }
  if (match(opt, "-igncr")) {
	termios.c_iflag &= ~IGNCR;
	return 0;
  }

  if (match(opt, "ignpar")) {
	termios.c_iflag |= IGNPAR;
	return 0;
  }
  if (match(opt, "-ignpar")) {
	termios.c_iflag &= ~IGNPAR;
	return 0;
  }

  if (match(opt, "inlcr")) {
	termios.c_iflag |= INLCR;
	return 0;
  }
  if (match(opt, "-inlcr")) {
	termios.c_iflag &= ~INLCR;
	return 0;
  }

  if (match(opt, "inpck")) {
	termios.c_iflag |= INPCK;
	return 0;
  }
  if (match(opt, "-inpck")) {
	termios.c_iflag &= ~INPCK;
	return 0;
  }

  if (match(opt, "istrip")) {
	termios.c_iflag |= ISTRIP;
	return 0;
  }
  if (match(opt, "-istrip")) {
	termios.c_iflag &= ~ISTRIP;
	return 0;
  }

  if (match(opt, "ixoff")) {
	termios.c_iflag |= IXOFF;
	return 0;
  }
  if (match(opt, "-ixoff")) {
	termios.c_iflag &= ~IXOFF;
	return 0;
  }

  if (match(opt, "ixon")) {
	termios.c_iflag |= IXON;
	return 0;
  }
  if (match(opt, "-ixon")) {
	termios.c_iflag &= ~IXON;
	return 0;
  }

  if (match(opt, "parmrk")) {
	termios.c_iflag |= PARMRK;
	return 0;
  }
  if (match(opt, "-parmrk")) {
	termios.c_iflag &= ~PARMRK;
	return 0;
  }

  if (match(opt, "ixany")) {
	termios.c_iflag |= IXANY;
	return 0;
  }
  if (match(opt, "-ixany")) {
	termios.c_iflag &= ~IXANY;
	return 0;
  }

  /* Output modes. */

  if (match(opt, "opost")) {
	termios.c_oflag |= OPOST;
	return 0;
  }
  if (match(opt, "-opost")) {
	termios.c_oflag &= ~OPOST;
	return 0;
  }
  if (match(opt, "onlcr")) {
	termios.c_oflag |= ONLCR;
	return 0;
  }
  if (match(opt, "-onlcr")) {
	termios.c_oflag &= ~ONLCR;
	return 0;
  }
#ifdef __minix

  if (match(opt, "xtabs")) {
	termios.c_oflag |= XTABS;
	return 0;
  }
  if (match(opt, "-xtabs")) {
	termios.c_oflag &= ~XTABS;
	return 0;
  }

  if (match(opt, "onoeot")) {
	termios.c_oflag |= ONOEOT;
	return 0;
  }
  if (match(opt, "-onoeot")) {
	termios.c_oflag &= ~ONOEOT;
	return 0;
  }
#endif

  /* Local modes. */

  if (match(opt, "echo")) {
	termios.c_lflag |= ECHO;
	return 0;
  }
  if (match(opt, "-echo")) {
	termios.c_lflag &= ~ECHO;
	return 0;
  }

  if (match(opt, "echoe")) {
	termios.c_lflag |= ECHOE;
	return 0;
  }
  if (match(opt, "-echoe")) {
	termios.c_lflag &= ~ECHOE;
	return 0;
  }

  if (match(opt, "echok")) {
	termios.c_lflag |= ECHOK;
	return 0;
  }
  if (match(opt, "-echok")) {
	termios.c_lflag &= ~ECHOK;
	return 0;
  }

  if (match(opt, "echonl")) {
	termios.c_lflag |= ECHONL;
	return 0;
  }
  if (match(opt, "-echonl")) {
	termios.c_lflag &= ~ECHONL;
	return 0;
  }

  if (match(opt, "icanon")) {
	termios.c_lflag |= ICANON;
	return 0;
  }
  if (match(opt, "-icanon")) {
	termios.c_lflag &= ~ICANON;
	return 0;
  }

  if (match(opt, "iexten")) {
	termios.c_lflag |= IEXTEN;
	return 0;
  }
  if (match(opt, "-iexten")) {
	termios.c_lflag &= ~IEXTEN;
	return 0;
  }

  if (match(opt, "isig")) {
	termios.c_lflag |= ISIG;
	return 0;
  }
  if (match(opt, "-isig")) {
	termios.c_lflag &= ~ISIG;
	return 0;
  }

  if (match(opt, "noflsh")) {
	termios.c_lflag |= NOFLSH;
	return 0;
  }
  if (match(opt, "-noflsh")) {
	termios.c_lflag &= ~NOFLSH;
	return 0;
  }

  if (match(opt, "tostop")) {
	termios.c_lflag |= TOSTOP;
	return 0;
  }
  if (match(opt, "-tostop")) {
	termios.c_lflag &= ~TOSTOP;
	return 0;
  }

#ifdef __minix
  if (match(opt, "lflusho")) {
	termios.c_lflag |= LFLUSHO;
	return 0;
  }
  if (match(opt, "-lflusho")) {
	termios.c_lflag &= ~LFLUSHO;
	return 0;
  }
#endif

  /* The special control characters. */
  if (match(opt, "eof")) {
	set_control(VEOF, next);
	return 1;
  }

  if (match(opt, "eol")) {
	set_control(VEOL, next);
	return 1;
  }

  if (match(opt, "erase")) {
	set_control(VERASE, next);
	return 1;
  }

  if (match(opt, "intr")) {
	set_control(VINTR, next);
	return 1;
  }

  if (match(opt, "kill")) {
	set_control(VKILL, next);
	return 1;
  }

  if (match(opt, "quit")) {
	set_control(VQUIT, next);
	return 1;
  }

  if (match(opt, "susp")) {
	set_control(VSUSP, next);
	return 1;
  }

  if (match(opt, "start")) {
	set_control(VSTART, next);
	return 1;
  }

  if (match(opt, "stop")) {
	set_control(VSTOP, next);
	return 1;
  }
#ifdef __minix
  if (match(opt, "rprnt")) {
	set_control(VREPRINT, next);
	return 1;
  }

  if (match(opt, "lnext")) {
	set_control(VLNEXT, next);
	return 1;
  }

  if (match(opt, "flush")) {
	set_control(VDISCARD, next);
	return 1;
  }
#endif
 
  if (match(opt, "min")) {
	set_min_tim(VMIN, next);
	return 1;
  }

  if (match(opt, "time")) {
	set_min_tim(VTIME, next);
	return 1;
  }

  /* Special modes. */
  if (opt[0] == ':')
  {
	set_saved_settings(opt);
	return 0;
  }

  if (match(opt, "cooked") || match(opt, "raw")) {
	int x = opt[0] == 'c' ? 1 : 0;

	option(x + "-icrnl", "");	/* off in raw mode, on in cooked mode */
	option(x + "-ixon", "");
	option(x + "-opost", "");
	option(x + "-onlcr", "");
	option(x + "-isig", "");
	option(x + "-icanon", "");
	option(x + "-iexten", "");
	option(x + "-echo", "");
	return 0;
  }

  if (match(opt, "evenp") || match(opt, "parity")) {
	option("parenb", "");
	option("cs7", "");
	option("-parodd", "");
	return 0;
  }

  if (match(opt, "oddp")) {
	option("parenb", "");
	option("cs7", "");
	option("parodd", "");
	return 0;
  }

  if (match(opt, "-parity") || match(opt, "-evenp") || match(opt, "-oddp")) {
	option("-parenb", "");
	option("cs8", "");
	return 0;
  }

  if (match(opt, "nl")) {
	option("icrnl", "");
	return 0;
  }

  if (match(opt, "-nl")) {
	option("-icrnl", "");
	option("-inlcr", "");
	option("-igncr", "");
	return 0;
  }

  if (match(opt, "ek")) {
	termios.c_cc[VERASE]= TERASE_DEF;;
	termios.c_cc[VKILL]= TKILL_DEF;;
	return 0;
  }

  if (match(opt, "sane"))
  {
	/* Reset all terminal attributes to a sane state, except things like
	 * line speed and parity, because it can't be known what their sane
	 * values are.
	 */
	termios.c_iflag= (TINPUT_DEF & ~(IGNPAR|ISTRIP|INPCK))
		| (termios.c_iflag & (IGNPAR|ISTRIP|INPCK));
#ifdef __minix
	termios.c_oflag= (TOUTPUT_DEF & ~(XTABS))
		| (termios.c_oflag & (XTABS));
#endif
	termios.c_cflag= (TCTRL_DEF & ~(CLOCAL|CSIZE|CSTOPB|PARENB|PARODD))
		| (termios.c_cflag & (CLOCAL|CSIZE|CSTOPB|PARENB|PARODD));
	termios.c_lflag= (TLOCAL_DEF & ~(ECHOE|ECHOK))
		| (termios.c_lflag & (ECHOE|ECHOK));
	if (termios.c_lflag & ICANON) {
		termios.c_cc[VMIN]= TMIN_DEF;
		termios.c_cc[VTIME]= TTIME_DEF;
	}
	termios.c_cc[VEOF]= TEOF_DEF;
	termios.c_cc[VEOL]= TEOL_DEF;
	termios.c_cc[VERASE]= TERASE_DEF;
	termios.c_cc[VINTR]= TINTR_DEF;
	termios.c_cc[VKILL]= TKILL_DEF;
	termios.c_cc[VQUIT]= TQUIT_DEF;
	termios.c_cc[VSUSP]= TSUSP_DEF;
#ifdef __minix
	termios.c_cc[VREPRINT]= TREPRINT_DEF;
	termios.c_cc[VLNEXT]= TLNEXT_DEF;
	termios.c_cc[VDISCARD]= TDISCARD_DEF;
#endif
	termios.c_cc[VSTART]= TSTART_DEF;
	termios.c_cc[VSTOP]= TSTOP_DEF;
	if (!(termios.c_lflag & ICANON)) {
		termios.c_cc[VMIN]= TMIN_DEF;
		termios.c_cc[VTIME]= TTIME_DEF;
	}
	return 0;
  }

#ifdef __minix
  if (match(opt, "cols"))
  {
  	num= strtol(next, &check, 0);
  	if (check[0] != '\0')
  	{
  		fprintf(stderr, "%s: illegal parameter to cols: '%s'\n", 
  							prog_name, next);
  		return 1;
  	}
  	winsize.ws_col= num;
	return 1;
  }

  if (match(opt, "rows"))
  {
  	num= strtol(next, &check, 0);
  	if (check[0] != '\0')
  	{
  		fprintf(stderr, "%s: illegal parameter to rows: '%s'\n", 
  							prog_name, next);
  		return 1;
  	}
  	winsize.ws_row= num;
	return 1;
  }

  if (match(opt, "xpixels"))
  {
  	num= strtol(next, &check, 0);
  	if (check[0] != '\0')
  	{
  		fprintf(stderr, "%s: illegal parameter to xpixels: '%s'\n", 
  							prog_name, next);
  		return 1;
  	}
  	winsize.ws_xpixel= num;
	return 1;
  }

  if (match(opt, "ypixels"))
  {
  	num= strtol(next, &check, 0);
  	if (check[0] != '\0')
  	{
  		fprintf(stderr, "%s: illegal parameter to ypixels: '%s'\n", 
  							prog_name, next);
  		return 1;
  	}
  	winsize.ws_ypixel= num;
	return 1;
  }
#endif /* __minix */

  fprintf(stderr, "%s: unknown mode: %s\n", prog_name, opt);
  return 0;
}

int match(s1, s2)
char *s1, *s2;
{

  while (1) {
	if (*s1 == 0 && *s2 == 0) return(1);
	if (*s1 == 0 || *s2 == 0) return (0);
	if (*s1 != *s2) return (0);
	s1++;
	s2++;
  }
}

void prctl(c)
char c;
{
  if (c < ' ')
	printf("^%c", 'A' + c - 1);
  else if (c == 0177)
	printf("^?");
  else
	printf("%c", c);
}

struct s2s {
	speed_t ts;
	long ns;
} s2s[] = {
	{ B0,		     0 },
	{ B50,		    50 },
	{ B75,		    75 },
	{ B110,		   110 },
	{ B134,		   134 },
	{ B150,		   150 },
	{ B200,		   200 },
	{ B300,		   300 },
	{ B600,		   600 },
	{ B1200,	  1200 },
	{ B1800,	  1800 },
	{ B2400,	  2400 },
	{ B4800,	  4800 },
	{ B9600,	  9600 },
	{ B19200,	 19200 },
	{ B38400,	 38400 },
#ifdef __minix
	{ B57600,	 57600 },
	{ B115200,	115200 },
#ifdef B230400
	{ B230400,	230400 },
#endif
#ifdef B460800
	{ B460800,	460800 },
#endif
#ifdef B921600
	{ B921600,	921600 },
#endif
#endif
};

speed_t long2speed(num)
long num;
{
	struct s2s *sp;

	for (sp = s2s; sp < s2s + (sizeof(s2s) / sizeof(s2s[0])); sp++) {
		if (sp->ns == num) return sp->ts;
	}
	return -1;
}

long speed2long(speed)
unsigned long speed;
{
	struct s2s *sp;

	for (sp = s2s; sp < s2s + (sizeof(s2s) / sizeof(s2s[0])); sp++) {
		if (sp->ts == speed) return sp->ns;
	}
	return -1;
}
		
void print_flags(flags, flag, def, string, all)
unsigned long flags;
unsigned long flag;
unsigned long def;
char *string;
int all;
{
	if (!(flags & flag))
	{
		if (all || (def & flag))
			output(string);
		return;
	}
	string++;
	if (all || !(def & flag))
		output(string);
}

void output(s)
char *s;
{
	int len;

	len= strlen(s);
	if (column + len + 3 >= max_column)
	{
		printf("\n");
		column= 0;
	}
	if (column)
	{
		putchar(' ');
		column++;
	}
	fputs(s, stdout);
	column += len;
}

void do_print_char(chr, def, name, all)
unsigned chr;
unsigned def;
char *name;
int all;
{
	char line[20];

	if (!all && chr == def)
		return;
	
#ifdef _POSIX_VDISABLE
	if (chr == _POSIX_VDISABLE)
		sprintf(line, "%s = <undef>", name);
	else
#endif
	if (chr < ' ')
		sprintf(line, "%s = ^%c", name, chr + '@');
	else if (chr == 127)
		sprintf(line, "%s = ^?", name);
	else
		sprintf(line, "%s = %c", name, chr);
	output(line);
}

void do_print_num(num, def, name, all)
unsigned num;
unsigned def;
char *name;
int all;
{
	char line[20];
		
	if (!all && num == def)
		return;
	sprintf(line, "%s = %u", name, num);
	output(line);
}

void set_saved_settings(opt)
char *opt;
{
	long num;
	char *check;
	tcflag_t c_oflag, c_cflag, c_lflag, c_iflag;
	cc_t c_cc[NCCS];
	speed_t ispeed, ospeed;
	int i;

	check= opt;
	num= strtol(check+1, &check, 16);
	if (check[0] != ':')
	{
		fprintf(stderr, "error in saved settings '%s'\n", opt);
		return;
	}
	c_iflag= num;

	num= strtol(check+1, &check, 16);
	if (check[0] != ':')
	{
		fprintf(stderr, "error in saved settings '%s'\n", opt);
		return;
	}
	c_oflag= num;

	num= strtol(check+1, &check, 16);
	if (check[0] != ':')
	{
		fprintf(stderr, "error in saved settings '%s'\n", opt);
		return;
	}
	c_cflag= num;

	num= strtol(check+1, &check, 16);
	if (check[0] != ':')
	{
		fprintf(stderr, "error in saved settings '%s'\n", opt);
		return;
	}
	c_lflag= num;

	num= strtol(check+1, &check, 16);
	if (check[0] != ':')
	{
		fprintf(stderr, "error in saved settings '%s'\n", opt);
		return;
	}
	ispeed= num;

	num= strtol(check+1, &check, 16);
	if (check[0] != ':')
	{
		fprintf(stderr, "error in saved settings '%s'\n", opt);
		return;
	}
	ospeed= num;

	for(i=0; i<NCCS; i++)
	{
		num= strtol(check+1, &check, 16);
		if (check[0] != ':')
		{
			fprintf(stderr, "error in saved settings '%s'\n", opt);
			return;
		}
		c_cc[i]= num;
	}
	if (check[1] != '\0')
	{
		fprintf(stderr, "error in saved settings '%s'\n", opt);
		return;
	}
	termios.c_iflag= c_iflag;
	termios.c_oflag= c_oflag;
	termios.c_cflag= c_cflag;
	termios.c_lflag= c_lflag;

	cfsetispeed(&termios, ispeed);
	cfsetospeed(&termios, ospeed);

	for(i=0; i<NCCS; i++)
		termios.c_cc[i]= c_cc[i];
}

void set_control(option, value)
int option;
char *value;
{
	int chr;

	if (match(value, "undef") || match(value, "^-")) {
#ifdef _POSIX_VDISABLE
		chr= _POSIX_VDISABLE;
#else
		fprintf(stderr, 
			"stty: unable to set option to _POSIX_VDISABLE\n");
		return;
#endif
	} else if (match(value, "^?"))
		chr= '\177';
	else if (strlen(value) == 2 && value[0] == '^') {
		chr= toupper(value[1]) - '@';
		if (chr < 0 || chr >= 32) {
			fprintf(stderr, "stty: illegal option value: '%s'\n",
				value);
			return;
		}
	} else if (strlen(value) == 1)
		chr= value[0];
	else {
		fprintf(stderr, "stty: illegal option value: '%s'\n", value);
		return;
	}

	assert(option >= 0 && option < NCCS);
	termios.c_cc[option]= chr;
}
		
void set_min_tim(option, value)
int option;
char *value;
{
	long num;
	char *check;

	num= strtol(value, &check, 0);
	if (check[0] != '\0') {
		fprintf(stderr, "stty: illegal option value: '%s'\n", value);
		return;
	}

	if ((cc_t)num != num) {
		fprintf(stderr, "stty: illegal option value: '%s'\n", value);
		return;
	}
	assert(option >= 0 && option < NCCS);
	termios.c_cc[option]= num;
}

/*
 * $PchId: stty.c,v 1.7 2001/05/02 15:04:42 philip Exp $
 */
