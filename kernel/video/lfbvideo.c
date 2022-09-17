/**
 * @file  kernel/video/lfbvideo.c
 * @brief Shared linear framebuffer drivers for qemu/bochs/vbox, vmware,
 *        and platforms that can modeset in the bootloader.
 *
 * Detects a small set of video devices that can be configured with simple
 * port writes and provides a runtime modesetting API for them. For other
 * devices, provides framebuffer mapping and resolution querying for modes
 * that have been preconfigured by the bootloader.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2012-2021 K. Lange
 */
#include <errno.h>
#include <kernel/types.h>
#include <kernel/vfs.h>
#include <kernel/printf.h>
#include <kernel/pci.h>
#include <kernel/video.h>
#include <kernel/process.h>
#include <kernel/string.h>
#include <kernel/signal.h>
#include <kernel/tokenize.h>
#include <kernel/multiboot.h>
#include <kernel/procfs.h>
#include <kernel/mmu.h>
#include <kernel/args.h>

/* FIXME: Not sure what to do with this; ifdef around it? */
#include <kernel/arch/x86_64/ports.h>

static int PREFERRED_W = 1440;
static int PREFERRED_H = 900;
#define PREFERRED_VY 4096
#define PREFERRED_B 32

/* Exported to other modules */
uint16_t lfb_resolution_x = 0;
uint16_t lfb_resolution_y = 0;
uint16_t lfb_resolution_b = 0;
uint32_t lfb_resolution_s = 0;
uint8_t * lfb_vid_memory = (uint8_t *)0xE0000000;
size_t lfb_memsize = 0xFF0000;
const char * lfb_driver_name = NULL;

uintptr_t lfb_qemu_mmio = 0;

fs_node_t * lfb_device = NULL;
static int lfb_init(const char * c);

/* Where to send display size change signals */
static pid_t display_change_recipient = 0;

/* Driver-specific modesetting function */
void (*lfb_resolution_impl)(uint16_t,uint16_t) = NULL;

/* Called by ioctl on /dev/fb0 */
void lfb_set_resolution(uint16_t x, uint16_t y) {
	if (lfb_resolution_impl) {
		lfb_resolution_impl(x,y);
		if (display_change_recipient) {
			send_signal(display_change_recipient, SIGWINEVENT, 1);
		}
	}
}

extern void ptr_validate(void * ptr, const char * syscall);
#define validate(o) ptr_validate(o,"ioctl")

/**
 * Framebuffer control ioctls.
 * Used by the compositor to get display sizes and by the
 * resolution changer to initiate modesetting.
 */
static int ioctl_vid(fs_node_t * node, unsigned long request, void * argp) {
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
			/* Map framebuffer into userspace process */
			validate(argp);
			{
				uintptr_t lfb_user_offset;
				if (*(uintptr_t*)argp == 0) {
					/* Pick an address and map it */
					lfb_user_offset = USER_DEVICE_MAP;
				} else {
					validate((void*)(*(uintptr_t*)argp));
					lfb_user_offset = *(uintptr_t*)argp;
				}
				for (uintptr_t i = 0; i < lfb_memsize; i += 0x1000) {
					union PML * page = mmu_get_page(lfb_user_offset + i, MMU_GET_MAKE);
					mmu_frame_map_address(page,MMU_FLAG_WRITABLE|MMU_FLAG_WC,((uintptr_t)(lfb_vid_memory) & 0xFFFFFFFF) + i);
				}
				*((uintptr_t *)argp) = lfb_user_offset;
			}
			return 0;
		case IO_VID_SIGNAL:
			/* ioctl to register for a signal (vid device change? idk) on display change */
			display_change_recipient = this_core->current_process->id;
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
		case IO_VID_REINIT:
			if (this_core->current_process->user != 0) {
				return -EPERM;
			}
			validate(argp);
			return lfb_init(argp);
		default:
			return -EINVAL;
	}
	return -EINVAL;
}

/* Framebuffer device file initializer */
static fs_node_t * lfb_video_device_create(void /* TODO */) {
	fs_node_t * fnode = malloc(sizeof(fs_node_t));
	memset(fnode, 0x00, sizeof(fs_node_t));
	snprintf(fnode->name, 100, "fb0"); /* TODO */
	fnode->length  = 0;
	fnode->flags   = FS_BLOCKDEVICE; /* Framebuffers are block devices */
	fnode->mask    = 0660; /* Only accessible to root user/group */
	fnode->ioctl   = ioctl_vid; /* control function defined above */
	return fnode;
}

