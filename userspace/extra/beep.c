/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014 Kevin Lange
 */
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

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

int main(int argc, char * argv[]) {

	spkr = open("/dev/spkr", O_WRONLY);
	if (spkr == -1) {
		fprintf(stderr, "%s: could not open speaker\n", argv[0]);
	}

	note(20, 15680);
	note(10, 11747);
	note(10, 12445);
	note(20, 13969);
	note(10, 12445);
	note(10, 11747);
	note(20, 10465);
	note(10, 10465);
	note(10, 12445);
	note(20, 15680);
	note(10, 13969);
	note(10, 12445);
	note(30, 11747);
	note(10, 12445);
	note(20, 13969);
	note(20, 15680);
	note(20, 12445);
	note(20, 10465);
	note(20, 10465);

	return 0;
}

