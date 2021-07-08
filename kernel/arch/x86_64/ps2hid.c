/**
 * @file  kernel/arch/x86_64/ps2mouse.c
 * @brief PC PS/2 input device driver
 *
 * This is the slightly less terrible merged PS/2 mouse+keyboard driver.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014-2021 K. Lange
 */
#include <stdint.h>
#include <stddef.h>
#include <kernel/printf.h>
#include <kernel/pipe.h>
#include <kernel/mouse.h>
#include <kernel/misc.h>
#include <kernel/args.h>

#include <kernel/arch/x86_64/ports.h>
#include <kernel/arch/x86_64/regs.h>
#include <kernel/arch/x86_64/irq.h>

#define PACKETS_IN_PIPE    1024
#define DISCARD_POINT        32
#define KEYBOARD_IRQ          1
#define MOUSE_IRQ            12

#define PS2_DATA           0x60
#define PS2_STATUS         0x64
#define PS2_COMMAND        0x64
#define MOUSE_WRITE        0xD4
#define MOUSE_V_BIT        0x08

#define PS2_PORT1_IRQ      0x01
#define PS2_PORT2_IRQ      0x02
#define PS2_PORT1_TLATE    0x40

#define PS2_READ_CONFIG    0x20
#define PS2_WRITE_CONFIG   0x60

#define PS2_DISABLE_PORT2  0xA7
#define PS2_ENABLE_PORT2   0xA8
#define PS2_DISABLE_PORT1  0xAD
#define PS2_ENABLE_PORT1   0xAE

#define MOUSE_SET_REMOTE   0xF0
#define MOUSE_DEVICE_ID    0xF2
#define MOUSE_SAMPLE_RATE  0xF3
#define MOUSE_DATA_ON      0xF4
#define MOUSE_DATA_OFF     0xF5
#define MOUSE_SET_DEFAULTS 0xF6

#define MOUSE_DEFAULT         0
#define MOUSE_SCROLLWHEEL     1
#define MOUSE_BUTTONS         2

#define KBD_SET_SCANCODE   0xF0

static uint8_t mouse_cycle = 0;
static uint8_t mouse_byte[4];
static int8_t mouse_mode = MOUSE_DEFAULT;
static fs_node_t * mouse_pipe;
static fs_node_t * keyboard_pipe;

void (*ps2_mouse_alternate)(uint8_t) = NULL;

/**
 * @brief Wait until the PS/2 controller's input buffer is clear.
 *
 * Use this before WRITING to the controller.
 */
static int ps2_wait_input(void) {
	uint64_t timeout = 100000UL;
	while (--timeout) {
		if (!(inportb(PS2_STATUS) & (1 << 1))) return 0;
	}
	return 1;
}

/**
 * @brief Wait until the PS/2 controller's output buffer is filled.
 *
 * Use this before READING from the controller.
 */
static int ps2_wait_output(void) {
	uint64_t timeout = 100000UL;
	while (--timeout) {
		if (inportb(PS2_STATUS) & (1 << 0)) return 0;
	}
	return 1;
}

/**
 * @brief Send a command with no response or argument.
 */
static void ps2_command(uint8_t cmdbyte) {
	ps2_wait_input();
	outportb(PS2_COMMAND, cmdbyte);
}

/**
 * @brief Send a command and get the reply.
 */
static uint8_t ps2_command_response(uint8_t cmdbyte) {
	ps2_wait_input();
	outportb(PS2_COMMAND, cmdbyte);
	ps2_wait_output();
	return inportb(PS2_DATA);
}

/**
 * @brief Send a command with an argument and no reply.
 */
static void ps2_command_arg(uint8_t cmdbyte, uint8_t arg) {
	ps2_wait_input();
	outportb(PS2_COMMAND, cmdbyte);
	ps2_wait_input();
	outportb(PS2_DATA, arg);
}

/**
 * @brief Write to the aux port.
 */
static uint8_t mouse_write(uint8_t write) {
	ps2_command_arg(MOUSE_WRITE, write);
	ps2_wait_output();
	return inportb(PS2_DATA);
}

/**
 * @brief Read generic response byte
 */
static uint8_t ps2_read_byte(void) {
	ps2_wait_output();
	return inportb(PS2_DATA);
}

/**
 * @brief Write to the primary port.
 */