static void framebuffer_func(fs_node_t * node) {
	if (lfb_driver_name) {
		procfs_printf(node,
			"Driver:\t%s\n"
			"XRes:\t%d\n"
			"YRes:\t%d\n"
			"BitsPerPixel:\t%d\n"
			"Stride:\t%d\n"
			"Address:\t%p\n",
			lfb_driver_name,
			lfb_resolution_x,
			lfb_resolution_y,
			lfb_resolution_b,
			lfb_resolution_s,
			lfb_vid_memory);
	} else {
		procfs_printf(node, "Driver:\tnone\n");
	}
}

static struct procfs_entry framebuffer_entry = {
	0,
	"framebuffer",
	framebuffer_func,
};

/* Install framebuffer device */
static void finalize_graphics(const char * driver) {
	lfb_driver_name = driver;
	lfb_device = lfb_video_device_create();
	lfb_device->length  = lfb_resolution_s * lfb_resolution_y; /* Size is framebuffer size in bytes */
	vfs_mount("/dev/fb0", lfb_device);

	procfs_install(&framebuffer_entry);
}

/* QEMU support {{{ */
static void qemu_scan_pci(uint32_t device, uint16_t v, uint16_t d, void * extra) {
	uintptr_t * output = extra;
	if ((v == 0x1234 && d == 0x1111) ||
	    (v == 0x10de && d == 0x0a20))  {
		#ifndef __x86_64__
		/* we have to configure this thing ourselves */
		uintptr_t t = 0x10000008;
		uintptr_t m = 0x11000000;
		pci_write_field(device, PCI_BAR0, 4, t); /* video memory? */
		pci_write_field(device, PCI_BAR2, 4, m); /* MMIO? */
		pci_write_field(device, PCI_COMMAND, 2, 4|2|1);
		#else
		uintptr_t t = pci_read_field(device, PCI_BAR0, 4);
		uintptr_t m = pci_read_field(device, PCI_BAR2, 4);
		#endif

		if (m == 0) {
			/* Shoot. */
			return;
		}

		if (t > 0) {
			output[0] = (uintptr_t)mmu_map_from_physical(t & 0xFFFFFFF0);
			output[1] = (uintptr_t)mmu_map_from_physical(m & 0xFFFFFFF0);
			/* Figure out size */
			pci_write_field(device, PCI_BAR0, 4, 0xFFFFFFFF);
			uint32_t s = pci_read_field(device, PCI_BAR0, 4);
			s = ~(s & -15) + 1;
			output[2] = s;
			pci_write_field(device, PCI_BAR0, 4, (uint32_t)t);
		}
	}
}

#define QEMU_MMIO_ID       0x00
#define QEMU_MMIO_FBWIDTH  0x02
#define QEMU_MMIO_FBHEIGHT 0x04
#define QEMU_MMIO_BPP      0x06
#define QEMU_MMIO_ENABLED  0x08
#define QEMU_MMIO_VIRTX    0x0c
#define QEMU_MMIO_VIRTY    0x0e

static void qemu_mmio_out(int off, uint16_t val) {
	*(volatile uint16_t*)(lfb_qemu_mmio + 0x500 + off) = val;
}

static uint16_t qemu_mmio_in(int off) {
	return *(volatile uint16_t*)(lfb_qemu_mmio + 0x500 + off);
}

static void qemu_set_resolution(uint16_t x, uint16_t y) {
	qemu_mmio_out(QEMU_MMIO_ENABLED,  0);
	qemu_mmio_out(QEMU_MMIO_FBWIDTH,  x);
	qemu_mmio_out(QEMU_MMIO_FBHEIGHT, y);
	qemu_mmio_out(QEMU_MMIO_BPP, PREFERRED_B);
	qemu_mmio_out(QEMU_MMIO_VIRTX, x);
	qemu_mmio_out(QEMU_MMIO_VIRTY, y);
	qemu_mmio_out(QEMU_MMIO_ENABLED,  0x41); /* 01h: enabled, 40h: lfb */

	/* unblank vga; this should only be necessary on secondary displays */
	*(volatile uint8_t*)(lfb_qemu_mmio + 0x400) = 0x20;

	lfb_resolution_x = qemu_mmio_in(QEMU_MMIO_FBWIDTH);
	lfb_resolution_y = qemu_mmio_in(QEMU_MMIO_FBHEIGHT);
	lfb_resolution_b = qemu_mmio_in(QEMU_MMIO_BPP);
	lfb_resolution_s = qemu_mmio_in(QEMU_MMIO_VIRTX) * (lfb_resolution_b / 8);
}

