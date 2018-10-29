/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014-2018 K. Lange
 *
 * Serial communication device
 *
 */

#include <kernel/system.h>
#include <kernel/fs.h>
#include <kernel/pipe.h>
#include <kernel/logging.h>
#include <kernel/args.h>
#include <kernel/module.h>
#include <kernel/pty.h>
#include <kernel/printf.h>

#define SERIAL_PORT_A 0x3F8
#define SERIAL_PORT_B 0x2F8
#define SERIAL_PORT_C 0x3E8
#define SERIAL_PORT_D 0x2E8

#define SERIAL_IRQ_AC 4
#define SERIAL_IRQ_BD 3

static pty_t * _serial_port_pty_a = NULL;
static pty_t * _serial_port_pty_b = NULL;
static pty_t * _serial_port_pty_c = NULL;
static pty_t * _serial_port_pty_d = NULL;

static pty_t ** pty_for_port(int port) {
	switch (port) {
		case SERIAL_PORT_A: return &_serial_port_pty_a;
		case SERIAL_PORT_B: return &_serial_port_pty_b;
		case SERIAL_PORT_C: return &_serial_port_pty_c;
		case SERIAL_PORT_D: return &_serial_port_pty_d;
	}
	__builtin_unreachable();
}

static int serial_rcvd(int device) {
	return inportb(device + 5) & 1;
}

static char serial_recv(int device) {
	while (serial_rcvd(device) == 0) ;
	return inportb(device);
}

static int serial_transmit_empty(int device) {
	return inportb(device + 5) & 0x20;
}

static void serial_send(int device, char out) {
	while (serial_transmit_empty(device) == 0);
	outportb(device, out);
}

static int serial_handler_ac(struct regs *r) {
	char serial;
	int  port = 0;
	if (inportb(SERIAL_PORT_A+1) & 0x01) {
		port = SERIAL_PORT_A;
	} else {
		port = SERIAL_PORT_C;
	}
	serial = serial_recv(port);
	irq_ack(SERIAL_IRQ_AC);
	tty_input_process(*pty_for_port(port), serial);
	return 1;
}

static int serial_handler_bd(struct regs *r) {
	char serial;
	int  port = 0;
	debug_print(NOTICE, "Received something on secondary port");
	if (inportb(SERIAL_PORT_B+1) & 0x01) {
		port = SERIAL_PORT_B;
	} else {
		port = SERIAL_PORT_D;
	}
	serial = serial_recv(port);
	irq_ack(SERIAL_IRQ_BD);
	tty_input_process(*pty_for_port(port), serial);
	return 1;
}

static void serial_enable(int port) {
	outportb(port + 1, 0x00); /* Disable interrupts */
	outportb(port + 3, 0x80); /* Enable divisor mode */
	outportb(port + 0, 0x01); /* Div Low:  01 Set the port to 115200 bps */
	outportb(port + 1, 0x00); /* Div High: 00 */
	outportb(port + 3, 0x03); /* Disable divisor mode, set parity */
	outportb(port + 2, 0xC7); /* Enable FIFO and clear */
	outportb(port + 4, 0x0B); /* Enable interrupts */
	outportb(port + 1, 0x01); /* Enable interrupts */
}

static int have_installed_ac = 0;
static int have_installed_bd = 0;

static void serial_write_out(pty_t * pty, uint8_t c) {
	if (pty == _serial_port_pty_a) serial_send(SERIAL_PORT_A, c);
	if (pty == _serial_port_pty_b) serial_send(SERIAL_PORT_B, c);
	if (pty == _serial_port_pty_c) serial_send(SERIAL_PORT_C, c);
	if (pty == _serial_port_pty_d) serial_send(SERIAL_PORT_D, c);
}

#define DEV_PATH "/dev/"
#define TTY_A "ttyS0"
#define TTY_B "ttyS1"
#define TTY_C "ttyS2"
#define TTY_D "ttyS3"

static void serial_fill_name(pty_t * pty, char * name) {
	if (pty == _serial_port_pty_a) sprintf(name, DEV_PATH TTY_A);
	if (pty == _serial_port_pty_b) sprintf(name, DEV_PATH TTY_B);
	if (pty == _serial_port_pty_c) sprintf(name, DEV_PATH TTY_C);
	if (pty == _serial_port_pty_d) sprintf(name, DEV_PATH TTY_D);
}

static fs_node_t * serial_device_create(int port) {
	pty_t * pty = pty_new(NULL);
	*pty_for_port(port) = pty;
	pty->write_out = serial_write_out;
	pty->fill_name = serial_fill_name;

	serial_enable(port);

	if (port == SERIAL_PORT_A || port == SERIAL_PORT_C) {
		if (!have_installed_ac) {
			irq_install_handler(SERIAL_IRQ_AC, serial_handler_ac, "serial ac");
			have_installed_ac = 1;
		}
	} else {
		if (!have_installed_bd) {
			irq_install_handler(SERIAL_IRQ_BD, serial_handler_bd, "serial bd");
			have_installed_bd = 1;
		}
	}

	return pty->slave;
}

static int serial_mount_devices(void) {

	fs_node_t * ttyS0 = serial_device_create(SERIAL_PORT_A); vfs_mount(DEV_PATH TTY_A, ttyS0);
	fs_node_t * ttyS1 = serial_device_create(SERIAL_PORT_B); vfs_mount(DEV_PATH TTY_B, ttyS1);
	fs_node_t * ttyS2 = serial_device_create(SERIAL_PORT_C); vfs_mount(DEV_PATH TTY_C, ttyS2);
	fs_node_t * ttyS3 = serial_device_create(SERIAL_PORT_D); vfs_mount(DEV_PATH TTY_D, ttyS3);

	char * c;
	if ((c = args_value("logtoserial"))) {
		debug_file = ttyS0;
		if (!strcmp(c,"INFO") || !strcmp(c,"info")) {
			debug_level = INFO;
		} else if (!strcmp(c,"NOTICE") || !strcmp(c,"notice")) {
			debug_level = NOTICE;
		} else if (!strcmp(c,"WARNING") || !strcmp(c,"warning")) {
			debug_level = WARNING;
		} else if (!strcmp(c,"ERROR") || !strcmp(c,"error")) {
			debug_level = ERROR;
		} else if (!strcmp(c,"CRITICAL") || !strcmp(c,"critical")) {
			debug_level = CRITICAL;
		} else if (!strcmp(c,"INSANE") || !strcmp(c,"insane")) {
			debug_level = INSANE;
		} else {
			debug_level = atoi(c);
		}
		debug_print(NOTICE, "Serial logging enabled at level %d.", debug_level);
	}


	return 0;
}

static int serial_finalize(void) {
	return 0;
}

MODULE_DEF(serial, serial_mount_devices, serial_finalize);
