/**
 * @brief set-font - set vga emulation font
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2026 K. Lange
 */
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <err.h>
#include <sys/ioctl.h>
#include <kernel/video.h>

static char font[256*17];

static int usage(int argc, char * argv[]) {
	fprintf(stderr, "usage: %s [-g] font_payload\n", argv[0]);
	return 1;
}

int main(int argc, char * argv[]) {
	int get_font = 0;
	int opt;

	while ((opt = getopt(argc, argv, "g")) != -1) {
		switch (opt) {
			case 'g':
				get_font = 1;
				break;
			default:
				return usage(argc, argv);
		}
	}

	if (optind + 1 != argc) return usage(argc, argv);

	int fd = open("/dev/vga0", O_RDWR);
	if (fd < 0) err(1, "/dev/vga0");

	int f = open(argv[optind], get_font ? (O_WRONLY | O_CREAT) : O_RDONLY, 0644);
	if (f < 0) err(1, argv[optind]);

	if (get_font) {
		if (ioctl(fd, IO_VGA_GETFONT, font) < 0) err(1, "ioctl");
		ssize_t w = write(f, &font, sizeof(font));
		if (w < 0) err(1, "write");
	} else {
		ssize_t r = read(f, &font, sizeof(font));
		if (r < 0) err(1, "read");
		if (r != sizeof(font)) errx(1, "font is wrong size");
		if (ioctl(fd, IO_VGA_SETFONT, font) < 0) err(1, "ioctl");
	}

	return 0;
}
