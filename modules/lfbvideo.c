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

uint16_t lfb_resolution_x = 0;
uint16_t lfb_resolution_y = 0;
uint16_t lfb_resolution_b = 0;
uint32_t lfb_resolution_s = 0;

/* BOCHS / QEMU VBE Driver */
static void graphics_install_bochs(uint16_t, uint16_t);
static void bochs_set_y_offset(uint16_t y);
static uint16_t bochs_current_scroll(void);

static pid_t display_change_recipient = 0;

void lfb_set_resolution(uint16_t x, uint16_t y);

/*
 * Address of the linear frame buffer.
 * This can move, so it's a pointer instead of
 * #define.
 */
uint8_t * lfb_vid_memory = (uint8_t *)0xE0000000;

struct vid_size {
	uint32_t width;
	uint32_t height;
};

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
		case IO_VID_SIGNAL:
			/* ioctl to register for a signal (vid device change? idk) on display change */
			display_change_recipient = getpid();
			return 0;
		case IO_VID_SET:
			validate(argp);
			lfb_set_resolution(((struct vid_size *)argp)->width, ((struct vid_size *)argp)->height);
			return 0;
		case IO_VID_STRIDE:
			*((size_t *)argp) = lfb_resolution_s;
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
	uint32_t * cell = &disp[y * (lfb_resolution_s / 4) + x];
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
			uint32_t * cell = &disp[y * (lfb_resolution_s / 4) + x];

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
	fnode->mask    = 0660;
	fnode->ioctl   = ioctl_vid;
	return fnode;
}

