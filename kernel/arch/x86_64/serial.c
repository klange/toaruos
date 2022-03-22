/**
 * @file  kernel/arch/x86_64/serial.c
 * @brief PC serial port driver.
 *
 * Attaches serial ports to TTY interfaces. Serial input processing
 * happens in a kernel tasklet so that blocking is handled smoothly.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014-2021 K. Lange
 */
#include <kernel/string.h>
#include <kernel/types.h>
#include <kernel/vfs.h>
#include <kernel/pipe.h>
#include <kernel/process.h>
#include <kernel/printf.h>
#include <kernel/args.h>
#include <kernel/pty.h>
#include <kernel/arch/x86_64/regs.h>
#include <kernel/arch/x86_64/ports.h>
#include <kernel/arch/x86_64/irq.h>

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
	while (serial_rcvd(device) == 0) switch_task(1);
	return inportb(device);
}

static int serial_transmit_empty(int device) {
	return inportb(device + 5) & 0x20;
}

static void serial_send(int device, char out) {
	while (serial_transmit_empty(device) == 0) switch_task(1);
	outportb(device, out);
}

static process_t * serial_ac_handler = NULL;
static process_t * serial_bd_handler = NULL;

static void process_serial(void * argp) {
	int portBase = (uintptr_t)argp;
	int _did_something;
	while (1) {
		unsigned long s, ss;
		relative_time(1, 0, &s, &ss);
		sleep_until((process_t *)this_core->current_process, s, ss);
		switch_task(0);
		_did_something = 1;
		while (_did_something) {
			_did_something = 0;
			int port_a_status = inportb(portBase + 5);
			int port_c_status = inportb(portBase - 0x10 + 5);

			if (port_a_status != 0xFF && (port_a_status & 1)) {
				char ch = inportb(portBase);
				pty_t * pty = *pty_for_port(portBase);
				tty_input_process(pty, ch);
				_did_something = 1;
			}

			if (port_c_status != 0xFF && (port_c_status & 1)) {
				char ch = inportb(portBase - 0x10);
				pty_t * pty = *pty_for_port(portBase - 0x10);
				tty_input_process(pty, ch);
				_did_something = 1;
			}
		}
	}
}

int serial_handler_ac(struct regs *r) {
	irq_ack(SERIAL_IRQ_AC);
	make_process_ready(serial_ac_handler);
	return 1;
}

int serial_handler_bd(struct regs *r) {
	irq_ack(SERIAL_IRQ_BD);
	make_process_ready(serial_bd_handler);
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
	if (pty == _serial_port_pty_a) snprintf(name, 100, DEV_PATH TTY_A);
	if (pty == _serial_port_pty_b) snprintf(name, 100, DEV_PATH TTY_B);
	if (pty == _serial_port_pty_c) snprintf(name, 100, DEV_PATH TTY_C);
	if (pty == _serial_port_pty_d) snprintf(name, 100, DEV_PATH TTY_D);
}

static fs_node_t * serial_device_create(int port) {
	pty_t * pty = pty_new(NULL, 0);
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

	pty->slave->gid = 2; /* dialout group */
	pty->slave->mask = 0660;

	return pty->slave;
}

void serial_initialize(void) {
	serial_ac_handler = spawn_worker_thread(process_serial, "[serial ac]", (void*)(uintptr_t)SERIAL_PORT_A);
	serial_bd_handler = spawn_worker_thread(process_serial, "[serial bd]", (void*)(uintptr_t)SERIAL_PORT_B);

	fs_node_t * ttyS0 = serial_device_create(SERIAL_PORT_A); vfs_mount(DEV_PATH TTY_A, ttyS0);
	fs_node_t * ttyS1 = serial_device_create(SERIAL_PORT_B); vfs_mount(DEV_PATH TTY_B, ttyS1);
	fs_node_t * ttyS2 = serial_device_create(SERIAL_PORT_C); vfs_mount(DEV_PATH TTY_C, ttyS2);
	fs_node_t * ttyS3 = serial_device_create(SERIAL_PORT_D); vfs_mount(DEV_PATH TTY_D, ttyS3);
}
