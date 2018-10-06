/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014-2018 K. Lange
 *
 * Generic linear framebuffer driver.
 *
 * Supports several cases:
 *  - Bochs/QEMU/VirtualBox "Bochs VBE" with modesetting.
 *  - VMware SVGA with modesetting.
 *  - Linear framebuffers set by the bootloader with no modesetting.
 */

#include <kernel/system.h>
#include <kernel/fs.h>
#include <kernel/printf.h>
#include <kernel/types.h>
#include <kernel/logging.h>
#include <kernel/pci.h>
#include <kernel/boot.h>
#include <kernel/args.h>
#include <kernel/tokenize.h>
#include <kernel/module.h>
#include <kernel/video.h>
#include <kernel/mod/procfs.h>

#define PREFERRED_W  1024
#define PREFERRED_H  768
#define PREFERRED_VY 4096
#define PREFERRED_B 32

/* Exported to other modules */
uint16_t lfb_resolution_x = 0;
uint16_t lfb_resolution_y = 0;
uint16_t lfb_resolution_b = 0;
uint32_t lfb_resolution_s = 0;
uint8_t * lfb_vid_memory = (uint8_t *)0xE0000000;
const char * lfb_driver_name = NULL;

/* Where to send display size change signals */
static pid_t display_change_recipient = 0;

/* Driver-specific modesetting function */
static void (*lfb_resolution_impl)(uint16_t,uint16_t) = NULL;

/* Called by ioctl on /dev/fb0 */
void lfb_set_resolution(uint16_t x, uint16_t y) {
	if (lfb_resolution_impl) {
		lfb_resolution_impl(x,y);
		if (display_change_recipient) {
			send_signal(display_change_recipient, SIGWINEVENT, 1);
			debug_print(WARNING, "Telling %d to SIGWINEVENT", display_change_recipient);
		}
	}
}

/**
 * Framebuffer control ioctls.
 * Used by the compositor to get display sizes and by the
 * resolution changer to initiate modesetting.
 */
static int ioctl_vid(fs_node_t * node, int request, void * argp) {
	switch (request) {
		case IO_VID_WIDTH:
			/* Get framebuffer width */
			validate(argp);
			*((size_t *)argp) = lfb_resolution_x;
			return 0;
		case IO_VID_HEIGHT:
			/* Get framebuffer height */
			validate(argp);
			*((size_t *)argp) = lfb_resolution_y;
			return 0;
		case IO_VID_DEPTH:
			/* Get framebuffer bit depth */
			validate(argp);
			*((size_t *)argp) = lfb_resolution_b;
			return 0;
		case IO_VID_STRIDE:
			/* Get framebuffer scanline stride */
			validate(argp);
			*((size_t *)argp) = lfb_resolution_s;
			return 0;
		case IO_VID_ADDR:
			/* Get framebuffer address - TODO: map the framebuffer? */
			validate(argp);
			*((uintptr_t *)argp) = (uintptr_t)lfb_vid_memory;
			return 0;
		case IO_VID_SIGNAL:
			/* ioctl to register for a signal (vid device change? idk) on display change */
			display_change_recipient = getpid();
			return 0;
		case IO_VID_SET:
			/* Initiate mode setting */
			validate(argp);
			lfb_set_resolution(((struct vid_size *)argp)->width, ((struct vid_size *)argp)->height);
			return 0;
		case IO_VID_DRIVER:
			validate(argp);
			memcpy(argp, lfb_driver_name, strlen(lfb_driver_name));
			return 0;
		default:
			return -EINVAL;
	}
}

/* Framebuffer device file initializer */
static fs_node_t * lfb_video_device_create(void /* TODO */) {
	fs_node_t * fnode = malloc(sizeof(fs_node_t));
	memset(fnode, 0x00, sizeof(fs_node_t));
	sprintf(fnode->name, "fb0"); /* TODO */
	fnode->length  = lfb_resolution_s * lfb_resolution_y; /* Size is framebuffer size in bytes */
	fnode->flags   = FS_BLOCKDEVICE; /* Framebuffers are block devices */
	fnode->mask    = 0660; /* Only accessible to root user/group */
	fnode->ioctl   = ioctl_vid; /* control function defined above */
	return fnode;
}

