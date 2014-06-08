/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014 Kevin Lange
 *
 * Null Device
 *
 */

#include <system.h>
#include <fs.h>
#include <module.h>

static uint32_t read_null(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
static uint32_t write_null(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
static void open_null(fs_node_t *node, unsigned int flags);
static void close_null(fs_node_t *node);
static uint32_t read_zero(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
static uint32_t write_zero(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
static void open_zero(fs_node_t *node, unsigned int flags);
static void close_zero(fs_node_t *node);

static uint32_t read_null(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	return 0;
}

static uint32_t write_null(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	return 0;
}

static void open_null(fs_node_t * node, unsigned int flags) {
	return;
}

static void close_null(fs_node_t * node) {
	return;
}

static uint32_t read_zero(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	memset(buffer, 0x00, size);
	return 1;
}

static uint32_t write_zero(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	return 0;
}

static void open_zero(fs_node_t * node, unsigned int flags) {
	return;
}

static void close_zero(fs_node_t * node) {
	return;
}

static fs_node_t * null_device_create(void) {
	fs_node_t * fnode = malloc(sizeof(fs_node_t));
	memset(fnode, 0x00, sizeof(fs_node_t));
	fnode->inode = 0;
	strcpy(fnode->name, "null");
	fnode->uid = 0;
	fnode->gid = 0;
	fnode->flags   = FS_CHARDEVICE;
	fnode->read    = read_null;
	fnode->write   = write_null;
	fnode->open    = open_null;
	fnode->close   = close_null;
	fnode->readdir = NULL;
	fnode->finddir = NULL;
	fnode->ioctl   = NULL;
	return fnode;
}

static fs_node_t * zero_device_create(void) {
	fs_node_t * fnode = malloc(sizeof(fs_node_t));
	memset(fnode, 0x00, sizeof(fs_node_t));
	fnode->inode = 0;
	strcpy(fnode->name, "zero");
	fnode->uid = 0;
	fnode->gid = 0;
	fnode->flags   = FS_CHARDEVICE;
	fnode->read    = read_zero;
	fnode->write   = write_zero;
	fnode->open    = open_zero;
	fnode->close   = close_zero;
	fnode->readdir = NULL;
	fnode->finddir = NULL;
	fnode->ioctl   = NULL;
	return fnode;
}

static int zero_initialize(void) {
	vfs_mount("/dev/null", null_device_create());
	vfs_mount("/dev/zero", zero_device_create());
	return 0;
}

static int zero_finalize(void) {
	return 0;
}


MODULE_DEF(zero, zero_initialize, zero_finalize);
