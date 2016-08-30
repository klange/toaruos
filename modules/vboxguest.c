/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2016 Kevin Lange
 *
 * VirtualBox Guest Additions driver
 */

#include <system.h>
#include <fs.h>
#include <printf.h>
#include <types.h>
#include <logging.h>
#include <pci.h>
#include <module.h>
#include <video.h>
#include <pipe.h>
#include <mouse.h>

#define VBOX_VENDOR_ID 0x80EE
#define VBOX_DEVICE_ID 0xCAFE

static void vbox_scan_pci(uint32_t device, uint16_t v, uint16_t d, void * extra) {
	if (v == VBOX_VENDOR_ID && d == VBOX_DEVICE_ID) {
		*((uint32_t *)extra) = device;
	}
}

#define VMMDEV_VERSION 0x00010003
#define VBOX_REQUEST_HEADER_VERSION 0x10001
struct vbox_header {
	uint32_t size;
	uint32_t version;
	uint32_t requestType;
	int32_t  rc;
	uint32_t reserved1;
	uint32_t reserved2;
};

struct vbox_guest_info {
	struct vbox_header header;
	uint32_t version;
	uint32_t ostype;
};

struct vbox_guest_caps {
	struct vbox_header header;
	uint32_t caps;
};

struct vbox_ack_events {
	struct vbox_header header;
	uint32_t events;
};

struct vbox_display_change {
	struct vbox_header header;
	uint32_t xres;
	uint32_t yres;
	uint32_t bpp;
	uint32_t eventack;
};

struct vbox_mouse {
	struct vbox_header header;
	uint32_t features;
	int32_t x;
	int32_t y;
};


#define EARLY_LOG_DEVICE 0x504
static uint32_t _vbox_write(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	for (unsigned int i = 0; i < size; ++i) {
		outportb(EARLY_LOG_DEVICE, buffer[i]);
	}
	return size;
}
static fs_node_t vb = { .write = &_vbox_write };

static uint32_t vbox_device = 0;
static uint32_t vbox_port = 0x0;
static int vbox_irq = 0;

static struct vbox_ack_events * vbox_irq_ack;
static uint32_t vbox_phys_ack;
static struct vbox_display_change * vbox_disp;
static uint32_t vbox_phys_disp;
static struct vbox_mouse * vbox_m;
static uint32_t vbox_phys_mouse;
static uint32_t * vbox_vmmdev = 0;

static fs_node_t * mouse_pipe;

#define PACKETS_IN_PIPE 1024
#define DISCARD_POINT 32

static int vbox_irq_handler(struct regs *r) {
	outportl(vbox_port, vbox_phys_disp);
	outportl(vbox_port, vbox_phys_mouse);
	outportl(vbox_port, vbox_phys_ack);
	irq_ack(vbox_irq);

	if (lfb_vid_memory && lfb_resolution_x && lfb_resolution_y && vbox_m->x && vbox_m->y) {
		unsigned int x = ((unsigned int)vbox_m->x * lfb_resolution_x) / 0xFFFF;
		unsigned int y = ((unsigned int)vbox_m->y * lfb_resolution_y) / 0xFFFF;

		mouse_device_packet_t packet;
		packet.magic = MOUSE_MAGIC;
		packet.x_difference = x;
		packet.y_difference = y;
		packet.buttons = 0;

		mouse_device_packet_t bitbucket;
		while (pipe_size(mouse_pipe) > (int)(DISCARD_POINT * sizeof(packet))) {
			read_fs(mouse_pipe, 0, sizeof(packet), (uint8_t *)&bitbucket);
		}
		write_fs(mouse_pipe, 0, sizeof(packet), (uint8_t *)&packet);
	}


	if (lfb_resolution_x && vbox_disp->xres && (vbox_disp->xres != lfb_resolution_x  || vbox_disp->yres != lfb_resolution_y)) {

		lfb_set_resolution(vbox_disp->xres, vbox_disp->yres);
	}
	return 1;
}

