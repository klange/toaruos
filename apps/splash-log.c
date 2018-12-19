/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 *
 * splash-log - Display startup messages before UI has started.
 */
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include <kernel/video.h>
#include <toaru/pex.h>

#include "terminal-font.h"

/**
 * For legacy backwards-compatibility reasons, the VGA
 * text-mode window is normalled mapped 1:1. If this
 * ever changes, a few applications will need to be updated.
 */
static unsigned short * textmemptr = (unsigned short *)0xB8000;
static void placech(unsigned char c, int x, int y, int attr) {
	unsigned short *where;
	unsigned att = attr << 8;
	where = textmemptr + (y * 80 + x);
	*where = c | att;
}

/**
 * Graphical framebuffer is a bit more straightforward.
 */
static int framebuffer_fd = -1;
static int width, height, depth;
static char * framebuffer;

static void set_point(int x, int y, uint32_t value) {
	uint32_t * disp = (uint32_t *)framebuffer;
	uint32_t * cell = &disp[y * width + x];
	*cell = value;
}

#define BG_COLOR 0xFF050505
#define FG_COLOR 0xFFCCCCCC
#define char_width  9
#define char_height 20
static void write_char(int x, int y, int val, uint32_t color) {
	if (val > 128) {
		val = 4;
	}
	uint16_t * c = large_font[val];
	for (uint8_t i = 0; i < char_height; ++i) {
		for (uint8_t j = 0; j < char_width; ++j) {
			if (c[i] & (1 << (15-j))) {
				set_point(x+j,y+i,color);
			} else {
				set_point(x+j,y+i,BG_COLOR);
			}
		}
	}
}

static void update_message(char * c) {
	if (framebuffer_fd != -1) {
		int x = 20;
		int y = 20;
		while (*c) {
			write_char(x, y, *c, FG_COLOR);
			c++;
			x += char_width;
		}
		while (x < width - char_width) {
			write_char(x, y, ' ', FG_COLOR);
			x += char_width;
		}
	} else {
		int x = 2;
		int y = 2;
		while (*c) {
			placech(*c, x, y, 0x7);
			c++;
			x++;
		}
		while (x < 80) {
			placech(' ', x, y, 0x7);
			x++;
		}
	}
}

static void clear_screen(void) {
	if (framebuffer_fd != -1) {
		for (int y = 0; y < height; ++y) {
			for (int x = 0; x < width; ++x) {
				set_point(x,y,BG_COLOR);
			}
		}

	} else {
		for (int y = 0; y < 24; ++y) {
			for (int x = 0; x < 80; ++x) {
				placech(' ', x, y, 0); /* Clear */
			}
		}
	}
}

static void check_framebuffer(void) {
	int tmpfd = open("/proc/framebuffer", O_RDONLY);
	if (tmpfd > 0) {
		framebuffer_fd = open("/dev/fb0", O_RDONLY);
		ioctl(framebuffer_fd, IO_VID_WIDTH,  &width);
		ioctl(framebuffer_fd, IO_VID_HEIGHT, &height);
		ioctl(framebuffer_fd, IO_VID_DEPTH,  &depth);
		ioctl(framebuffer_fd, IO_VID_ADDR,   &framebuffer);
		ioctl(framebuffer_fd, IO_VID_SIGNAL, NULL);
	} else {
		framebuffer_fd = -1;
	}
}

static FILE * pex_endpoint = NULL;
static void open_socket(void) {
	pex_endpoint = pex_bind("splash");
	if (!pex_endpoint) exit(1);
}

int main(int argc, char * argv[]) {
	if (getuid() != 0) {
		fprintf(stderr, "%s: only root should run this\n", argv[0]);
		return 1;
	}

	open_socket();

	if (!fork()) {
		check_framebuffer();
		clear_screen();
		update_message("ToaruOS is starting up...");

		while (1) {
			pex_packet_t * p = calloc(PACKET_SIZE, 1);
			pex_listen(pex_endpoint, p);

			if (p->size < 4)  continue; /* Ignore blank messages, erroneous line feeds, etc. */
			if (p->size > 80) continue; /* Ignore overly large messages */

			if (!strncmp((char*)p->data, "!quit", 5)) {
				/* Use the special message !quit to exit. */
				fclose(pex_endpoint);
				return 0;
			} else {
				/* Make sure message is nil terminated (it should be...) */
				char * tmp = malloc(p->size + 1);
				memcpy(tmp, p->data, p->size);
				tmp[p->size] = '\0';
				update_message(tmp);
				free(tmp);
			}
		}
	}

	return 0;
}
