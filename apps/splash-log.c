/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018-2021 K. Lange
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
#include <sys/utsname.h>

#include <kernel/video.h>
#include <toaru/pex.h>

#include "terminal-font.h"

static void do_nothing() {
	/* do nothing */
}

/* Framebuffer setup */
static int framebuffer_fd = -1;
static long width, height, depth;
static char * framebuffer;
static void (*update_message)(char * c, int line) = do_nothing;
static void (*clear_screen)(void) = do_nothing;

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

static unsigned int line_offset = 0;
static void fb_update_message(char * c, int line) {
	int x = 20;
	int y = 20 + line_offset * char_height;
	if (line == 0) {
		line_offset++;
	}
	while (*c) {
		write_char(x, y, *c, FG_COLOR);
		c++;
		x += char_width;
	}
	while (x < width - char_width) {
		write_char(x, y, ' ', FG_COLOR);
		x += char_width;
	}
}

static void fb_clear_screen(void) {
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			set_point(x,y,BG_COLOR);
		}
	}
}

static void placech(unsigned char c, int x, int y, int attr) {
	unsigned short *where;
	unsigned att = attr << 8;
	where = (unsigned short*)(framebuffer) + (y * 80 + x);
	*where = c | att;
}

static void vga_update_message(char * c, int line) {
	int x = 1;
	int y = 1 + line_offset;
	if (line == 0) {
		line_offset++;
	}
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

static void vga_clear_screen(void) {
	for (int y = 0; y < 24; ++y) {
		for (int x = 0; x < 80; ++x) {
			placech(' ', x, y, 0); /* Clear */
		}
	}
}

static void check_framebuffer(void) {
	framebuffer_fd = open("/dev/fb0", O_RDONLY);
	if (framebuffer_fd >= 0) {
		update_message = fb_update_message;
		clear_screen = fb_clear_screen;
	} else {
		framebuffer_fd = open("/dev/vga0", O_RDONLY);
		if (framebuffer_fd < 0) return;
		update_message = vga_update_message;
		clear_screen = vga_clear_screen;
	}
	ioctl(framebuffer_fd, IO_VID_WIDTH,  &width);
	ioctl(framebuffer_fd, IO_VID_HEIGHT, &height);
	ioctl(framebuffer_fd, IO_VID_DEPTH,  &depth);
	ioctl(framebuffer_fd, IO_VID_ADDR,   &framebuffer);
	ioctl(framebuffer_fd, IO_VID_SIGNAL, NULL);
}

static FILE * pex_endpoint = NULL;
static void open_socket(void) {
	pex_endpoint = pex_bind("splash");
	if (!pex_endpoint) exit(1);
}

static void say_hello(void) {
	/* Get our release version */
	struct utsname u;
	uname(&u);
	/* Strip git tag */
	char * tmp = strstr(u.release, "-");
	if (tmp) *tmp = '\0';
	/* Setup hello message */
	char hello_msg[512];
	snprintf(hello_msg, 511, "ToaruOS %s is starting up...", u.release);
	/* Add it to the log */
	update_message(hello_msg, 0);
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
		say_hello();

		while (1) {
			pex_packet_t * p = calloc(PACKET_SIZE, 1);
			pex_listen(pex_endpoint, p);

			if (p->size < 4)  continue; /* Ignore blank messages, erroneous line feeds, etc. */
			if (p->size > 80) continue; /* Ignore overly large messages */

			if (!strncmp((char*)p->data, "!quit", 5)) {
				/* Use the special message !quit to exit. */
				fclose(pex_endpoint);
				return 0;
			} else if (p->data[0] == ':') {
				/* Make sure message is nil terminated (it should be...) */
				char * tmp = malloc(p->size + 1);
				memcpy(tmp, p->data, p->size);
				tmp[p->size] = '\0';
				update_message(tmp+1, 1);
				free(tmp);
			} else {
				/* Make sure message is nil terminated (it should be...) */
				char * tmp = malloc(p->size + 1);
				memcpy(tmp, p->data, p->size);
				tmp[p->size] = '\0';
				update_message(tmp, 0);
				update_message("", 1);
				free(tmp);
			}
		}
	}

	return 0;
}
