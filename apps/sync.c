/**
 * @brief Wait for filesystem buffered writes to finish.
 *
 * And by "wait" and "to finish" I mean "tell the block device
 * to write everything because it doesn't do that asynchronously".
 *
 * Currently only syncs the block device that owns the current
 * working directory because I haven't set this up to iterate
 * over mounts yet.
 *
 * It will correctly wait until the sync finishes, though.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021 K. Lange
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>

int main(int argc, char * argv[]) {
	char * file = ".";
	if (argc > 1) {
		file = argv[1];
	}
	int fd = open(file,O_RDONLY|O_DIRECTORY);
	if (fd < 0) {
		fd = open(file,O_RDONLY);
	}
	if (fd < 0) {
		fprintf(stderr, "sync: open: %s: %s (%d)\n", file, strerror(errno), fd);
		return 1;
	}
	int res = ioctl(fd, IOCTLSYNC, NULL);
	if (res < 0) {
		fprintf(stderr, "sync: ioctl: %s (%d)\n", strerror(errno), fd);
		return 1;
	}
	return 0;
}
