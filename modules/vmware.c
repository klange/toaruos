/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2017-2018 K. Lange
 *
 * VMWare backdoor driver.
 *
 * Supports absolute mouse cursor and resolution setting.
 *
 * Mouse:
 *   Toggle off / on with ioctl 1 and 2 respectively to /dev/vmmouse.
 *   Supports mouse buttons, unlike the one in VirtualBox.
 *   This device is also available by default in QEMU.
 *
 * Resolution setting:
 *   Enabled when the "vmware" LFB driver is active. Automatically
 *   resizes the display when the window size changes.
 */

#include <kernel/system.h>
#include <kernel/fs.h>
#include <kernel/printf.h>
#include <kernel/types.h>
#include <kernel/logging.h>
#include <kernel/module.h>
#include <kernel/video.h>
#include <kernel/pipe.h>
#include <kernel/mouse.h>
#include <kernel/args.h>

#define VMWARE_MAGIC  0x564D5868 /* hXMV */
#define VMWARE_PORT   0x5658
#define VMWARE_PORTHB 0x5659

#define PACKETS_IN_PIPE 1024
#define DISCARD_POINT 32

#define CMD_GETVERSION         10
#define CMD_MESSAGE            30
#define CMD_ABSPOINTER_DATA    39
#define CMD_ABSPOINTER_STATUS  40
#define CMD_ABSPOINTER_COMMAND 41

#define ABSPOINTER_ENABLE   0x45414552 /* Q E A E */
#define ABSPOINTER_RELATIVE 0xF5
#define ABSPOINTER_ABSOLUTE 0x53424152 /* R A B S */

#define MESSAGE_RPCI   0x49435052 /* R P C I */
#define MESSAGE_TCLO   0x4f4c4354 /* T C L O */

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

/** Low bandwidth backdoor */
static void vmware_send(vmware_cmd * cmd) {
	cmd->magic = VMWARE_MAGIC;
	cmd->port = VMWARE_PORT;

	asm volatile("in %%dx, %0" : "+a"(cmd->ax), "+b"(cmd->bx), "+c"(cmd->cx), "+d"(cmd->dx), "+S"(cmd->si), "+D"(cmd->di));
}

/** Output to high bandwidth backdoor */
static void vmware_send_hb(vmware_cmd * cmd) {
	cmd->magic = VMWARE_MAGIC;
	cmd->port = VMWARE_PORTHB;

	asm volatile("cld; rep; outsb" : "+a"(cmd->ax), "+b"(cmd->bx), "+c"(cmd->cx), "+d"(cmd->dx), "+S"(cmd->si), "+D"(cmd->di));
}

/** Input from high bandwidth backdoor */
static void vmware_get_hb(vmware_cmd * cmd) {
	cmd->magic = VMWARE_MAGIC;
	cmd->port = VMWARE_PORTHB;

	asm volatile("cld; rep; insb" : "+a"(cmd->ax), "+b"(cmd->bx), "+c"(cmd->cx), "+d"(cmd->dx), "+S"(cmd->si), "+D"(cmd->di));
}

static void mouse_off(void) {
	/* Disable the absolute mouse */
	vmware_cmd cmd;
	cmd.bx = ABSPOINTER_RELATIVE;
	cmd.command = CMD_ABSPOINTER_COMMAND;
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

	/* Enable */
	cmd.bx = ABSPOINTER_ENABLE;
	cmd.command = CMD_ABSPOINTER_COMMAND;
	vmware_send(&cmd);

	/* Status */
	cmd.bx = 0;
	cmd.command = CMD_ABSPOINTER_STATUS;
	vmware_send(&cmd);

	/* Read data (1) */
	cmd.bx = 1;
	cmd.command = CMD_ABSPOINTER_DATA;
	vmware_send(&cmd);

	/* Enable absolute */
	cmd.bx = ABSPOINTER_ABSOLUTE;
	cmd.command = CMD_ABSPOINTER_COMMAND;
	vmware_send(&cmd);
}

volatile int8_t vmware_mouse_byte = 0;

