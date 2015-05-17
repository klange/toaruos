/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014 Kevin Lange
 *
 * Bochs VBE / QEMU vga=std Graphics Driver
 */

#include <system.h>
#include <fs.h>
#include <printf.h>
#include <types.h>
#include <logging.h>
#include <pci.h>
#include <boot.h>
#include <args.h>
#include <tokenize.h>
#include <module.h>
#include <video.h>

#include "../userspace/gui/terminal/terminal-font.h"

#define PREFERRED_VY 4096
#define PREFERRED_B 32
/* Generic (pre-set, 32-bit, linear frame buffer) */
static void graphics_install_preset(uint16_t, uint16_t);

static uint16_t lfb_resolution_x = 0;
static uint16_t lfb_resolution_y = 0;
static uint16_t lfb_resolution_b = 0;

/* BOCHS / QEMU VBE Driver */
static void graphics_install_bochs(uint16_t, uint16_t);
static void bochs_set_y_offset(uint16_t y);
static uint16_t bochs_current_scroll(void);

/*
 * Address of the linear frame buffer.
 * This can move, so it's a pointer instead of
 * #define.
 */
static uint8_t * lfb_vid_memory = (uint8_t *)0xE0000000;

static int ioctl_vid(fs_node_t * node, int request, void * argp) {
	/* TODO: Make this actually support multiple video devices */

	switch (request) {
		case IO_VID_WIDTH:
			validate(argp);
			*((size_t *)argp) = lfb_resolution_x;
			return 0;
		case IO_VID_HEIGHT:
			validate(argp);
			*((size_t *)argp) = lfb_resolution_y;
			return 0;
		case IO_VID_DEPTH:
			validate(argp);
			*((size_t *)argp) = lfb_resolution_b;
			return 0;
		case IO_VID_ADDR:
			validate(argp);
			*((uintptr_t *)argp) = (uintptr_t)lfb_vid_memory;
			return 0;
		default:
			return -1; /* TODO EINV... something or other */
	}
}

static int vignette_at(int x, int y) {
	int amount = 0;
	int level = 100;
	if (x < level) amount += (level - x);
	if (x > lfb_resolution_x - level) amount += (level - (lfb_resolution_x - x));
	if (y < level) amount += (level - y);
	if (y > lfb_resolution_y - level) amount += (level - (lfb_resolution_y - y));
	return amount;
}

#define char_height 12
#define char_width  8

static void set_point(int x, int y, uint32_t value) {
	uint32_t * disp = (uint32_t *)lfb_vid_memory;
	uint32_t * cell = &disp[y * lfb_resolution_x + x];
	*cell = value;
}

static void write_char(int x, int y, int val, uint32_t color) {
	if (val > 128) {
		val = 4;
	}
	uint8_t * c = number_font[val];
	for (uint8_t i = 0; i < char_height; ++i) {
		for (uint8_t j = 0; j < char_width; ++j) {
			if (c[i] & (1 << (8-j))) {
				set_point(x+j,y+i,color);
			}
		}
	}
}

#define _RED(color) ((color & 0x00FF0000) / 0x10000)
#define _GRE(color) ((color & 0x0000FF00) / 0x100)
#define _BLU(color) ((color & 0x000000FF) / 0x1)
#define _ALP(color) ((color & 0xFF000000) / 0x1000000)
static void lfb_video_panic(char ** msgs) {
	/* Desaturate the display */
	uint32_t * disp = (uint32_t *)lfb_vid_memory;
	for (int y = 0; y < lfb_resolution_y; y++) {
		for (int x = 0; x < lfb_resolution_x; x++) {
			uint32_t * cell = &disp[y * lfb_resolution_x + x];

			int r = _RED(*cell);
			int g = _GRE(*cell);
			int b = _BLU(*cell);

			int l = 3 * r + 6 * g + 1 * b;
			r = (l) / 10;
			g = (l) / 10;
			b = (l) / 10;

			r = r > 255 ? 255 : r;
			g = g > 255 ? 255 : g;
			b = b > 255 ? 255 : b;

			int amount = vignette_at(x,y);
			r = (r - amount < 0) ? 0 : r - amount;
			g = (g - amount < 0) ? 0 : g - amount;
			b = (b - amount < 0) ? 0 : b - amount;

			*cell = 0xFF000000 + ((0xFF & r) * 0x10000) + ((0xFF & g) * 0x100) + ((0xFF & b) * 0x1); 
		}
	}

	/* Now print the message, divided on line feeds, into the center of the screen */
	int num_entries = 0;
	for (char ** m = msgs; *m; m++, num_entries++);
	int y = (lfb_resolution_y - (num_entries * char_height)) / 2;
	for (char ** message = msgs; *message; message++) {
		int x = (lfb_resolution_x - (strlen(*message) * char_width)) / 2;
		for (char * c = *message; *c; c++) {
			write_char(x+1, y+1, *c, 0xFF000000);
			write_char(x, y, *c, 0xFFFF0000);
			x += char_width;
		}
		y += char_height;
	}

}

