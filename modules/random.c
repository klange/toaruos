/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014 Kevin Lange
 *
 * Provides access to the kernel RNG
 *
 */

#include <system.h>
#include <logging.h>
#include <fs.h>

#include <module.h>

static uint32_t read_random(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
static uint32_t write_random(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
static void open_random(fs_node_t *node, unsigned int flags);
static void close_random(fs_node_t *node);

static uint32_t read_random(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	uint32_t s = 0;
	while (s < size) {
		buffer[s] = krand() % 0xFF;
		offset++;
		s++;
	}
	return size;
}

static uint32_t write_random(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	return size;
}

static void open_random(fs_node_t * node, unsigned int flags) {
	return;
}

static void close_random(fs_node_t * node) {
	return;
}

static fs_node_t * random_device_create(void) {
	fs_node_t * fnode = malloc(sizeof(fs_node_t));
	memset(fnode, 0x00, sizeof(fs_node_t));
	fnode->inode = 0;
	strcpy(fnode->name, "random");
	fnode->uid = 0;
	fnode->gid = 0;
	fnode->length  = 1024;
	fnode->flags   = FS_CHARDEVICE;
	fnode->read    = read_random;
	fnode->write   = write_random;
	fnode->open    = open_random;
	fnode->close   = close_random;
	fnode->readdir = NULL;
	fnode->finddir = NULL;
	fnode->ioctl   = NULL;
	return fnode;
}

static int random_initialize(void) {
	vfs_mount("/dev/random", random_device_create());
	vfs_mount("/dev/urandom", random_device_create());
	return 0;
}

static int random_finalize(void) {
	return 0;
}

MODULE_DEF(random, random_initialize, random_finalize);
