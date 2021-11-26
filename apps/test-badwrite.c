/**
 * @brief Test tool for examining a bug that was crashing the audio subsystem.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021 K. Lange
 */
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

int main(int argc, char * argv[]) {
	int fd = open("/dev/dsp",O_WRONLY);
	if (fd < 0) {
		fprintf(stderr, "failed to open dsp\n");
		return 1;
	}
	return write(fd, &fd, -1);
}