static uint8_t kbd_write(uint8_t write) {
	ps2_wait_input();
	outportb(PS2_DATA, write);
	ps2_wait_output();
	return inportb(PS2_DATA);
}

/**
 * @brief Process a completed mouse packet.
 *
 * Assembles a mouse_device_packet_t from the data we got from
 * the PS/2 device and forwards it to the pipe to be read by
 * userspace; if the pipe is full we discard old bytes first.
 */
static void finish_packet(void) {
	mouse_cycle = 0;
	/* We now have a full mouse packet ready to use */
	mouse_device_packet_t packet;
	packet.magic = MOUSE_MAGIC;
	int x = mouse_byte[1];
	int y = mouse_byte[2];
	if (x && mouse_byte[0] & (1 << 4)) {
		/* Sign bit */
		x = x - 0x100;
	}
	if (y && mouse_byte[0] & (1 << 5)) {
		/* Sign bit */
		y = y - 0x100;
	}
	if (mouse_byte[0] & (1 << 6) || mouse_byte[0] & (1 << 7)) {
		/* Overflow */
		x = 0;
		y = 0;
	}
	packet.x_difference = x;
	packet.y_difference = y;
	packet.buttons = 0;
	if (mouse_byte[0] & 0x01) {
		packet.buttons |= LEFT_CLICK;
	}
	if (mouse_byte[0] & 0x02) {
		packet.buttons |= RIGHT_CLICK;
	}
	if (mouse_byte[0] & 0x04) {
		packet.buttons |= MIDDLE_CLICK;
	}

	if (mouse_mode == MOUSE_SCROLLWHEEL && mouse_byte[3]) {
		if ((int8_t)mouse_byte[3] > 0) {
			packet.buttons |= MOUSE_SCROLL_DOWN;
		} else if ((int8_t)mouse_byte[3] < 0) {
			packet.buttons |= MOUSE_SCROLL_UP;
		}
	}

	mouse_device_packet_t bitbucket;
	while (pipe_size(mouse_pipe) > (int)(DISCARD_POINT * sizeof(packet))) {
		read_fs(mouse_pipe, 0, sizeof(packet), (uint8_t *)&bitbucket);
	}
	write_fs(mouse_pipe, 0, sizeof(packet), (uint8_t *)&packet);
}

/**
 * @brief Read one byte from the mouse.
 */
static void ps2_mouse_handle(uint8_t data_byte) {
	if (ps2_mouse_alternate) {
		ps2_mouse_alternate(data_byte);
	} else {
		int8_t mouse_in = data_byte;
		switch (mouse_cycle) {
			case 0:
				mouse_byte[0] = mouse_in;
				if (!(mouse_in & MOUSE_V_BIT)) break;
				++mouse_cycle;
				break;
			case 1:
				mouse_byte[1] = mouse_in;
				++mouse_cycle;
				break;
			case 2:
				mouse_byte[2] = mouse_in;
				if (mouse_mode == MOUSE_SCROLLWHEEL || mouse_mode == MOUSE_BUTTONS) {
					++mouse_cycle;
					break;
				}
				finish_packet();
				break;
			case 3:
				mouse_byte[3] = mouse_in;
				finish_packet();
				break;
		}
	}
}

static int ioctl_mouse(fs_node_t * node, unsigned long request, void * argp) {
	if (request == 1) {
		mouse_cycle = 0;
		return 0;
	}
	return -1;
}

/**
 * @brief Read one byte from the keyboard.
 *
 * We give userspace the keyboard scancodes directly, and libtoaru_kbd
 * handles translation to a more usable format. This is probably not
 * the best way to do this...
 */
static void ps2_kbd_handle(uint8_t data_byte) {
	write_fs(keyboard_pipe, 0, 1, (uint8_t []){data_byte});
}

/**
 * @brief Shared handler that does some magic that probably only works in QEMU.
 *
 * The general idea behind this shared handler is that QEMU is "broken"
 * and introduces a race that shouldn't be possible on real hardware?
 * We can get an interrupt but the byte we get out of the port is
 * for the other device. This makes playing Quake very hard because
 * our keyboard our mouse devices get garbage when we're doing both
 * at once! Thankfully, QEMU supports the status bit for checking
 * if there is mouse data, and if we prevent any data from coming
 * in from either port (by disabling both) while checking both
 * the status and the data port, we can use that as a lock and get
 * an "atomic" read that tells us which thing the data come from.
 */
