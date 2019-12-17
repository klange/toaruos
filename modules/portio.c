/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 *
 * provides /dev/port
 *
 */

#include <kernel/system.h>
#include <kernel/fs.h>
#include <kernel/module.h>

static uint32_t read_port(fs_node_t *node, uint64_t offset, uint32_t size, uint8_t *buffer) {
	switch (size) {
		case 1:
			buffer[0] = inportb(offset);
			break;
		case 2:
			((uint16_t *)buffer)[0] = inports(offset);
			break;
		case 4:
			((uint32_t *)buffer)[0] = inportl(offset);
			break;
		default:
			for (unsigned int i = 0; i < size; ++i) {
				buffer[i] = inportb(offset + i);
			}
			break;
	}

	return size;
}

static uint32_t write_port(fs_node_t *node, uint64_t offset, uint32_t size, uint8_t *buffer) {
	switch (size) {
		case 1:
			outportb(offset, buffer[0]);
			break;
		case 2:
			outports(offset, ((uint16_t*)buffer)[0]);
			break;
		case 4:
			outportl(offset, ((uint32_t*)buffer)[0]);
			break;
		default:
			for (unsigned int i = 0; i < size; ++i) {
				outportb(offset +i, buffer[i]);
			}
			break;
	}

	return size;
}

static fs_node_t * port_device_create(void) {
	fs_node_t * fnode = malloc(sizeof(fs_node_t));
	memset(fnode, 0x00, sizeof(fs_node_t));
	fnode->inode = 0;
	strcpy(fnode->name, "port");
	fnode->uid = 0;
	fnode->gid = 0;
	fnode->mask = 0660;
	fnode->flags   = FS_BLOCKDEVICE;
	fnode->read    = read_port;
	fnode->write   = write_port;
	fnode->open    = NULL;
	fnode->close   = NULL;
	fnode->readdir = NULL;
	fnode->finddir = NULL;
	fnode->ioctl   = NULL;
	return fnode;
}

static int port_initialize(void) {
	vfs_mount("/dev/port", port_device_create());
	return 0;
}

static int port_finalize(void) {
	return 0;
}

MODULE_DEF(portio, port_initialize, port_finalize);
