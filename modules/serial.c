/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014 Kevin Lange
 *
 * Serial communication device
 *
 */

#include <system.h>
#include <fs.h>
#include <pipe.h>
#include <logging.h>
#include <args.h>
#include <module.h>

#define SERIAL_PORT_A 0x3F8
#define SERIAL_PORT_B 0x2F8
#define SERIAL_PORT_C 0x3E8
#define SERIAL_PORT_D 0x2E8

#define SERIAL_IRQ_AC 4
#define SERIAL_IRQ_BD 3

static char serial_recv(int device);

static fs_node_t * _serial_port_a = NULL;
static fs_node_t * _serial_port_b = NULL;
static fs_node_t * _serial_port_c = NULL;
static fs_node_t * _serial_port_d = NULL;

static uint8_t convert(uint8_t in) {
	switch (in) {
		case 0x7F:
			return 0x08;
		case 0x0D:
			return '\n';
		default:
			return in;
	}
}

static fs_node_t ** pipe_for_port(int port) {
	switch (port) {
		case SERIAL_PORT_A: return &_serial_port_a;
		case SERIAL_PORT_B: return &_serial_port_b;
		case SERIAL_PORT_C: return &_serial_port_c;
		case SERIAL_PORT_D: return &_serial_port_d;
	}
	return NULL;
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
	uint8_t buf[] = {convert(serial), 0};
	write_fs(*pipe_for_port(port), 0, 1, buf);
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
	uint8_t buf[] = {convert(serial), 0};
	write_fs(*pipe_for_port(port), 0, 1, buf);
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

static void serial_string(char * out) {
	for (uint32_t i = 0; i < strlen(out); ++i) {
		serial_send(SERIAL_PORT_A, out[i]);
	}
}

static uint32_t read_serial(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
static uint32_t write_serial(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
static void open_serial(fs_node_t *node, unsigned int flags);
static void close_serial(fs_node_t *node);

static uint32_t read_serial(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	return read_fs(*pipe_for_port((int)node->device), offset, size, buffer);
}

static uint32_t write_serial(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	uint32_t sent = 0;
	while (sent < size) {
		serial_send((int)node->device, buffer[sent]);
		sent++;
	}
	return size;
}

static void open_serial(fs_node_t * node, unsigned int flags) {
	return;
}

static void close_serial(fs_node_t * node) {
	return;
}

static fs_node_t * serial_device_create(int device) {
	fs_node_t * fnode = malloc(sizeof(fs_node_t));
	memset(fnode, 0x00, sizeof(fs_node_t));
	fnode->device= (void *)device;
	strcpy(fnode->name, "serial");
	fnode->uid = 0;
	fnode->gid = 0;
	fnode->flags   = FS_CHARDEVICE;
	fnode->read    = read_serial;
	fnode->write   = write_serial;
	fnode->open    = open_serial;
	fnode->close   = close_serial;
	fnode->readdir = NULL;
	fnode->finddir = NULL;
	fnode->ioctl   = NULL; /* TODO ioctls for raw serial devices */

	fnode->atime = now();
	fnode->mtime = fnode->atime;
	fnode->ctime = fnode->atime;

	serial_enable(device);

	if (device == SERIAL_PORT_A || device == SERIAL_PORT_C) {
		irq_install_handler(SERIAL_IRQ_AC, serial_handler_ac);
	} else {
		irq_install_handler(SERIAL_IRQ_BD, serial_handler_bd);
	}

	*pipe_for_port(device) = make_pipe(128);

	return fnode;
}

static int serial_mount_devices(void) {

	fs_node_t * ttyS0 = serial_device_create(SERIAL_PORT_A);
	vfs_mount("/dev/ttyS0", ttyS0);

	fs_node_t * ttyS1 = serial_device_create(SERIAL_PORT_B);
	vfs_mount("/dev/ttyS1", ttyS1);

	fs_node_t * ttyS2 = serial_device_create(SERIAL_PORT_C);
	vfs_mount("/dev/ttyS2", ttyS2);

	fs_node_t * ttyS3 = serial_device_create(SERIAL_PORT_D);
	vfs_mount("/dev/ttyS3", ttyS3);

	char * c;
	if ((c = args_value("logtoserial"))) {
		debug_file = ttyS0;
		debug_level = atoi(c);
		debug_print(NOTICE, "Serial logging enabled at level %d.", debug_level);
	}


	return 0;
}

static int serial_finalize(void) {
	return 0;
}

MODULE_DEF(serial, serial_mount_devices, serial_finalize);
