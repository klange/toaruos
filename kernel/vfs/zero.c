/**
 * @file  kernel/vfs/zero.c
 * @brief /dev/null and /dev/zero provider.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014-2018 K. Lange
 */

#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <kernel/vfs.h>
#include <kernel/string.h>

static ssize_t read_null(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
	return 0;
}

static ssize_t write_null(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
	return size;
}

static void open_null(fs_node_t * node, unsigned int flags) {
	return;
}

static void close_null(fs_node_t * node) {
	return;
}

static ssize_t read_zero(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
	memset(buffer, 0x00, size);
	return size;
}

static ssize_t write_zero(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
	return size;
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
	fnode->mask = 0666;
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
	fnode->mask = 0666;
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

void zero_initialize(void) {
	vfs_mount("/dev/null", null_device_create());
	vfs_mount("/dev/zero", zero_device_create());
}

