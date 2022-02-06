/**
 * @file  kernel/arch/x86_64/vbox.c
 * @brief VirtualBox Guest Additions driver
 * @package x86_64
 *
 * Implements the following features:
 * - Absolute mouse cursor positioning
 * - "Hardware" cursor sprites
 * - Automatic display modesetting
 * - "Seamless" mode rectangle device
 * - Log device
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2016-2021 K. Lange
 */
#include <errno.h>
#include <kernel/types.h>
#include <kernel/vfs.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/pci.h>
#include <kernel/video.h>
#include <kernel/pipe.h>
#include <kernel/mouse.h>
#include <kernel/args.h>
#include <kernel/module.h>
#include <kernel/mmu.h>

#include <kernel/arch/x86_64/regs.h>
#include <kernel/arch/x86_64/ports.h>
#include <kernel/arch/x86_64/irq.h>

#define VBOX_VENDOR_ID 0x80EE
#define VBOX_DEVICE_ID 0xCAFE

static void vbox_scan_pci(uint32_t device, uint16_t v, uint16_t d, void * extra) {
	if (v == VBOX_VENDOR_ID && d == VBOX_DEVICE_ID) {
		*((uint32_t *)extra) = device;
	}
}

#define VMM_GetMouseState 1
#define VMM_SetMouseState 2
#define VMM_SetPointerShape 3
#define VMM_AcknowledgeEvents 41
#define VMM_ReportGuestInfo 50
#define VMM_GetDisplayChangeRequest 51
#define VMM_ReportGuestCapabilities 55
#define VMM_VideoSetVisibleRegion 72

#define VMMCAP_SeamlessMode (1 << 0)
#define VMMCAP_HostWindows (1 << 1)
#define VMMCAP_Graphics (1 << 2)

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

struct vbox_rtrect {
	int32_t xLeft;
	int32_t yTop;
	int32_t xRight;
	int32_t yBottom;
};

struct vbox_visibleregion {
	struct vbox_header header;
	uint32_t count;
	struct vbox_rtrect rect[1];
};

struct vbox_pointershape {
	struct vbox_header header;
	uint32_t flags;
	uint32_t xHot;
	uint32_t yHot;
	uint32_t width;
	uint32_t height;
	unsigned char data[];
};


#define EARLY_LOG_DEVICE 0x504
static size_t _vbox_write(size_t size, uint8_t * buffer) {
	for (unsigned int i = 0; i < size; ++i) {
		outportb(EARLY_LOG_DEVICE, buffer[i]);
	}
	return size;
}

void vbox_set_log(void) {
	printf_output = &_vbox_write;
	printf("Hello world, using VBox machine log for kernel output\n");
}

static uint32_t vbox_device = 0;
static uint32_t vbox_port = 0x0;
static int vbox_irq = 0;

static struct vbox_ack_events * vbox_irq_ack;
static uint32_t vbox_phys_ack;
static struct vbox_display_change * vbox_disp;
static uint32_t vbox_phys_disp;
static struct vbox_mouse * vbox_m;
static uint32_t vbox_phys_mouse;
static struct vbox_mouse * vbox_mg;
static uint32_t vbox_phys_mouse_get;
static struct vbox_visibleregion * vbox_visibleregion;
static uint32_t vbox_phys_visibleregion;
static struct vbox_pointershape * vbox_pointershape = NULL;
static uint32_t vbox_phys_pointershape;

static volatile uint32_t * vbox_vmmdev = 0;

static fs_node_t * mouse_pipe;
static fs_node_t * rect_pipe;
static fs_node_t * pointer_pipe;

static int mouse_state;

#define PACKETS_IN_PIPE 1024
#define DISCARD_POINT 32

#define VMM_Event_DisplayChange (1 << 2)
static void vbox_do_modeset(void) {
	outportl(vbox_port, vbox_phys_disp);
	outportl(vbox_port, vbox_phys_disp);
	if (lfb_resolution_x && vbox_disp->xres && (vbox_disp->xres != lfb_resolution_x  || vbox_disp->yres != lfb_resolution_y)) {
		lfb_set_resolution(vbox_disp->xres, vbox_disp->yres);
	}
}

