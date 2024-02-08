/**
 * @brief cursor-off - Disables the VGA text mode cursor.
 *
 * This is an old tool that calls a special system call
 * to change the VGA text-mode cursor position. The VGA
 * terminal renders its own cursor in software, so we
 * try to move the hardware cursor off screen so it doesn't
 * interfere with the rest of the terminal and look weird.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2016-2018 K. Lange
 */
#include <unistd.h>
#include <fcntl.h>
#include <sys/sysfunc.h>

int main(int argc, char * argv[]) {
	int fd = open("/dev/port", O_RDWR);
	if (fd < 0) return 1;
	pwrite(fd, (unsigned char[]){14}, 1, 0x3D4);
	pwrite(fd, (unsigned char[]){0xFF}, 1, 0x3D5);
	pwrite(fd, (unsigned char[]){15}, 1, 0x3D4);
	pwrite(fd, (unsigned char[]){0xFF}, 1, 0x3D5);
	return 0;
}