static fs_node_t * lfb_video_device_create(void /* TODO */) {
	fs_node_t * fnode = malloc(sizeof(fs_node_t));
	memset(fnode, 0x00, sizeof(fs_node_t));
	sprintf(fnode->name, "fb0"); /* TODO */
	fnode->length  = lfb_resolution_x * lfb_resolution_y * (lfb_resolution_b / 8);
	fnode->flags   = FS_BLOCKDEVICE;
	fnode->ioctl   = ioctl_vid;
	return fnode;
}

static void finalize_graphics(uint16_t x, uint16_t y, uint16_t b) {
	lfb_resolution_x = x;
	lfb_resolution_y = y;
	lfb_resolution_b = b;
	fs_node_t * fb_device = lfb_video_device_create();
	vfs_mount("/dev/fb0", fb_device);
	debug_video_crash = lfb_video_panic;
}

/* Bochs support {{{ */
static uintptr_t current_scroll = 0;

static void bochs_set_y_offset(uint16_t y) {
	outports(0x1CE, 0x9);
	outports(0x1CF, y);
	current_scroll = y;
}

static uint16_t bochs_current_scroll(void) {
	return current_scroll;
}

static void bochs_scan_pci(uint32_t device, uint16_t v, uint16_t d, void * extra) {
	if ((v == 0x1234 && d == 0x1111) ||
	    (v == 0x80EE && d == 0xBEEF)) {
		uintptr_t t = pci_read_field(device, PCI_BAR0, 4);
		if (t > 0) {
			*((uint8_t **)extra) = (uint8_t *)(t & 0xFFFFFFF0);
		}
	}
}

static void graphics_install_bochs(uint16_t resolution_x, uint16_t resolution_y) {
	debug_print(NOTICE, "Setting up BOCHS/QEMU graphics controller...");

	outports(0x1CE, 0x00);
	uint16_t i = inports(0x1CF);
	if (i < 0xB0C0 || i > 0xB0C6) {
		return;
	}
	outports(0x1CF, 0xB0C4);
	i = inports(0x1CF);
	/* Disable VBE */
	outports(0x1CE, 0x04);
	outports(0x1CF, 0x00);
	/* Set X resolution to 1024 */
	outports(0x1CE, 0x01);
	outports(0x1CF, resolution_x);
	/* Set Y resolution to 768 */
	outports(0x1CE, 0x02);
	outports(0x1CF, resolution_y);
	/* Set bpp to 32 */
	outports(0x1CE, 0x03);
	outports(0x1CF, PREFERRED_B);
	/* Set Virtual Height to stuff */
	outports(0x1CE, 0x07);
	outports(0x1CF, PREFERRED_VY);
	/* Re-enable VBE */
	outports(0x1CE, 0x04);
	outports(0x1CF, 0x41);

	pci_scan(bochs_scan_pci, -1, &lfb_vid_memory);

	if (lfb_vid_memory) {
		/* Enable the higher memory */
		uintptr_t fb_offset = (uintptr_t)lfb_vid_memory;
		for (uintptr_t i = fb_offset; i <= fb_offset + 0xFF0000; i += 0x1000) {
			dma_frame(get_page(i, 1, kernel_directory), 0, 1, i);
		}

		goto mem_found;
	} else {
		/* XXX: Massive hack */

		uint32_t * text_vid_mem = (uint32_t *)0xA0000;
		text_vid_mem[0] = 0xA5ADFACE;

		for (uintptr_t fb_offset = 0xE0000000; fb_offset < 0xFF000000; fb_offset += 0x01000000) {
			/* Enable the higher memory */
			for (uintptr_t i = fb_offset; i <= fb_offset + 0xFF0000; i += 0x1000) {
				dma_frame(get_page(i, 1, kernel_directory), 0, 1, i);
			}

			/* Go find it */
			for (uintptr_t x = fb_offset; x < fb_offset + 0xFF0000; x += 0x1000) {
				if (((uintptr_t *)x)[0] == 0xA5ADFACE) {
					lfb_vid_memory = (uint8_t *)x;
					goto mem_found;
				}
			}
		}
	}

mem_found:
	if (lfb_vid_memory + 4 * resolution_x * resolution_y > lfb_vid_memory + 0xFF0000) {
		for (uintptr_t i = (uintptr_t)lfb_vid_memory + 0xFF1000; i <= (uintptr_t)lfb_vid_memory + 4 * resolution_x * resolution_y; i += 0x1000) {
			debug_print(WARNING, "Also mapping 0x%x", i);
			dma_frame(get_page(i, 1, kernel_directory), 0, 1, i);
		}
	}
	finalize_graphics(resolution_x, resolution_y, PREFERRED_B);
}