#define VMM_Event_Mouse (1 << 9)
static void vbox_do_mouse(void) {
	outportl(vbox_port, vbox_phys_mouse_get);
	unsigned int x, y;

	if (lfb_vid_memory && lfb_resolution_x && lfb_resolution_y && vbox_mg->x && vbox_mg->y) {
		x = ((unsigned int)vbox_mg->x * lfb_resolution_x) / 0xFFFF;
		y = ((unsigned int)vbox_mg->y * lfb_resolution_y) / 0xFFFF;
	} else {
		x = vbox_mg->x;
		y = vbox_mg->y;
	}

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

static int vbox_irq_handler(struct regs *r) {
	if (!vbox_vmmdev[2]) return 0;

	uint32_t events;

	events = vbox_irq_ack->events = vbox_vmmdev[2];
	outportl(vbox_port, vbox_phys_ack);
	irq_ack(vbox_irq);

	if (events & VMM_Event_Mouse) vbox_do_mouse();
	if (events & VMM_Event_DisplayChange) vbox_do_modeset();

	return 1;
}

#define VBOX_MOUSE_ON (1 << 0) | (1 << 4)
#define VBOX_MOUSE_OFF (0)

static void mouse_on_off(unsigned int status) {
	mouse_state = status;

	vbox_m->header.size = sizeof(struct vbox_mouse);
	vbox_m->header.version = VBOX_REQUEST_HEADER_VERSION;
	vbox_m->header.requestType = VMM_SetMouseState;
	vbox_m->header.rc = 0;
	vbox_m->header.reserved1 = 0;
	vbox_m->header.reserved2 = 0;
	vbox_m->features = status;
	vbox_m->x = 0;
	vbox_m->y = 0;
	outportl(vbox_port, vbox_phys_mouse);
}

static int ioctl_mouse(fs_node_t * node, unsigned long request, void * argp) {
	if (request == 1) {
		/* Disable */
		mouse_on_off(VBOX_MOUSE_OFF);
		return 0;
	}
	if (request == 2) {
		/* Enable */
		mouse_on_off(VBOX_MOUSE_ON);
		return 0;
	}
	if (request == 3) {
		return mouse_state == (VBOX_MOUSE_ON);
	}
	return -1;
}

static ssize_t write_pointer(fs_node_t * node, off_t offset, size_t size, uint8_t *buffer) {
	if (!mouse_state) {
		return -1;
	}

	memcpy(&vbox_pointershape->data[288], buffer, 48*48*4);
	outportl(vbox_port, vbox_phys_pointershape);

	return size;
}

static ssize_t write_rectpipe(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
	(void)node;
	(void)offset;

	/* TODO should check size */

	/* This is kinda special and always assumes everything was written at once. */
	uint32_t count = ((uint32_t *)buffer)[0];
	vbox_visibleregion->count = count;

	if (count > 254) count = 254; /* enforce maximum */

	buffer += sizeof(uint32_t);

	for (unsigned int i = 0; i < count; ++i) {
		memcpy(&vbox_visibleregion->rect[i], buffer, sizeof(struct vbox_rtrect));
		buffer += sizeof(struct vbox_rtrect);
	}

	vbox_visibleregion->header.size = sizeof(struct vbox_header) + sizeof(uint32_t) + sizeof(int32_t) * count * 4;
	outportl(vbox_port, vbox_phys_visibleregion);

	return size;
}

static void * kvmalloc_p(size_t size, uint32_t * outphys) {
	uintptr_t index = mmu_allocate_n_frames(size / 0x1000) << 12;
	*outphys = index;
	return mmu_map_from_physical(index);
}

static int vbox_install(int argc, char * argv[]) {
	if (args_present("novbox")) return -ENODEV;
	pci_scan(vbox_scan_pci, -1, &vbox_device);
	if (!vbox_device) return -ENODEV;

	if (!args_present("novboxdebug")) {
		vbox_set_log();
	}

	uintptr_t t = pci_read_field(vbox_device, PCI_BAR0, 4);
	if (t > 0) {
		vbox_port = (t & 0xFFFFFFF0);
	}

	uint16_t c = pci_read_field(vbox_device, PCI_COMMAND, 2);
	//fprintf(&vb, "Command register: 0x%4x\n", c);
	if (!!(c & (1 << 10))) {
		//fprintf(&vb, "Interrupts are disabled\n");
	}


	mouse_pipe = make_pipe(sizeof(mouse_device_packet_t) * PACKETS_IN_PIPE);
	mouse_pipe->flags = FS_CHARDEVICE;
	mouse_pipe->ioctl = ioctl_mouse;

	vfs_mount("/dev/absmouse", mouse_pipe);

	vbox_irq = pci_get_interrupt(vbox_device);
	//fprintf(&vb, "irq line is %d\n", vbox_irq);
	irq_install_handler(vbox_irq, vbox_irq_handler, "vbox");

	uint32_t vbox_phys = 0;
	struct vbox_guest_info * packet = (void*)kvmalloc_p(0x1000, &vbox_phys);
	packet->header.size = sizeof(struct vbox_guest_info);
	packet->header.version = VBOX_REQUEST_HEADER_VERSION;
	packet->header.requestType = VMM_ReportGuestInfo;
	packet->header.rc = 0;
	packet->header.reserved1 = 0;
	packet->header.reserved2 = 0;
	packet->version = VMMDEV_VERSION;
	packet->ostype = 0x00100; /* Unknown, x86-64 */

	outportl(vbox_port, vbox_phys);

	struct vbox_guest_caps * caps = (void*)kvmalloc_p(0x1000, &vbox_phys);
	caps->header.size = sizeof(struct vbox_guest_caps);
	caps->header.version = VBOX_REQUEST_HEADER_VERSION;
	caps->header.requestType = VMM_ReportGuestCapabilities;
	caps->header.rc = 0;
	caps->header.reserved1 = 0;
	caps->header.reserved2 = 0;
	caps->caps = VMMCAP_Graphics | (args_present("novboxseamless") ? 0 : VMMCAP_SeamlessMode);
	outportl(vbox_port, vbox_phys);

	vbox_irq_ack = (void*)kvmalloc_p(0x1000, &vbox_phys_ack);
	vbox_irq_ack->header.size = sizeof(struct vbox_ack_events);
	vbox_irq_ack->header.version = VBOX_REQUEST_HEADER_VERSION;
	vbox_irq_ack->header.requestType = VMM_AcknowledgeEvents;
	vbox_irq_ack->header.rc = 0;
	vbox_irq_ack->header.reserved1 = 0;
	vbox_irq_ack->header.reserved2 = 0;
	vbox_irq_ack->events = 0;


	vbox_disp = (void*)kvmalloc_p(0x1000, &vbox_phys_disp);
	vbox_disp->header.size = sizeof(struct vbox_display_change);
	vbox_disp->header.version = VBOX_REQUEST_HEADER_VERSION;
	vbox_disp->header.requestType = VMM_GetDisplayChangeRequest;
	vbox_disp->header.rc = 0;
	vbox_disp->header.reserved1 = 0;
	vbox_disp->header.reserved2 = 0;
	vbox_disp->xres = 0;
	vbox_disp->yres = 0;
	vbox_disp->bpp = 0;
	vbox_disp->eventack = 1;

	vbox_m = (void*)kvmalloc_p(0x1000, &vbox_phys_mouse);
	mouse_on_off(VBOX_MOUSE_ON);

	/* For use with later receives */
	vbox_mg = (void*)kvmalloc_p(0x1000, &vbox_phys_mouse_get);
	vbox_mg->header.size = sizeof(struct vbox_mouse);
	vbox_mg->header.version = VBOX_REQUEST_HEADER_VERSION;
	vbox_mg->header.requestType = VMM_GetMouseState;
	vbox_mg->header.rc = 0;
	vbox_mg->header.reserved1 = 0;
	vbox_mg->header.reserved2 = 0;

	if (!args_present("novboxpointer")) {
		vbox_pointershape = (void*)kvmalloc_p(0x4000, &vbox_phys_pointershape);

		if (vbox_pointershape) {
			//fprintf(&vb, "Got a valid set of pages to load up a cursor.\n");
			vbox_pointershape->header.version = VBOX_REQUEST_HEADER_VERSION;
			vbox_pointershape->header.requestType = VMM_SetPointerShape;
			vbox_pointershape->header.rc = 0;
			vbox_pointershape->header.reserved1 = 0;
			vbox_pointershape->header.reserved2 = 0;
			vbox_pointershape->flags = (1 << 0) | (1 << 1) | (1 << 2); /* visible, alpha, shape */
			vbox_pointershape->xHot = 26;
			vbox_pointershape->yHot = 26;
			vbox_pointershape->width = 48;
			vbox_pointershape->height = 48;

			unsigned int mask_bytes = ((vbox_pointershape->width + 7) / 8) * vbox_pointershape->height;

			for (uint32_t i = 0; i < mask_bytes; ++i) {
				vbox_pointershape->data[i] = 0x00;
			}

			while (mask_bytes & 3) {
				mask_bytes++;
			}
			int base = mask_bytes;
			//fprintf(&vb, "mask_bytes = %d\n", mask_bytes);

			vbox_pointershape->header.size = sizeof(struct vbox_pointershape) + (48*48*4)+mask_bytes; /* update later */

			for (int i = 0; i < 48 * 48; ++i) {
				vbox_pointershape->data[base+i*4] = 0x00; /* blue */
				vbox_pointershape->data[base+i*4+1] = 0x00; /* red */
				vbox_pointershape->data[base+i*4+2] = 0x00; /* green */
				vbox_pointershape->data[base+i*4+3] = 0x00; /* alpha */
			}
			outportl(vbox_port, vbox_phys_pointershape);

			if (vbox_pointershape->header.rc < 0) {
				//fprintf(&vb, "Bad response code: -%d\n", -vbox_pointershape->header.rc);
			} else {
				/* Success, let's install the device file */
				//fprintf(&vb, "Successfully initialized cursor, going to allow compositor to set it.\n");
				pointer_pipe = malloc(sizeof(fs_node_t));
				memset(pointer_pipe, 0, sizeof(fs_node_t));
				pointer_pipe->mask = 0666;
				pointer_pipe->flags = FS_CHARDEVICE;
				pointer_pipe->write = write_pointer;

				vfs_mount("/dev/vboxpointer", pointer_pipe);
			}
		}
	}

	if (!args_present("novboxseamless")) {
		vbox_visibleregion = (void*)kvmalloc_p(0x1000, &vbox_phys_visibleregion);
		vbox_visibleregion->header.size = sizeof(struct vbox_header) + sizeof(uint32_t) + sizeof(int32_t) * 4; /* TODO + more for additional rects? */
		vbox_visibleregion->header.version = VBOX_REQUEST_HEADER_VERSION;
		vbox_visibleregion->header.requestType = VMM_VideoSetVisibleRegion;
		vbox_visibleregion->header.rc = 0;
		vbox_visibleregion->header.reserved1 = 0;
		vbox_visibleregion->header.reserved2 = 0;
		vbox_visibleregion->count = 1;
		vbox_visibleregion->rect[0].xLeft = 0;
		vbox_visibleregion->rect[0].yTop = 0;
		vbox_visibleregion->rect[0].xRight = 1440;
		vbox_visibleregion->rect[0].yBottom = 900;
		outportl(vbox_port, vbox_phys_visibleregion);

		rect_pipe = malloc(sizeof(fs_node_t));
		memset(rect_pipe, 0, sizeof(fs_node_t));
		rect_pipe->mask = 0666;
		rect_pipe->flags = FS_CHARDEVICE;
		rect_pipe->write = write_rectpipe;

		vfs_mount("/dev/vboxrects", rect_pipe);
	}

	/* device memory region mapping? */
	{
		uintptr_t t = pci_read_field(vbox_device, PCI_BAR1, 4);
		//fprintf(&vb, "mapping vmm_dev = 0x%x\n", t);
		if (t > 0) {
			vbox_vmmdev =  mmu_map_from_physical(t & 0xFFFFFFF0);
			printf("Setting vbox mem device at %p\n", (void*)vbox_vmmdev);
		}
	}

	/* Try a mode set */
	vbox_do_modeset();

	vbox_vmmdev[3] = 0xFFFFFFFF; /* Enable all for now */

	return 0;
}

static int fini(void) {
	return 0;
}

struct Module metadata = {
	.name = "vbox",
	.init = vbox_install,
	.fini = fini,
};