static int vbox_check(void) {
	pci_scan(vbox_scan_pci, -1, &vbox_device);

	if (vbox_device) {
		fprintf(&vb, "VirtualBox host detected, switching log to VirtualBox.\n");
		debug_file = &vb;

		uintptr_t t = pci_read_field(vbox_device, PCI_BAR0, 4);
		if (t > 0) {
			vbox_port = (t & 0xFFFFFFF0);
		}

		mouse_pipe = make_pipe(sizeof(mouse_device_packet_t) * PACKETS_IN_PIPE);
		mouse_pipe->flags = FS_CHARDEVICE;

		vfs_mount("/dev/absmouse", mouse_pipe);

		vbox_irq = pci_read_field(vbox_device, PCI_INTERRUPT_LINE, 1);
		debug_print(WARNING, "(vbox) device IRQ is set to %d\n", vbox_irq);
		irq_install_handler(vbox_irq, vbox_irq_handler);

		uint32_t vbox_phys = 0;
		struct vbox_guest_info * packet = (void*)kvmalloc_p(0x1000, &vbox_phys);
		packet->header.size = sizeof(struct vbox_guest_info);
		packet->header.version = VBOX_REQUEST_HEADER_VERSION;
		packet->header.requestType = 50;
		packet->header.rc = 0;
		packet->header.reserved1 = 0;
		packet->header.reserved2 = 0;
		packet->version = VMMDEV_VERSION;
		packet->ostype = 0;

		outportl(vbox_port, vbox_phys);

		struct vbox_guest_caps * caps = (void*)kvmalloc_p(0x1000, &vbox_phys);
		caps->header.size = sizeof(struct vbox_guest_caps);
		caps->header.version = VBOX_REQUEST_HEADER_VERSION;
		caps->header.requestType = 55;
		caps->header.rc = 0;
		caps->header.reserved1 = 0;
		caps->header.reserved2 = 0;
		caps->caps = 1 << 2;
		outportl(vbox_port, vbox_phys);

		vbox_irq_ack = (void*)kvmalloc_p(0x1000, &vbox_phys_ack);
		vbox_irq_ack->header.size = sizeof(struct vbox_ack_events);
		vbox_irq_ack->header.version = VBOX_REQUEST_HEADER_VERSION;
		vbox_irq_ack->header.requestType = 41;
		vbox_irq_ack->header.rc = 0;
		vbox_irq_ack->header.reserved1 = 0;
		vbox_irq_ack->header.reserved2 = 0;
		vbox_irq_ack->events = 0;

		vbox_disp = (void*)kvmalloc_p(0x1000, &vbox_phys_disp);
		vbox_disp->header.size = sizeof(struct vbox_display_change);
		vbox_disp->header.version = VBOX_REQUEST_HEADER_VERSION;
		vbox_disp->header.requestType = 51;
		vbox_disp->header.rc = 0;
		vbox_disp->header.reserved1 = 0;
		vbox_disp->header.reserved2 = 0;
		vbox_disp->xres = 0;
		vbox_disp->yres = 0;
		vbox_disp->bpp = 0;
		vbox_disp->eventack = 1;

		vbox_m = (void*)kvmalloc_p(0x1000, &vbox_phys_mouse);
		vbox_m->header.size = sizeof(struct vbox_mouse);
		vbox_m->header.version = VBOX_REQUEST_HEADER_VERSION;
		vbox_m->header.requestType = 2;
		vbox_m->header.rc = 0;
		vbox_m->header.reserved1 = 0;
		vbox_m->header.reserved2 = 0;
		vbox_m->features = (1 << 0) | (1 << 4);
		vbox_m->x = 0;
		vbox_m->y = 0;
		outportl(vbox_port, vbox_phys_mouse);

		vbox_m->header.requestType = 1;

		/* device memory region mapping? */
		{
			uintptr_t t = pci_read_field(vbox_device, PCI_BAR1, 4);
			if (t > 0) {
				vbox_vmmdev =  (void *)(t & 0xFFFFFFF0);
			}
			uintptr_t fb_offset = (uintptr_t)vbox_vmmdev;
			for (uintptr_t i = fb_offset; i <= fb_offset + 0x2000; i += 0x1000) {
				dma_frame(get_page(i, 1, kernel_directory), 0, 1, i);
			}
		}

		vbox_vmmdev[3] = 0xFFFFFFFF; /* Enable all for now */
	}

	return 0;
}

static int fini(void) {
	return 0;
}

MODULE_DEF(vboxguest, vbox_check, fini);
MODULE_DEPENDS(lfbvideo);
