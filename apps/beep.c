/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014-2018 K. Lange
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

int spkr = 0;

struct spkr {
	int length;
	int frequency;
};

static void note(int length, int frequency) {
	struct spkr s = {
		.length = length,
		.frequency = frequency,
	};

	write(spkr, &s, sizeof(s));
}

/* Stolen from the Linux tool */
#define DEFAULT_FREQ  440.0
#define DEFAULT_LEN   200
#define DEFAULT_DELAY 100

static int repetitions = 1;
static float frequency = DEFAULT_FREQ;
static int length      = DEFAULT_LEN;
static int delay       = DEFAULT_DELAY;
static int beep_after  = 0;

void beep(void) {
	for (int i = 0; i < repetitions; ++i) {
		note(length, frequency * 10);
		if (delay && ((i != repetitions - 1) || beep_after)) {
			usleep(delay * 1000);
		}
	}

}

int main(int argc, char * argv[]) {

	spkr = open("/dev/spkr", O_WRONLY);
	if (spkr == -1) {
		fprintf(stderr, "%s: could not open speaker\n", argv[0]);
	}

	int opt;
	while ((opt = getopt(argc, argv, "?hr:f:l:d:D:n")) != -1) {
		switch (opt) {
			case 'h':
			case '?':
				fprintf(stderr, "usage: %s BEEP...\n"
						"Where BEEP consists of:\n"
						"  -r  REPS  \033[3mNumber of repetitions.\033[0m\n"
						"  -f  FREQ  \033[3mFrequency in Hz. 440 is A4. Supports fractional values.\033[0m\n"
						"  -l  TIME  \033[3mDuration in milliseconds.\033[0m\n"
						"  -d  TIME  \033[3mDelay between repetitions in milliseconds.\033[0m\n"
						"  -D  TIME  \033[3mDelay between, and after, repetitions.\033[0m\n"
						"  -n        \033[3mStart a new beep.\033[0m\n"
						"\n"
						"The default values are:\n"
						"   -r 1 -l %d -f %.2f -d %d\n"
						"\n"
						"A length of -1 will start a sustained beep without blocking.\n"
						"A length of 0 will stop a currently playing sustained beep.\n",
						argv[0], DEFAULT_LEN, DEFAULT_FREQ, DEFAULT_DELAY);
				return 1;
			case 'r':
				repetitions = atoi(optarg);
				break;
			case 'l':
				length = atoi(optarg);
				break;
			case 'f':
				frequency = atof(optarg);
				break;
			case 'd':
				delay = atoi(optarg);
				beep_after = 0;
				break;
			case 'D':
				delay = atoi(optarg);
				beep_after = 1;
				break;
			case 'n':
				beep();
				repetitions = 1;
				frequency = DEFAULT_FREQ;
				length = DEFAULT_LEN;
				delay = DEFAULT_DELAY;
				beep_after = 0;
				break;
		}
	}

	beep();

	return 0;
}