/* }}} end bochs support */

static void graphics_install_preset(uint16_t w, uint16_t h) {
	debug_print(NOTICE, "Graphics were pre-configured (thanks, bootloader!), locating video memory...");
	uint16_t b = 32; /* If you are 24 bit, go away, we really do not support you. */

	/* XXX: Massive hack */
	uint32_t * herp = (uint32_t *)0xA0000;
	herp[0] = 0xA5ADFACE;
	herp[1] = 0xFAF42943;

	if (lfb_vid_memory) {
		for (uintptr_t i = (uintptr_t)lfb_vid_memory; i <= (uintptr_t)lfb_vid_memory + 0xFF0000; i += 0x1000) {
			dma_frame(get_page(i, 1, kernel_directory), 0, 1, i);
		}
		if (((uintptr_t *)lfb_vid_memory)[0] == 0xA5ADFACE && ((uintptr_t *)lfb_vid_memory)[1] == 0xFAF42943) {
			debug_print(INFO, "Was able to locate video memory at 0x%x without dicking around.", lfb_vid_memory);
			goto mem_found;
		}
	}

	for (int i = 2; i < 1000; i += 2) {
		herp[i]   = 0xFF00FF00;
		herp[i+1] = 0x00FF00FF;
	}

	for (uintptr_t fb_offset = 0xE0000000; fb_offset < 0xFF000000; fb_offset += 0x01000000) {
		/* Enable the higher memory */
		for (uintptr_t i = fb_offset; i <= fb_offset + 0xFF0000; i += 0x1000) {
			dma_frame(get_page(i, 1, kernel_directory), 0, 1, i);
		}

		/* Go find it */
		for (uintptr_t x = fb_offset; x < fb_offset + 0xFF0000; x += 0x1000) {
			if (((uintptr_t *)x)[0] == 0xA5ADFACE && ((uintptr_t *)x)[1] == 0xFAF42943) {
				lfb_vid_memory = (uint8_t *)x;
				debug_print(INFO, "Had to futz around, but found video memory at 0x%x", lfb_vid_memory);
				goto mem_found;
			}
		}
	}

	for (int i = 2; i < 1000; i += 2) {
		herp[i]   = 0xFF00FF00;
		herp[i+1] = 0xFF00FF00;
	}

	debug_print(WARNING, "Failed to locate video memory. This could end poorly.");

mem_found:
	finalize_graphics(w,h,b);

	for (uint16_t y = 0; y < h; y++) {
		for (uint16_t x = 0; x < w; x++) {
			uint8_t f = y % 255;
			((uint32_t *)lfb_vid_memory)[x + y * w] = 0xFF000000 | (f * 0x10000) | (f * 0x100) | f;
		}
	}
}

static int init(void) {

	if (mboot_ptr->vbe_mode_info) {
		lfb_vid_memory = (uint8_t *)((vbe_info_t *)(mboot_ptr->vbe_mode_info))->physbase;
	}

	char * c;
	if ((c = args_value("vid"))) {
		debug_print(NOTICE, "Video mode requested: %s", c);

		char * arg = strdup(c);
		char * argv[10];
		int argc = tokenize(arg, ",", argv);

		uint16_t x, y;
		if (argc < 3) {
			x = 1024;
			y = 768;
		} else {
			x = atoi(argv[1]);
			y = atoi(argv[2]);
		}

		if (!strcmp(argv[0], "qemu")) {
			/* Bochs / Qemu Video Device */
			graphics_install_bochs(x,y);
		} else if (!strcmp(argv[0],"preset")) {
			graphics_install_preset(x,y);
		} else {
			debug_print(WARNING, "Unrecognized video adapter: %s", argv[0]);
		}

		free(arg);
	}

	return 0;
}

static int fini(void) {
	return 0;
}

MODULE_DEF(lfbvideo, init, fini);