static int shared_handler(struct regs * r) {
	/* Disable both ports */
	ps2_command(PS2_DISABLE_PORT1);
	ps2_command(PS2_DISABLE_PORT2);
	/* Read the status and data */
	uint8_t status = inportb(PS2_STATUS);
	uint8_t data_byte = inportb(PS2_DATA);
	/* Re-enable both */
	ps2_command(PS2_ENABLE_PORT1);
	ps2_command(PS2_ENABLE_PORT2);

	irq_ack(r->int_no-32);

	if (!(status & 0x01)) return 1;

	if (!(status & 0x20)) {
		ps2_kbd_handle(data_byte);
	} else if (status & 0x21) {
		ps2_mouse_handle(data_byte);
	}
	return 1;
}

/**
 * @brief IRQ1 handler.
 */
static int keyboard_handler(struct regs *r) {
	uint8_t data_byte = inportb(PS2_DATA);
	irq_ack(KEYBOARD_IRQ);
	ps2_kbd_handle(data_byte);
	return 1;
}

/**
 * @brief IRQ12 handler.
 */
static int mouse_handler(struct regs *r) {
	uint8_t data_byte = inportb(PS2_DATA);
	irq_ack(MOUSE_IRQ);
	ps2_mouse_handle(data_byte);
	return 1;
}


/**
 * @brief Initialze i8042/AIP PS/2 controller.
 */
void ps2hid_install(void) {
	uint8_t status, result;

	mouse_pipe = make_pipe(sizeof(mouse_device_packet_t) * PACKETS_IN_PIPE);
	mouse_pipe->flags = FS_CHARDEVICE;
	mouse_pipe->ioctl = ioctl_mouse;
	vfs_mount("/dev/mouse", mouse_pipe);

	keyboard_pipe = make_pipe(128);
	keyboard_pipe->flags = FS_CHARDEVICE;
	vfs_mount("/dev/kbd", keyboard_pipe);

	/* Disable both ports. */
	ps2_command(PS2_DISABLE_PORT1);
	ps2_command(PS2_DISABLE_PORT2);

	/* Clear the input buffer. */
	size_t timeout = 1024; /* Can't imagine a buffer with more than that being full... */
	while ((inportb(PS2_STATUS) & 1) && timeout > 0) {
		timeout--;
		inportb(PS2_DATA);
	}

	if (timeout == 0) {
		printf("ps2hid: probably don't actually have PS/2.\n");
		return;
	}

	/* Enable interrupt lines, enable translation. */
	status = ps2_command_response(PS2_READ_CONFIG);
	status |= (PS2_PORT1_IRQ | PS2_PORT2_IRQ | PS2_PORT1_TLATE);
	ps2_command_arg(PS2_WRITE_CONFIG, status);

	/* Re-enable ports */
	ps2_command(PS2_ENABLE_PORT1);
	ps2_command(PS2_ENABLE_PORT2);

	/* Set scancode mode to 2... which then gives us 1 with translation... */
	kbd_write(KBD_SET_SCANCODE);
	kbd_write(2);

	/* Now we'll configure the mouse... */
	mouse_write(MOUSE_SET_DEFAULTS);
	mouse_write(MOUSE_DATA_ON);

	/* Try to enable scroll wheel (but not buttons) */
	if (!args_present("nomousescroll")) {
		mouse_write(MOUSE_DEVICE_ID);
		ps2_read_byte(); /* Ignore response */
		mouse_write(MOUSE_SAMPLE_RATE);
		mouse_write(200);
		mouse_write(MOUSE_SAMPLE_RATE);
		mouse_write(100);
		mouse_write(MOUSE_SAMPLE_RATE);
		mouse_write(80);
		mouse_write(MOUSE_DEVICE_ID);
		result = ps2_read_byte();
		if (result == 3) {
			mouse_mode = MOUSE_SCROLLWHEEL;
		}
	}

	if (args_present("sharedps2")) {
		irq_install_handler(KEYBOARD_IRQ, shared_handler, "ps2hid");
		irq_install_handler(MOUSE_IRQ,    shared_handler, "ps2hid");
	} else {
		irq_install_handler(KEYBOARD_IRQ, keyboard_handler, "ps2hid");
		irq_install_handler(MOUSE_IRQ,    mouse_handler,    "ps2hid");
	}
}