static void finalize_graphics(uint16_t x, uint16_t y, uint16_t b, uint32_t s) {
	lfb_resolution_x = x;
	lfb_resolution_s = s;
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

static void (*lfb_resolution_impl)(uint16_t,uint16_t) = NULL;

void lfb_set_resolution(uint16_t x, uint16_t y) {

	if (lfb_resolution_impl) {
		lfb_resolution_impl(x,y);
		if (display_change_recipient) {
			send_signal(display_change_recipient, SIGWINEVENT);
			debug_print(WARNING, "Telling %d to SIGWINEVENT", display_change_recipient);
		}
	}

}

static void res_change_bochs(uint16_t x, uint16_t y) {

	outports(0x1CE, 0x04);
	outports(0x1CF, 0x00);
	/* Uh oh, here we go. */
	outports(0x1CE, 0x01);
	outports(0x1CF, x);
	/* Set Y resolution to 768 */
	outports(0x1CE, 0x02);
	outports(0x1CF, y);
	/* Set bpp to 32 */
	outports(0x1CE, 0x03);
	outports(0x1CF, PREFERRED_B);
	/* Set Virtual Height to stuff */
	outports(0x1CE, 0x07);
	outports(0x1CF, PREFERRED_VY);
	/* Turn it back on */
	outports(0x1CE, 0x04);
	outports(0x1CF, 0x41);

	/* Read X to see if it's something else */
	outports(0x1CE, 0x01);
	uint16_t new_x = inports(0x1CF);
	if (x != new_x) {
		x = new_x;
	}

	lfb_resolution_x = x;
	lfb_resolution_s = x * 4;
	lfb_resolution_y = y;
}


static void graphics_install_bochs(uint16_t resolution_x, uint16_t resolution_y) {
	uint32_t vid_memsize;
	debug_print(NOTICE, "Setting up BOCHS/QEMU graphics controller...");

	outports(0x1CE, 0x00);
	uint16_t i = inports(0x1CF);
	if (i < 0xB0C0 || i > 0xB0C6) {
		return;
	}
	outports(0x1CF, 0xB0C4);
	i = inports(0x1CF);
	res_change_bochs(resolution_x, resolution_y);
	resolution_x = lfb_resolution_x; /* may have changed */

	pci_scan(bochs_scan_pci, -1, &lfb_vid_memory);

	lfb_resolution_impl = &res_change_bochs;

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
	outports(0x1CE, 0x0a);
	i = inports(0x1CF);
	if (i > 1) {
		vid_memsize = (uint32_t)i * 64 * 1024;
	} else {
		vid_memsize = inportl(0x1CF);
	}
	debug_print(WARNING, "Video memory size is 0x%x", vid_memsize);
	for (uintptr_t i = (uintptr_t)lfb_vid_memory; i <= (uintptr_t)lfb_vid_memory + vid_memsize; i += 0x1000) {
		dma_frame(get_page(i, 1, kernel_directory), 0, 1, i);
	}
	finalize_graphics(resolution_x, resolution_y, PREFERRED_B, resolution_x * 4);
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
	finalize_graphics(w,h,b,w*4);

}

#define SVGA_IO_BASE (vmware_io)
#define SVGA_IO_MUL 1
#define SVGA_INDEX_PORT 0
#define SVGA_VALUE_PORT 1

#define SVGA_REG_ID 0
#define SVGA_REG_ENABLE 1
#define SVGA_REG_WIDTH 2
#define SVGA_REG_HEIGHT 3
#define SVGA_REG_BITS_PER_PIXEL 7
#define SVGA_REG_BYTES_PER_LINE 12
#define SVGA_REG_FB_START 13

static uint32_t vmware_io = 0;

static void vmware_scan_pci(uint32_t device, uint16_t v, uint16_t d, void * extra) {
	if ((v == 0x15ad && d == 0x0405)) {
		uintptr_t t = pci_read_field(device, PCI_BAR0, 4);
		if (t > 0) {
			*((uint8_t **)extra) = (uint8_t *)(t & 0xFFFFFFF0);
		}
	}
}

static void vmware_write(int reg, int value) {
	outportl(SVGA_IO_MUL * SVGA_INDEX_PORT + SVGA_IO_BASE, reg);
	outportl(SVGA_IO_MUL * SVGA_VALUE_PORT + SVGA_IO_BASE, value);
}

static uint32_t vmware_read(int reg) {
	outportl(SVGA_IO_MUL * SVGA_INDEX_PORT + SVGA_IO_BASE, reg);
	return inportl(SVGA_IO_MUL * SVGA_VALUE_PORT + SVGA_IO_BASE);
}

static void vmware_set_mode(uint16_t w, uint16_t h) {
	vmware_write(SVGA_REG_ENABLE, 0);
	vmware_write(SVGA_REG_ID, 0);
	vmware_write(SVGA_REG_WIDTH, w);
	vmware_write(SVGA_REG_HEIGHT, h);
	vmware_write(SVGA_REG_BITS_PER_PIXEL, 32);
	vmware_write(SVGA_REG_ENABLE, 1);

	uint32_t bpl = vmware_read(SVGA_REG_BYTES_PER_LINE);

	lfb_resolution_x = w;
	lfb_resolution_s = bpl;
	lfb_resolution_y = h;

}

static void graphics_install_vmware(uint16_t w, uint16_t h) {
	debug_print(WARNING, "Please note that the `vmware` display driver is experimental.");
	pci_scan(vmware_scan_pci, -1, &vmware_io);

	if (!vmware_io) {
		debug_print(ERROR, "No vmware device found?");
		return;
	} else {
		debug_print(WARNING, "vmware io base: 0x%x", vmware_io);
	}

	vmware_write(SVGA_REG_ID, 0);
	vmware_write(SVGA_REG_WIDTH, w);
	vmware_write(SVGA_REG_HEIGHT, h);
	vmware_write(SVGA_REG_BITS_PER_PIXEL, 32);
	vmware_write(SVGA_REG_ENABLE, 1);

	uint32_t bpl = vmware_read(SVGA_REG_BYTES_PER_LINE);

	lfb_resolution_impl = &vmware_set_mode;

	uint32_t fb_addr = vmware_read(SVGA_REG_FB_START);
	debug_print(WARNING, "vmware fb address: 0x%x", fb_addr);

	uint32_t fb_size = vmware_read(15);

	debug_print(WARNING, "vmware fb size: 0x%x", fb_size);

	lfb_vid_memory = (uint8_t *)fb_addr;

	uintptr_t fb_offset = (uintptr_t)lfb_vid_memory;
	for (uintptr_t i = fb_offset; i <= fb_offset + fb_size; i += 0x1000) {
		dma_frame(get_page(i, 1, kernel_directory), 0, 1, i);
	}

	finalize_graphics(w,h,32,bpl);
}

struct disp_mode {
	int16_t x;
	int16_t y;
	int set;
};

static void auto_scan_pci(uint32_t device, uint16_t v, uint16_t d, void * extra) {
	struct disp_mode * mode = extra;
	if (mode->set) return;
	if ((v == 0x1234 && d == 0x1111) ||
	    (v == 0x80EE && d == 0xBEEF)) {
		mode->set = 1;
		graphics_install_bochs(mode->x, mode->y);
	} else if ((v == 0x15ad && d == 0x0405)) {
		mode->set = 1;
		graphics_install_vmware(mode->x, mode->y);
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

		if (!strcmp(argv[0], "auto")) {
			/* Attempt autodetection */
			debug_print(WARNING, "Autodetect is in beta, this may not work.");
			struct disp_mode mode = {x,y,0};
			pci_scan(auto_scan_pci, -1, &mode);
			if (!mode.set) {
				graphics_install_preset(x,y);
			}
		} else if (!strcmp(argv[0], "qemu")) {
			/* Bochs / Qemu Video Device */
			graphics_install_bochs(x,y);
		} else if (!strcmp(argv[0],"preset")) {
			graphics_install_preset(x,y);
		} else if (!strcmp(argv[0],"vmware")) {
			graphics_install_vmware(x,y);
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
