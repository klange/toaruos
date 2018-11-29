/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014-2018 K. Lange
 *
 * piano - Interactively make beeping noises
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
		.length = length,
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
	while ((c = fgetc(stdin))) {
		switch (c) {
			case 'q': note(0, 1000); return 0;
			case 'z': note(0, 1000); return 0;
			case ' ': note(0, 1000); break;
			case 'a': note(-1, 1308); break;
			case 'w': note(-1, 1386); break;
			case 's': note(-1, 1468); break;
			case 'e': note(-1, 1556); break;
			case 'd': note(-1, 1648); break;
			case 'f': note(-1, 1746); break;
			case 't': note(-1, 1850); break;
			case 'g': note(-1, 1960); break;
			case 'y': note(-1, 2077); break;
			case 'h': note(-1, 2200); break;
			case 'u': note(-1, 2331); break;
			case 'j': note(-1, 2469); break;
			case 'k': note(-1, 2616); break;
			case 'o': note(-1, 2772); break;
			case 'l': note(-1, 2937); break;
			case 'p': note(-1, 3111); break;
			case ';': note(-1, 3296); break;
			case '\'': note(-1, 3492);break;
		}
	}

	return 0;
}

