/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014 Kevin Lange
 */
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>

int spkr = 0;

struct spkr {
	int length;
	int frequency;
};

void note(int length, int frequency) {
	struct spkr s = {
		.length = 2,
		.frequency = frequency,
	};

	write(spkr, &s, sizeof(s));
}

struct termios old;

void set_unbuffered() {
	tcgetattr(fileno(stdin), &old);
	struct termios new = old;
	new.c_lflag &= (~ICANON & ~ECHO);
	tcsetattr(fileno(stdin), TCSAFLUSH, &new);
}


int main(int argc, char * argv[]) {

	spkr = open("/dev/spkr", O_WRONLY);
	if (spkr == -1) {
		fprintf(stderr, "%s: could not open speaker\n", argv[0]);
		return 1;
	}

	set_unbuffered();

	char c;
	while (c = fgetc(stdin)) {
		switch (c) {
			case 'z': return 0;
			case 'a': note(10, 1308); break;
			case 'w': note(10, 1386); break;
			case 's': note(10, 1468); break;
			case 'e': note(10, 1556); break;
			case 'd': note(10, 1648); break;
			case 'f': note(10, 1746); break;
			case 't': note(10, 1850); break;
			case 'g': note(10, 1960); break;
			case 'y': note(10, 2077); break;
			case 'h': note(10, 2200); break;
			case 'u': note(10, 2331); break;
			case 'j': note(10, 2469); break;
			case 'k': note(10, 2616); break;
			case 'o': note(10, 2772); break;
			case 'l': note(10, 2937); break;
			case 'p': note(10, 3111); break;
			case ';': note(10, 3296); break;
			case '\'': note(10, 3492);break;
		}
	}

	return 0;
}

