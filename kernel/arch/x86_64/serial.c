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

struct serial_port_map {
	int port;
	pty_t * pty;
	int index;

	tcflag_t cflags;
};

static struct serial_port_map serial_ports[4] = {
	{ SERIAL_PORT_A, NULL, 0, 0 },
	{ SERIAL_PORT_B, NULL, 1, 0 },
	{ SERIAL_PORT_C, NULL, 2, 0 },
	{ SERIAL_PORT_D, NULL, 3, 0 },
};

static struct serial_port_map * map_entry_for_port(int port) {
	switch (port) {
		case SERIAL_PORT_A: return &serial_ports[0];
		case SERIAL_PORT_B: return &serial_ports[1];
		case SERIAL_PORT_C: return &serial_ports[2];
		case SERIAL_PORT_D: return &serial_ports[3];
	}
	__builtin_unreachable();
}

static pty_t ** pty_for_port(int port) {
	return &map_entry_for_port(port)->pty;
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

#define BASE 115200
#define D(n) { B ## n, BASE / n }
static struct divisor {
	speed_t baud;
	uint16_t div;
} divisors[] = {
	{ B0,  0 },
	D(50), D(75), D(110),
	{ B134, BASE * 10 / 1345 },
	D(150), D(200), D(300), D(600), D(1200),
	D(1800), D(2400), D(4800), D(9600), D(19200),
	D(38400), D(57600), D(115200),
};
#undef D

static void serial_enable(int port, tcflag_t cflags) {
	outportb(port + 1, 0x00); /* Disable interrupts */
	outportb(port + 3, 0x80); /* Enable divisor mode */

	uint16_t divisor = 0;
	for (size_t i = 0; i < sizeof(divisors) / sizeof(*divisors); ++i) {
		if ((cflags & CBAUD) == divisors[i].baud) {
			divisor = divisors[i].div;
			break;
		}
	}

	outportb(port + 0, divisor & 0xFF); /* Div Low */
	outportb(port + 1, divisor >> 8);   /* Div High */

	uint8_t line_ctl = 0;
	if (cflags & PARENB) {
		line_ctl |= (1 << 3); /* Enable parity */
		if (!(cflags & PARODD)) line_ctl |= (1 << 4); /* Even parity */
	}

	/* Size */
	switch (cflags & CSIZE) {
		case CS5: line_ctl |= 0; break;
		case CS6: line_ctl |= 1; break;
		case CS7: line_ctl |= 2; break;
		case CS8: line_ctl |= 3; break;
	}

	outportb(port + 3, line_ctl); /* set line mode */
	outportb(port + 2, 0xC7); /* Enable FIFO and clear */
	outportb(port + 4, 0x0B); /* Enable interrupts */
	outportb(port + 1, 0x01); /* Enable interrupts */
}

static int have_installed_ac = 0;
static int have_installed_bd = 0;

static void serial_write_out(pty_t * pty, uint8_t c) {
	struct serial_port_map * me = pty->_private;
	if (pty->tios.c_cflag != me->cflags) {
		me->cflags = pty->tios.c_cflag;
		serial_enable(me->port, pty->tios.c_cflag);
	}
	serial_send(me->port, c);
}

#define DEV_PATH "/dev/ttyS"

static void serial_fill_name(pty_t * pty, char * name) {
	snprintf(name, 100, DEV_PATH "%d", ((struct serial_port_map *)pty->_private)->index);
}

static fs_node_t * serial_device_create(int port) {
	pty_t * pty = pty_new(NULL, 0);

	map_entry_for_port(port)->pty = pty;
	pty->_private = map_entry_for_port(port);

	pty->write_out = serial_write_out;
	pty->fill_name = serial_fill_name;

	serial_enable(port, pty->tios.c_cflag);

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

	fs_node_t * ttyS0 = serial_device_create(SERIAL_PORT_A); vfs_mount(DEV_PATH "0", ttyS0);
	fs_node_t * ttyS1 = serial_device_create(SERIAL_PORT_B); vfs_mount(DEV_PATH "1", ttyS1);
	fs_node_t * ttyS2 = serial_device_create(SERIAL_PORT_C); vfs_mount(DEV_PATH "2", ttyS2);
	fs_node_t * ttyS3 = serial_device_create(SERIAL_PORT_D); vfs_mount(DEV_PATH "3", ttyS3);
}
