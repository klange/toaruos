/**
 * @brief play - Play back PCM samples
 *
 * This needs very specifically-formatted PCM data to function
 * properly - 16-bit, signed, stereo, little endian, and 48KHz.
 *
 * TODO This should be fixed up to play back WAV files properly.
 *      We have a sample rate convert in the out-of-repo playmp3
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2015-2018 K. Lange
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <getopt.h>
#include <errno.h>

#include <sys/ioctl.h>

#define DSP_PATH "/dev/dsp"

static int usage(char * argv[]) {
	fprintf(stderr, "usage: %s [-d dsp_path] /path/to/48ks16le.wav\n", argv[0]);
	return 1;
}

int main(int argc, char * argv[]) {
	char buf[0x1000];
	int spkr, song;
	ssize_t r;
	int opt;
	char * dsp_path = DSP_PATH;

	while ((opt = getopt(argc, argv, "d:s:")) != -1) {
		switch (opt) {
			case 'd': /* DSP path */
				dsp_path = optarg;
				break;

			default:
				return usage(argv);
		}
	}

	if (optind == argc) return usage(argv);

	spkr = open(dsp_path, O_WRONLY);
	if (spkr < 0) {
		fprintf(stderr, "%s: %s: %s\n", argv[0], dsp_path, strerror(errno));
		return 1;
	}

	if (!strcmp(argv[optind], "-")) {
		song = STDIN_FILENO;
	} else {
		song = open(argv[optind], O_RDONLY);
		if (song < 0) {
			fprintf(stderr, "%s: %s: %s\n", argv[0], argv[optind], strerror(errno));
			return 1;
		}
	}

	while ((r = read(song, buf, sizeof(buf))) > 0) {
		if (write(spkr, buf, r) < 0) {
			fprintf(stderr, "%s: %s: %s\n", argv[0], dsp_path, strerror(errno));
			return 1;
		}
	}

	if (r < 0) {
		fprintf(stderr, "%s: %s: %s\n", argv[0], argv[optind], strerror(errno));
		return 1;
	}
	return 0;
}
