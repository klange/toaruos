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
#include <args.h>

#define VMWARE_VENDOR_ID 0x15AD
#define VMWARE_DEVICE_ID 0x0405

#define VMWARE_MAGIC 0x564D5868
#define VMWARE_PORT  0x5658

#define PACKETS_IN_PIPE 1024
#define DISCARD_POINT 32

/* -Wpedantic complains about unnamed unions */
#pragma GCC diagnostic ignored "-Wpedantic"

extern void (*ps2_mouse_alternate)(void); /* modules/mouse.c */

static fs_node_t * mouse_pipe;

typedef struct {
	union {
		uint32_t ax;
		uint32_t magic;
	};
	union {
		uint32_t bx;
		size_t size;
	};
	union {
		uint32_t cx;
		uint16_t command;
	};
	union {
		uint32_t dx;
		uint16_t port;
	};
	uint32_t si;
	uint32_t di;
} vmware_cmd;

static void vbox_scan_pci(uint32_t device, uint16_t v, uint16_t d, void * extra) {
	if (v == VMWARE_VENDOR_ID && d == VMWARE_DEVICE_ID) {
		*((uint32_t *)extra) = device;
	}
}

static void vmware_io(vmware_cmd * cmd) {
	uint32_t dummy;

	asm volatile(
			"pushl %%ebx\n"
			"pushl %%eax\n"
			"movl 20(%%eax), %%edi\n"
			"movl 16(%%eax), %%esi\n"
			"movl 12(%%eax), %%edx\n"
			"movl  8(%%eax), %%ecx\n"
			"movl  4(%%eax), %%ebx\n"
			"movl   (%%eax), %%eax\n"
			"inl %%dx, %%eax\n"
			"xchgl %%eax, (%%esp)\n"
			"movl %%edi, 20(%%eax)\n"
			"movl %%esi, 16(%%eax)\n"
			"movl %%edx, 12(%%eax)\n"
			"movl %%ecx,  8(%%eax)\n"
			"movl %%ebx,  4(%%eax)\n"
			"popl (%%eax)\n"
			"popl %%ebx\n"
			: "=a"(dummy)
			: "0"(cmd)
			: "ecx", "edx", "esi", "edi", "memory"
	);
}

static void vmware_send(vmware_cmd * cmd) {
	cmd->magic = VMWARE_MAGIC;
	cmd->port = VMWARE_PORT;

	vmware_io(cmd);
}

static void mouse_on(void) {
	vmware_cmd cmd;

	/* Enable */
	cmd.bx = 0x45414552;
	cmd.command = 41;
	vmware_send(&cmd);

	/* Status */
	cmd.bx = 0;
	cmd.command = 40;
	vmware_send(&cmd);

	/* Read data (1) */
	cmd.bx = 1;
	cmd.command = 39;
	vmware_send(&cmd);

	debug_print(WARNING, "Enabled with version ID %x", cmd.ax);
}

static void mouse_off(void) {
	vmware_cmd cmd;
	cmd.bx = 0xf5;
	cmd.command = 41;
	vmware_send(&cmd);
}

static void mouse_absolute(void) {
	vmware_cmd cmd;
	cmd.bx = 0x53424152; /* request absolute */
	cmd.command = 41; /* request for abs/rel */
	vmware_send(&cmd);
}

volatile int8_t vmware_mouse_byte;

static void vmware_mouse(void) {
	/* unused */
	vmware_mouse_byte = inportb(0x60);

	vmware_cmd cmd;
	cmd.bx = 0;
	cmd.command = 40;
	vmware_send(&cmd);

	if (cmd.ax == 0xffff0000) {
		mouse_off();
		mouse_on();
		mouse_absolute();
		return;
	}

	int words = cmd.ax & 0xFFFF;

	if (!words || words % 4) {
		return;
	}

	cmd.bx = 4;
	cmd.command = 39;
	vmware_send(&cmd);

	int flags   = (cmd.ax & 0xFFFF0000) >> 16;
	int buttons = (cmd.ax & 0x0000FFFF);

	debug_print(WARNING, "flags=%4x buttons=%4x", flags, buttons);
	debug_print(WARNING, "x=%x y=%x z=%x", cmd.bx, cmd.cx, cmd.dx);

	if (lfb_vid_memory && lfb_resolution_x && lfb_resolution_y) {
		unsigned int x = ((unsigned int)cmd.bx * lfb_resolution_x) / 0xFFFF;
		unsigned int y = ((unsigned int)cmd.cx * lfb_resolution_y) / 0xFFFF;

		mouse_device_packet_t packet;
		packet.magic = MOUSE_MAGIC;
		packet.x_difference = x;
		packet.y_difference = y;
		packet.buttons = 0;

		if (buttons & 0x20) {
			packet.buttons |= LEFT_CLICK;
		}
		if (buttons & 0x10) {
			packet.buttons |= RIGHT_CLICK;
		}
		if (buttons & 0x08) {
			packet.buttons |= MIDDLE_CLICK;
		}

		if ((int)cmd.dx > 0) {
			packet.buttons |= MOUSE_SCROLL_DOWN;
		} else if ((int)cmd.dx < 0) {
			packet.buttons |= MOUSE_SCROLL_UP;
		}

		mouse_device_packet_t bitbucket;
		while (pipe_size(mouse_pipe) > (int)(DISCARD_POINT * sizeof(packet))) {
			read_fs(mouse_pipe, 0, sizeof(packet), (uint8_t *)&bitbucket);
		}
		write_fs(mouse_pipe, 0, sizeof(packet), (uint8_t *)&packet);
	}

}

static int try_anyway(void) {
	vmware_cmd cmd;
	cmd.bx = ~VMWARE_MAGIC;
	cmd.command = 10;
	vmware_send(&cmd);

	if (cmd.bx != VMWARE_MAGIC || cmd.ax == 0xFFFFFFFF) {
		return 0;
	}

	return 1;
}

static int init(void) {
	int has_device = 0;
	pci_scan(vbox_scan_pci, -1, &has_device);

	if (has_device || try_anyway()) {

		mouse_pipe = make_pipe(sizeof(mouse_device_packet_t) * PACKETS_IN_PIPE);
		mouse_pipe->flags = FS_CHARDEVICE;

		vfs_mount("/dev/vmmouse", mouse_pipe);

		ps2_mouse_alternate = vmware_mouse;

		mouse_on();
		mouse_absolute();

	}

	return 0;
}

static int fini(void) {
	return 0;
}

MODULE_DEF(vmmware, init, fini);
MODULE_DEPENDS(ps2mouse);
MODULE_DEPENDS(lfbvideo);