static void graphics_install_bochs(uint16_t resolution_x, uint16_t resolution_y);

static void graphics_install_qemu(uint16_t resolution_x, uint16_t resolution_y) {

	uintptr_t vals[3] = {0,0,0};
	pci_scan(qemu_scan_pci, -1, vals);

	if (!vals[0]) {
		/* Try port-IO interface */
		graphics_install_bochs(resolution_x, resolution_y);
		return;
	}

	lfb_vid_memory = (uint8_t*)vals[0];
	lfb_qemu_mmio = vals[1];
	lfb_memsize    = vals[2];

	uint16_t i = qemu_mmio_in(QEMU_MMIO_ID);
	if (i < 0xB0C0 || i > 0xB0C6) return; /* Unsupported qemu device. */
	qemu_mmio_out(QEMU_MMIO_ID, 0xB0C4); /* We speak ver. 4 */

	qemu_set_resolution(resolution_x, resolution_y);
	resolution_x = lfb_resolution_x; /* may have changed */

	lfb_resolution_impl = &qemu_set_resolution;

	if (!lfb_vid_memory) {
		printf("failed to locate video memory\n");
		return;
	}

	finalize_graphics("qemu");
}

/* VirtualBox implements the portio-based interface, but not the MMIO one. */
static void bochs_scan_pci(uint32_t device, uint16_t v, uint16_t d, void * extra) {
	if ((v == 0x80EE && d == 0xBEEF) || (v == 0x1234 && d == 0x1111)) {
		uintptr_t t = pci_read_field(device, PCI_BAR0, 4);
		if (t > 0) {
			*((uint8_t **)extra) = mmu_map_from_physical(t & 0xFFFFFFF0);
		}
	}
}

static void bochs_set_resolution(uint16_t x, uint16_t y) {
	outports(0x1CE, 0x04);
	outports(0x1CF, 0x00);
	outports(0x1CE, 0x01);
	outports(0x1CF, x);
	outports(0x1CE, 0x02);
	outports(0x1CF, y);
	outports(0x1CE, 0x03);
	outports(0x1CF, PREFERRED_B);
	outports(0x1CE, 0x07);
	outports(0x1CF, PREFERRED_VY);
	outports(0x1CE, 0x04);
	outports(0x1CF, 0x41);
	outports(0x1CE, 0x01);
	x = inports(0x1CF);
	lfb_resolution_x = x;
	lfb_resolution_s = x * 4;
	lfb_resolution_y = y;
	lfb_resolution_b = 32;
}

static void graphics_install_bochs(uint16_t resolution_x, uint16_t resolution_y) {
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
		printf("failed to locate video memory\n");
		return;
	}
	outports(0x1CE, 0x0a);
	i = inports(0x1CF);
	if (i > 1) {
		lfb_memsize = (uint32_t)i * 64 * 1024;
	} else {
		lfb_memsize = inportl(0x1CF);
	}
	finalize_graphics("bochs");
}

extern void arch_framebuffer_initialize(void);

static void graphics_install_preset(uint16_t w, uint16_t h) {
	/* Make sure memsize is actually big enough */
	size_t minsize = lfb_resolution_s * lfb_resolution_y * 4;
	if (lfb_memsize < minsize) lfb_memsize = minsize;

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

	lfb_memsize = vmware_read(15);
}

