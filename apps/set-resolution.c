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
		fprintf(stderr, "Usage: %s width height\n", argv[0]);
	}

	/* Open framebuffer */
	int fd = open("/dev/fb0", O_RDONLY);

	if (fd < 0) {
		perror("open");
		return 1;
	}

	/* Prepare ioctl from arguments */
	struct vid_size s;
	s.width = atoi(argv[1]);
	s.height = atoi(argv[2]);

	/* Send ioctl */
	if (ioctl(fd, IO_VID_SET, &s) < 0) {
		perror("ioctl");
		return 1;
	}

	return 0;
}
