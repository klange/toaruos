/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2015 Kevin Lange
 */

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/ioctl.h>

int main(int argc, char * argv[]) {
	int spkr = open("/dev/dsp", O_WRONLY);
	int song = open(argv[1], O_RDONLY);

	if (spkr == -1) {
		fprintf(stderr, "no dsp\n");
		return 1;
	}

	if (song == -1) {
		fprintf(stderr, "audio file not found\n");
		return 2;
	}

	char buf[0x1000];
	while (read(song, buf, sizeof(buf))) {
		write(spkr, buf, sizeof(buf));
	}
	return 0;
}
