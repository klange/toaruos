/**
 * @brief Implementation of the 'beep' utility.
 *
 * I've tried to get the functionality here as close to the common
 * utility available on Linux systems so that "beep music" can be
 * played back.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014-2018 K. Lange
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

int spkr = 0;
int beeper = 1;
int envelope = 1;
float volume = 1.0;

struct spkr {
	int length;
	int frequency;
};

static void note(int length, int frequency) {
	if (beeper) {
		struct spkr s = {
			.length = length,
			.frequency = frequency,
		};

		write(spkr, &s, sizeof(s));
	} else {
		/* Attempt to generate a PCM tone of the appropriate length of frequency. */
		double f = frequency;

		/* The sample rate of the dsp is whatever, s16le stereo */
		uint16_t * beep = malloc(sizeof(uint16_t)*48*length*2);

		for (int i = 0; i < 48 * length; i++) {
			int16_t sample = sin((f * (double)i / 48000.0) / M_PI) * 20000.0 * volume;
			if (envelope) {
				double pct = (double)i / (double)(48 * length);
				pct = pct * pct;
				sample = (1.0 - pct) * sample;
			}
			beep[i*2] = sample;
			beep[i*2+1] = sample;
		}
		write(spkr,beep,sizeof(uint16_t)*48*length*2);
		free(beep);
	}
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
	if (spkr == -1) return;
	for (int i = 0; i < repetitions; ++i) {
		note(length, frequency * 10);
		if (delay && ((i != repetitions - 1) || beep_after)) {
			usleep(delay * 1000);
		}
	}

}

int usage(char * argv[]) {
#define X_S "\033[3m"
#define X_E "\033[0m"
	fprintf(stderr, "usage: %s BEEP...\n"
			"Where BEEP consists of:\n"
			"  -r  " X_S "REPS    Number of repetitions." X_E" \n"
			"  -f  " X_S "FREQ    Frequency in Hz. 440 is A4. Supports fractional values." X_E" \n"
			"  -l  " X_S "TIME    Duration in milliseconds." X_E" \n"
			"  -d  " X_S "TIME    Delay between repetitions in milliseconds." X_E" \n"
			"  -D  " X_S "TIME    Delay between, and after, repetitions." X_E" \n"
			"  -n          " X_S "Start a new beep." X_E" \n"
			"\n"
			"The default values are:\n"
			"   -r 1 -l %d -f %.2f -d %d\n"
			"\n"
			"This implementation has some extra options to control DSP playback:\n"
			"  --dsp           " X_S "Forces the use of the dsp even if the beeper was" X_E "\n"
			"                  " X_S "available." X_E "\n"
			"  --no-envelope   " X_S "Disables the full length release envelope" X_E "\n"
			"                  " X_S "that is otherwise applied to DSP tone generator." X_E" \n"
			"  --volume=" X_S "vol    Sets the volume as a float percentage." X_E "\n"
			"\n"
			"A length of -1 will start a sustained beep without blocking.\n"
			"A length of 0 will stop a currently playing sustained beep.\n",
			argv[0], DEFAULT_LEN, DEFAULT_FREQ, DEFAULT_DELAY);
	return 1;
}

int main(int argc, char * argv[]) {

	spkr = open("/dev/spkr", O_WRONLY);
	if (spkr == -1) {
		spkr = open("/dev/dsp", O_WRONLY);
		beeper = 0;
		if (spkr == -1) {
			fprintf(stderr, "%s: could not open speaker\n", argv[0]);
		}
	}

	int opt;
	while ((opt = getopt(argc, argv, "?hr:f:l:d:D:n-:")) != -1) {
		switch (opt) {
			case 'h':
			case '?':
				return usage(argv);
			case '-':
				if (!strcmp(optarg,"no-envelope")) {
					envelope = 0;
					break;
				} else if (!strcmp(optarg,"dsp")) {
					if (beeper)  {
						spkr = open("/dev/dsp", O_WRONLY);
						beeper = 0;
						if (spkr == -1) {
							fprintf(stderr, "%s: DSP was requested but is not available\n", argv[0]);
							return 1;
						}
					}
					break;
				} else if (strstr(optarg,"volume=") == optarg) {
					volume = atof(optarg+7) / 100.0;
					break;
				}
				fprintf(stderr, "%s: '--%s' is not a recognized long option\n", argv[0], optarg);
				return usage(argv);
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

	return spkr == -1;
}