/**
 * Framebuffer fatal error presentation.
 *
 * This is called by a kernel hook to render fatal error messages
 * (panic / oops / bsod) to the graphraical framebuffer. Mostly,
 * that means the "out of memory" error. Bescause this is a fatal
 * error condition, we don't care much about speed, so we can do
 * silly things like ready from the framebuffer, which we do to
 * produce a vignetting and desaturation effect.
 */
static int vignette_at(int x, int y) {
	int amount = 0;
	int level = 100;
	if (x < level) amount += (level - x);
	if (x > lfb_resolution_x - level) amount += (level - (lfb_resolution_x - x));
	if (y < level) amount += (level - y);
	if (y > lfb_resolution_y - level) amount += (level - (lfb_resolution_y - y));
	return amount;
}

#include "../apps/terminal-font.h"

/* XXX Why is this not defined in the font header...  */
#define char_height 20
#define char_width  9

/* Set point in framebuffer */
static void set_point(int x, int y, uint32_t value) {
	uint32_t * disp = (uint32_t *)lfb_vid_memory;
	uint32_t * cell = &disp[y * (lfb_resolution_s / 4) + x];
	*cell = value;
}

/* Draw text on framebuffer */
static void write_char(int x, int y, int val, uint32_t color) {
	if (val > 128) {
		val = 4;
	}
	uint16_t * c = large_font[val];
	for (uint8_t i = 0; i < char_height; ++i) {
		for (uint8_t j = 0; j < char_width; ++j) {
			if (c[i] & (1 << (15-j))) {
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

static uint32_t framebuffer_func(fs_node_t * node, uint32_t offset, uint32_t size, uint8_t * buffer) {
	char * buf = malloc(4096);

	if (lfb_driver_name) {
		sprintf(buf,
			"Driver:\t%s\n"
			"XRes:\t%d\n"
			"YRes:\t%d\n"
			"BitsPerPixel:\t%d\n"
			"Stride:\t%d\n"
			"Address:\t0x%x\n",
			lfb_driver_name,
			lfb_resolution_x,
			lfb_resolution_y,
			lfb_resolution_b,
			lfb_resolution_s,
			lfb_vid_memory);
	} else {
		sprintf(buf, "Driver:\tnone\n");
	}

	size_t _bsize = strlen(buf);
	if (offset > _bsize) {
		free(buf);
		return 0;
	}
	if (size > _bsize - offset) size = _bsize - offset;

	memcpy(buffer, buf + offset, size);
	free(buf);
	return size;
}

static struct procfs_entry framebuffer_entry = {
	0,
	"framebuffer",
	framebuffer_func,
};

/* Install framebuffer device */
static void finalize_graphics(const char * driver) {
	lfb_driver_name = driver;
	fs_node_t * fb_device = lfb_video_device_create();
	vfs_mount("/dev/fb0", fb_device);
	debug_video_crash = lfb_video_panic;

	int (*procfs_install)(struct procfs_entry *) = (int (*)(struct procfs_entry *))(uintptr_t)hashmap_get(modules_get_symbols(),"procfs_install");

	if (procfs_install) {
		procfs_install(&framebuffer_entry);
	}
}

/* Bochs support {{{ */
static void bochs_scan_pci(uint32_t device, uint16_t v, uint16_t d, void * extra) {
	if ((v == 0x1234 && d == 0x1111) ||
	    (v == 0x80EE && d == 0xBEEF) ||
	    (v == 0x10de && d == 0x0a20))  {
		uintptr_t t = pci_read_field(device, PCI_BAR0, 4);
		if (t > 0) {
			*((uint8_t **)extra) = (uint8_t *)(t & 0xFFFFFFF0);
		}
	}
}

static void bochs_set_resolution(uint16_t x, uint16_t y) {
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
	lfb_resolution_b = 32;
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
	bochs_set_resolution(resolution_x, resolution_y);
	resolution_x = lfb_resolution_x; /* may have changed */

	pci_scan(bochs_scan_pci, -1, &lfb_vid_memory);

	lfb_resolution_impl = &bochs_set_resolution;

	if (!lfb_vid_memory) {
		debug_print(ERROR, "Failed to locate video memory.");
		return;
	}

	/* Enable the higher memory */
	uintptr_t fb_offset = (uintptr_t)lfb_vid_memory;
	for (uintptr_t i = fb_offset; i <= fb_offset + 0xFF0000; i += 0x1000) {
		page_t * p = get_page(i, 1, kernel_directory);
		dma_frame(p, 0, 1, i);
		p->pat = 1;
		p->writethrough = 1;
		p->cachedisable = 1;
	}

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

	finalize_graphics("bochs");
}

static void graphics_install_preset(uint16_t w, uint16_t h) {
	if (!(mboot_ptr && (mboot_ptr->flags & (1 << 12)))) {
		debug_print(ERROR, "Failed to locate preset video memory - missing multiboot header.");
		return;
	}

	/* Extract framebuffer information from multiboot */
	lfb_vid_memory = (void *)mboot_ptr->framebuffer_addr;
	lfb_resolution_x = mboot_ptr->framebuffer_width;
	lfb_resolution_y = mboot_ptr->framebuffer_height;
	lfb_resolution_s = mboot_ptr->framebuffer_pitch;
	lfb_resolution_b = 32;

	debug_print(WARNING, "Mode was set by bootloader: %dx%d bpp should be 32, framebuffer is at 0x%x", w, h, (uintptr_t)lfb_vid_memory);

	for (uintptr_t i = (uintptr_t)lfb_vid_memory; i <= (uintptr_t)lfb_vid_memory + w * h * 4; i += 0x1000) {
		page_t * p = get_page(i, 1, kernel_directory);
		dma_frame(p, 0, 1, i);
		p->pat = 1;
		p->writethrough = 1;
		p->cachedisable = 1;
	}
	finalize_graphics("preset");
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

static void vmware_set_resolution(uint16_t w, uint16_t h) {
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
	lfb_resolution_b = 32;
}

static void graphics_install_vmware(uint16_t w, uint16_t h) {
	pci_scan(vmware_scan_pci, -1, &vmware_io);

	if (!vmware_io) {
		debug_print(ERROR, "No vmware device found?");
		return;
	} else {
		debug_print(WARNING, "vmware io base: 0x%x", vmware_io);
	}

	vmware_set_resolution(w,h);
	lfb_resolution_impl = &vmware_set_resolution;

	uint32_t fb_addr = vmware_read(SVGA_REG_FB_START);
	debug_print(WARNING, "vmware fb address: 0x%x", fb_addr);

	uint32_t fb_size = vmware_read(15);

	debug_print(WARNING, "vmware fb size: 0x%x", fb_size);

	lfb_vid_memory = (uint8_t *)fb_addr;

	uintptr_t fb_offset = (uintptr_t)lfb_vid_memory;
	for (uintptr_t i = fb_offset; i <= fb_offset + fb_size; i += 0x1000) {
		page_t * p = get_page(i, 1, kernel_directory);
		dma_frame(p, 0, 1, i);
		p->pat = 1;
		p->writethrough = 1;
		p->cachedisable = 1;
	}

	finalize_graphics("vmware");
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
	    (v == 0x80EE && d == 0xBEEF) ||
	    (v == 0x10de && d == 0x0a20))  {
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
			x = PREFERRED_W;
			y = PREFERRED_H;
		} else {
			x = atoi(argv[1]);
			y = atoi(argv[2]);
		}

		if (!strcmp(argv[0], "auto")) {
			/* Attempt autodetection */
			debug_print(NOTICE, "Automatically detecting display driver...");
			struct disp_mode mode = {x,y,0};
			pci_scan(auto_scan_pci, -1, &mode);
			if (!mode.set) {
				graphics_install_preset(x,y);
			}
		} else if (!strcmp(argv[0], "qemu")) {
			/* Bochs / Qemu Video Device */
			graphics_install_bochs(x,y);
		} else if (!strcmp(argv[0],"vmware")) {
			/* VMware SVGA */
			graphics_install_vmware(x,y);
		} else if (!strcmp(argv[0],"preset")) {
			/* Set by bootloader (UEFI) */
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
