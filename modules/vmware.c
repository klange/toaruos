/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2017 Kevin Lange
 *
 * VMWare absolute mouse driver.
 *
 * This device is also available by default in QEMU.
 *
 * Toggle off / back on with ioctl 1 and 2 respectively to /dev/vmmouse.
 *
 * Actually supports mouse buttons, unlike the one in VirtualBox.
 */

#include <system.h>
#include <fs.h>
#include <printf.h>
#include <types.h>
#include <logging.h>
#include <module.h>
#include <video.h>
#include <pipe.h>
#include <mouse.h>
#include <args.h>

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

static void vmware_io(vmware_cmd * cmd) {
	uint32_t dummy;

	/* Now how's THAT for a VM backdoor... */

	asm volatile(
			"pushl %%ebx\n"
			"pushl %%eax\n"
			"movl 20(%%eax), %%edi\n" /* Load data into registers */
			"movl 16(%%eax), %%esi\n"
			"movl 12(%%eax), %%edx\n"
			"movl  8(%%eax), %%ecx\n"
			"movl  4(%%eax), %%ebx\n"
			"movl   (%%eax), %%eax\n"
			"inl %%dx, %%eax\n"       /* Then trip a magic i/o port */
			"xchgl %%eax, (%%esp)\n"
			"movl %%edi, 20(%%eax)\n" /* Data also comes back out by registers */
			"movl %%esi, 16(%%eax)\n"
			"movl %%edx, 12(%%eax)\n"
			"movl %%ecx,  8(%%eax)\n"
			"movl %%ebx,  4(%%eax)\n"
			"popl (%%eax)\n"
			"popl %%ebx\n"
			: "=a"(dummy)
			: "0"(cmd)
			: "ecx", "edx", "esi", "edi", "memory" /* And vmware / qemu could trash anything they desire... */
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
	/* Disable the absolute mouse */
	vmware_cmd cmd;
	cmd.bx = 0xf5;
	cmd.command = 41;
	vmware_send(&cmd);
}

static void mouse_absolute(void) {
	/*
	 * Set the mouse to absolute.
	 *
	 * You can also set a relative mode, but there's not
	 * a lot of use in that as disabling the device just
	 * falls back to the PS/2 (or USB, I guess) device anyway,
	 * so instead of using that we just... turn it off.
	 */

	vmware_cmd cmd;
	cmd.bx = 0x53424152; /* request absolute */
	cmd.command = 41; /* request for abs/rel */
	vmware_send(&cmd);
}

volatile int8_t vmware_mouse_byte;

static void vmware_mouse(void) {
	/* unused, but we need to read the fake mouse event bytes from the PS/2 device. */
	vmware_mouse_byte = inportb(0x60);

	/* Read status byte. */
	vmware_cmd cmd;
	cmd.bx = 0;
	cmd.command = 40;
	vmware_send(&cmd);

	if (cmd.ax == 0xffff0000) {
		/* Device error; turn it off and back on again. */
		mouse_off();
		mouse_on();
		mouse_absolute();
		return;
	}

	int words = cmd.ax & 0xFFFF;

	if (!words || words % 4) {
		/* If we don't have data, or for some reason data isn't a multiple of 4... bail */
		return;
	}

	/* Read 4 bytes of data */
	cmd.bx = 4; /* how many */
	cmd.command = 39; /* read */
	vmware_send(&cmd);

	/*
	 * I guess the flags tell you if this was relative or absolute, so if we
	 * actually used the relative mode, we'd want to check that, but...
	 */
	int flags   = (cmd.ax & 0xFFFF0000) >> 16;
	int buttons = (cmd.ax & 0x0000FFFF);

	debug_print(INFO, "flags=%4x buttons=%4x", flags, buttons);
	debug_print(INFO, "x=%x y=%x z=%x", cmd.bx, cmd.cx, cmd.dx);

	if (lfb_vid_memory && lfb_resolution_x && lfb_resolution_y) {
		/*
		 * Just like the virtualbox stuff, this is based on a mapping
		 * to the display resolution, independently scaled in
		 * each dimension...
		 */
		unsigned int x = ((unsigned int)cmd.bx * lfb_resolution_x) / 0xFFFF;
		unsigned int y = ((unsigned int)cmd.cx * lfb_resolution_y) / 0xFFFF;

		mouse_device_packet_t packet;
		packet.magic = MOUSE_MAGIC;
		packet.x_difference = x;
		packet.y_difference = y;
		packet.buttons = 0;

		/* The particular bits for the buttons seem weird, but okay... */
		if (buttons & 0x20) {
			packet.buttons |= LEFT_CLICK;
		}
		if (buttons & 0x10) {
			packet.buttons |= RIGHT_CLICK;
		}
		if (buttons & 0x08) {
			packet.buttons |= MIDDLE_CLICK;
		}

		/* dx = z = scroll amount */
		if ((int8_t)cmd.dx > 0) {
			packet.buttons |= MOUSE_SCROLL_DOWN;
		} else if ((int8_t)cmd.dx < 0) {
			packet.buttons |= MOUSE_SCROLL_UP;
		}

		mouse_device_packet_t bitbucket;
		while (pipe_size(mouse_pipe) > (int)(DISCARD_POINT * sizeof(packet))) {
			read_fs(mouse_pipe, 0, sizeof(packet), (uint8_t *)&bitbucket);
		}
		write_fs(mouse_pipe, 0, sizeof(packet), (uint8_t *)&packet);
	}

}

static int detect_device(void) {
	vmware_cmd cmd;

	/* read version */
	cmd.bx = ~VMWARE_MAGIC;
	cmd.command = 10;
	vmware_send(&cmd);

	if (cmd.bx != VMWARE_MAGIC || cmd.ax == 0xFFFFFFFF) {
		/* Not a vmware device... */
		return 0;
	}

	/* Good to go! */
	return 1;
}

static int ioctl_mouse(fs_node_t * node, int request, void * argp) {
	if (request == 1) {
		/* Disable */
		mouse_off();
		ps2_mouse_alternate = NULL;
		return 0;
	}
	if (request == 2) {
		/* Enable */
		ps2_mouse_alternate = vmware_mouse;
		mouse_on();
		mouse_absolute();
		return 0;
	}
	return -1;
}

static int init(void) {
	if (detect_device()) {

		mouse_pipe = make_pipe(sizeof(mouse_device_packet_t) * PACKETS_IN_PIPE);
		mouse_pipe->flags = FS_CHARDEVICE;

		vfs_mount("/dev/vmmouse", mouse_pipe);

		mouse_pipe->flags = FS_CHARDEVICE;
		mouse_pipe->ioctl = ioctl_mouse;

		/*
		 * We have a hack in the PS/2 mouse driver that lets us
		 * take over for the normal mouse driver and essential
		 * intercept the interrputs when they are valid.
		 */
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
MODULE_DEPENDS(ps2mouse); /* For ps2_mouse_alternate */
MODULE_DEPENDS(lfbvideo); /* For lfb resolution */
