/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 *
 * set-resolution - Change the display resolution.
 *
 * Simple tool to interface with the IO_VID_SET ioctl.
 *
 * TODO: This is probably something we should request from the
 *       compositor rather than a separate application...
 */
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <kernel/video.h>

int main(int argc, char * argv[]) {
	if (argc < 3) {
		fprintf(stderr, "Usage: %s [--initialize DRIVER] WIDTH HEIGHT\n", argv[0]);
	}

	/* Open framebuffer */
	int fd = open("/dev/fb0", O_RDONLY);

	if (fd < 0) {
		perror("open");
		return 1;
	}

	int i = 1;
	int init = 0;
	char * driver = NULL;

	if (argc > 4 && !strcmp(argv[1],"--initialize")) {
		init = 1;
		driver = argv[2];
		i += 2;
	}

	/* Prepare ioctl from arguments */
	struct vid_size s;
	s.width = atoi(argv[i]);
	s.height = atoi(argv[i+1]);

	/* Send ioctl */
	if (init) {
		char tmp[100];
		sprintf(tmp, "%s,%lu,%lu", driver, s.width, s.height);
		if (ioctl(fd, IO_VID_REINIT, tmp) < 0) {
			perror("ioctl");
			return 1;
		}
	} else {
		if (ioctl(fd, IO_VID_SET, &s) < 0) {
			perror("ioctl");
			return 1;
		}
	}

	return 0;
}