static void graphics_install_vmware(uint16_t w, uint16_t h) {
	pci_scan(vmware_scan_pci, -1, &vmware_io);

	if (!vmware_io) {
		printf("vmware video, but no device found?\n");
		return;
	} else {
		printf("vmware io base: %p\n", (void*)(uintptr_t)vmware_io);
	}

	vmware_set_resolution(w,h);
	lfb_resolution_impl = &vmware_set_resolution;

	uintptr_t fb_addr = vmware_read(SVGA_REG_FB_START);
	printf("vmware fb address: %p\n", (void*)fb_addr);

	lfb_memsize = vmware_read(15);

	printf("vmware fb size: 0x%lx\n", lfb_memsize);

	lfb_vid_memory = mmu_map_from_physical(fb_addr);

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
	    (v == 0x10de && d == 0x0a20))  {
		mode->set = 1;
		graphics_install_qemu(mode->x, mode->y);
	} else if (v == 0x80EE && d == 0xBEEF) {
		mode->set = 1;
		graphics_install_bochs(mode->x, mode->y);
	} else if ((v == 0x15ad && d == 0x0405)) {
		mode->set = 1;
		graphics_install_vmware(mode->x, mode->y);
	}
}

static fs_node_t * vga_text_device = NULL;

static int ioctl_vga(fs_node_t * node, unsigned long request, void * argp) {
	switch (request) {
		case IO_VID_WIDTH:
			/* Get framebuffer width */
			validate(argp);
			*((size_t *)argp) = 80;
			return 0;
		case IO_VID_HEIGHT:
			/* Get framebuffer height */
			validate(argp);
			*((size_t *)argp) = 25;
			return 0;
		case IO_VID_ADDR:
			/* Map framebuffer into userspace process */
			validate(argp);
			{
				uintptr_t vga_user_offset;
				if (*(uintptr_t*)argp == 0) {
					vga_user_offset = USER_DEVICE_MAP;
				} else {
					validate((void*)(*(uintptr_t*)argp));
					vga_user_offset = *(uintptr_t*)argp;
				}
				for (uintptr_t i = 0; i < 0x1000; i += 0x1000) {
					union PML * page = mmu_get_page(vga_user_offset + i, MMU_GET_MAKE);
					mmu_frame_map_address(page,MMU_FLAG_WRITABLE/*|MMU_FLAG_WC*/,(uintptr_t)(0xB8000 + i));
				}
				*((uintptr_t *)argp) = vga_user_offset;
			}
			return 0;
		default:
			return -EINVAL;
	}
}

static void vga_text_init(void) {
	vga_text_device = calloc(sizeof(fs_node_t), 1);
	snprintf(vga_text_device->name, 100, "vga0");
	vga_text_device->length = 0;
	vga_text_device->flags  = FS_BLOCKDEVICE;
	vga_text_device->mask   = 0660;
	vga_text_device->ioctl  = ioctl_vga;
	vfs_mount("/dev/vga0", vga_text_device);
}

static int lfb_init(const char * c) {
	char * arg = strdup(c);
	char * argv[10];
	int argc = tokenize(arg, ",", argv);

	if (!strcmp(argv[0],"text")) {
		/* VGA text mode? TODO: We should try to detect this,
		 * or limit it to things that are likely to have it... */
		vga_text_init();
		free(arg);
		return 0;
	}

	uint16_t x, y;

	/* Extract framebuffer information from multiboot */
	arch_framebuffer_initialize();
	x = lfb_resolution_x;
	y = lfb_resolution_y;

	if (argc >= 3) {
		x = atoi(argv[1]);
		y = atoi(argv[2]);
	} else if (!lfb_resolution_x) {
		x = PREFERRED_W;
		y = PREFERRED_H;
	}

	int ret_val = 0;
	if (!strcmp(argv[0], "auto")) {
		/* Attempt autodetection */
		struct disp_mode mode = {x,y,0};
		pci_scan(auto_scan_pci, -1, &mode);
		if (!mode.set) {
			graphics_install_preset(x,y);
		}
	} else if (!strcmp(argv[0], "qemu")) {
		/* BGA with MMIO */
		graphics_install_qemu(x,y);
	} else if (!strcmp(argv[0], "bochs")) {
		/* BGA with no MMIO */
		graphics_install_bochs(x,y);
	} else if (!strcmp(argv[0],"vmware")) {
		/* VMware SVGA */
		graphics_install_vmware(x,y);
	} else if (!strcmp(argv[0],"preset")) {
		/* Set by bootloader (UEFI) */
		graphics_install_preset(x,y);
	} else {
		ret_val = 1;
	}

	free(arg);
	return ret_val;
}

int framebuffer_initialize(void) {
	lfb_init(args_present("vid") ? args_value("vid") : "auto");

	return 0;
}

