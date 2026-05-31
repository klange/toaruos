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
#include <bits/errno.h>
#include <kernel/vfs.h>
#include <kernel/string.h>

static ssize_t read_null(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
	return 0;
}

static ssize_t write_null_zero(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
	return size;
}

static ssize_t read_zero(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
	memset(buffer, 0x00, size);
	return size;
}

static fs_node_t * null_device_create(void) {
	fs_node_t * fnode = calloc(1, sizeof(fs_node_t));
	strcpy(fnode->name, "null");
	fnode->mask = 0666;
	fnode->flags   = FS_CHARDEVICE;
	fnode->read    = read_null;
	fnode->write   = write_null_zero;
	return fnode;
}

static fs_node_t * zero_device_create(void) {
	fs_node_t * fnode = calloc(1, sizeof(fs_node_t));
	strcpy(fnode->name, "zero");
	fnode->mask = 0666;
	fnode->flags   = FS_CHARDEVICE;
	fnode->read    = read_zero;
	fnode->write   = write_null_zero;
	return fnode;
}

void zero_initialize(void) {
	vfs_mount("/dev/null", null_device_create(), "null", "");
	vfs_mount("/dev/zero", zero_device_create(), "zero", "");
}