static void vmware_mouse(void) {
	/* unused, but we need to read the fake mouse event bytes from the PS/2 device. */
	vmware_mouse_byte = inportb(0x60);

	/* Read status byte. */
	vmware_cmd cmd;
	cmd.bx = 0;
	cmd.command = CMD_ABSPOINTER_STATUS;
	vmware_send(&cmd);

	if (cmd.ax == 0xffff0000) {
		/* Device error; turn it off and back on again. */
		mouse_off();
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
	cmd.command = CMD_ABSPOINTER_DATA; /* read */
	vmware_send(&cmd);

	/*
	 * I guess the flags tell you if this was relative or absolute, so if we
	 * actually used the relative mode, we'd want to check that, but...
	 */
	int flags   = (cmd.ax & 0xFFFF0000) >> 16;
	int buttons = (cmd.ax & 0x0000FFFF);

	debug_print(INFO, "flags=%4x buttons=%4x", flags, buttons);
	debug_print(INFO, "x=%x y=%x z=%x", cmd.bx, cmd.cx, cmd.dx);

	unsigned int x = 0;
	unsigned int y = 0;

	if (lfb_vid_memory && lfb_resolution_x && lfb_resolution_y) {
		/*
		 * Just like the virtualbox stuff, this is based on a mapping
		 * to the display resolution, independently scaled in
		 * each dimension...
		 */
		x = ((unsigned int)cmd.bx * lfb_resolution_x) / 0xFFFF;
		y = ((unsigned int)cmd.cx * lfb_resolution_y) / 0xFFFF;
	} else {
		x = cmd.bx;
		y = cmd.cx;
	}

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

static int detect_device(void) {
	vmware_cmd cmd;

	/* read version */
	cmd.bx = ~VMWARE_MAGIC;
	cmd.command = CMD_GETVERSION;
	vmware_send(&cmd);

	if (cmd.bx != VMWARE_MAGIC || cmd.ax == 0xFFFFFFFF) {
		/* Not a vmware device... */
		return 0;
	}

	/* Good to go! */
	return 1;
}


static int open_msg_channel(uint32_t proto) {
	vmware_cmd cmd;
	cmd.cx = CMD_MESSAGE | 0x00000000; /* CMD_MESSAGE */
	cmd.bx = proto;
	vmware_send(&cmd);

	if ((cmd.cx & 0x10000) == 0) {
		return -1;
	}

	return cmd.dx >> 16;
}

static void msg_close(int channel) {
	vmware_cmd cmd = {0};
	cmd.cx = CMD_MESSAGE | 0x00060000;
	cmd.bx = 0;
	cmd.dx = channel << 16;

	vmware_send(&cmd);
}

static int open_rpci_channel(void) {
	return open_msg_channel(MESSAGE_RPCI);
}

static int tclo_channel = -1;

static int open_tclo_channel(void) {
	if (tclo_channel != -1) {
		msg_close(tclo_channel);
	}
	tclo_channel = open_msg_channel(MESSAGE_TCLO);
	return tclo_channel;
}

static int msg_send(int channel, char * msg, size_t size) {
	{
		vmware_cmd cmd = {0};
		cmd.cx = CMD_MESSAGE | 0x00010000; /* CMD_MESSAGE size */
		cmd.size = size;
		cmd.dx   = channel << 16;
		vmware_send(&cmd);

		if (size == 0) return 0;

		if (((cmd.cx >> 16) & 0x0081) != 0x0081) {
			return -2;
		}
	}

	{
		vmware_cmd cmd = {0};
		cmd.bx = 0x0010000;
		cmd.cx = size;
		cmd.dx = channel << 16;
		cmd.si = (uint32_t)msg;
		vmware_send_hb(&cmd);

		if (!(cmd.bx & 0x0010000)) {
			return -3;
		}
	}

	return 0;
}

static int msg_recv(int channel, char * buf, size_t bufsize) {
	size_t size;
	{
		vmware_cmd cmd = {0};
		cmd.cx = CMD_MESSAGE | 0x00030000; /* CMD_MESSAGE receive ize */
		cmd.dx   = channel << 16;
		vmware_send(&cmd);

		size = cmd.bx;
		if (size == 0) return 0;
		if (((cmd.cx >> 16) & 0x0083) != 0x0083) {
			return -2;
		}
		if (size > bufsize) return -1;
	}

	{
		vmware_cmd cmd = {0};
		cmd.bx = 0x00010000;
		cmd.cx = size;
		cmd.dx = channel << 16;
		cmd.di = (uint32_t)buf;

		vmware_get_hb(&cmd);
		if (!(cmd.bx & 0x00010000)) {
			return -3;
		}
	}

	{
		vmware_cmd cmd = {0};
		cmd.cx = CMD_MESSAGE | 0x00050000;
		cmd.bx = 0x0001;
		cmd.dx = channel << 16;

		vmware_send(&cmd);
	}

	return size;
}

static int rpci_string(char * request) {
	/* Open channel */
	int channel = open_rpci_channel();
	if (channel < 0) return channel;

	size_t size = strlen(request) + 1;
	msg_send(channel, request, size);

	char buf[16];
	int recv_size = msg_recv(channel, buf, 16);

	msg_close(channel);

	if (recv_size < 0) return recv_size;

	return 0;
}

static int attempt_scale(void) {

	int i;
	int c = open_tclo_channel();
	if (c < 0) {
		return 1;
	}

	char buf[256];
	if ((i = msg_send(c, buf, 0)) < 0) { return 1; }

	int resend = 0;

	while (1) {
		i = msg_recv(c, buf, 256);
		if (i < 0) {
			return 1;
		} else if (i == 0) {
			if (resend) {
				if ((i = rpci_string("tools.capability.resolution_set 1")) < 0) { return 1; }
				if ((i = rpci_string("tools.capability.resolution_server toolbox 1")) < 0) { return 1; }
				if ((i = rpci_string("tools.capability.display_topology_set 1")) < 0) { return 1; }
				if ((i = rpci_string("tools.capability.color_depth_set 1")) < 0) { return 1; }
				if ((i = rpci_string("tools.capability.resolution_min 0 0")) < 0) { return 1; }
				if ((i = rpci_string("tools.capability.unity 1")) < 0) { return 1; }
				resend = 0;
			} else {
				unsigned long s, ss;
				relative_time(0, 10, &s, &ss);
				sleep_until((process_t *)current_process, s, ss);
				switch_task(0);
			}
			if ((i = msg_send(c, buf, 0)) < 0) { return 1; }
		} else {
			buf[i] = '\0';
			if (startswith(buf, "reset")) {
				if ((i = msg_send(c, "OK ATR toolbox", strlen("OK ATR toolbox"))) < 0) {
					return 1;
				}
			} else if (startswith(buf, "ping")) {
				if ((i = msg_send(c, "OK ", strlen("OK "))) < 0) {
					return 1;
				}
			} else if (startswith(buf, "Capabilities_Register")) {
				if ((i = msg_send(c, "OK ", strlen("OK "))) < 0) {
					return 1;
				}
				resend = 1;
			} else if (startswith(buf, "Resolution_Set")) {
				char * x = &buf[15];
				char * y = strstr(x," ");
				if (!y) {
					return 1;
				}
				*y = '\0';
				y++;
				int _x = atoi(x);
				int _y = atoi(y);

				if (lfb_resolution_x && _x && (_x != lfb_resolution_x  || _y != lfb_resolution_y)) {
					lfb_set_resolution(_x, _y);
				}

				if ((i = msg_send(c, "OK ", strlen("OK "))) < 0) {
					return 1;
				}

				msg_close(c);
				return 0;
			} else {
				if ((i = msg_send(c, "ERROR Unknown command", strlen("ERROR Unknown command"))) < 0) {
					return 1;
				}
			}
		}
	}
}

static void vmware_resize(void * data, char * name) {
	while (1) {
		attempt_scale();
		unsigned long s, ss;
		relative_time(1, 0, &s, &ss);
		sleep_until((process_t *)current_process, s, ss);
		switch_task(0);
	}
}

static int ioctl_mouse(fs_node_t * node, int request, void * argp) {
	switch (request) {
		case 1:
			/* Disable */
			mouse_off();
			ps2_mouse_alternate = NULL;
			return 0;
		case 2:
			/* Enable */
			ps2_mouse_alternate = vmware_mouse;
			mouse_absolute();
			return 0;
		case 3:
			return ps2_mouse_alternate == vmware_mouse;
		default:
			return -EINVAL;
	}
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

		mouse_absolute();

		if (lfb_driver_name && !strcmp(lfb_driver_name, "vmware") && !args_present("novmwareresset")) {
			create_kernel_tasklet(vmware_resize, "[vmware]", NULL);
		}

	}

	return 0;
}

static int fini(void) {
	return 0;
}

MODULE_DEF(vmmware, init, fini);
MODULE_DEPENDS(ps2mouse); /* For ps2_mouse_alternate */
MODULE_DEPENDS(lfbvideo); /* For lfb resolution */
