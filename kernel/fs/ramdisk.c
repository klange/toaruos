/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014-2018 K. Lange
  *
 * Ramdisk driver.
 *
 * Provide raw block access to files loaded into kernel memory.
 */

#include <kernel/system.h>
#include <kernel/logging.h>
#include <kernel/module.h>
#include <kernel/fs.h>
#include <kernel/printf.h>
#include <kernel/mem.h>

static uint32_t read_ramdisk(fs_node_t *node, uint64_t offset, uint32_t size, uint8_t *buffer);
static uint32_t write_ramdisk(fs_node_t *node, uint64_t offset, uint32_t size, uint8_t *buffer);
static void     open_ramdisk(fs_node_t *node, unsigned int flags);
static void     close_ramdisk(fs_node_t *node);

static uint32_t read_ramdisk(fs_node_t *node, uint64_t offset, uint32_t size, uint8_t *buffer) {

	if (offset > node->length) {
		return 0;
	}

	if (offset + size > node->length) {
		unsigned int i = node->length - offset;
		size = i;
	}

	memcpy(buffer, (void *)(node->inode + (uintptr_t)offset), size);

	return size;
}

static uint32_t write_ramdisk(fs_node_t *node, uint64_t offset, uint32_t size, uint8_t *buffer) {
	if (offset > node->length) {
		return 0;
	}

	if (offset + size > node->length) {
		unsigned int i = node->length - offset;
		size = i;
	}

	memcpy((void *)(node->inode + (uintptr_t)offset), buffer, size);
	return size;
}

static void open_ramdisk(fs_node_t * node, unsigned int flags) {
	return;
}

static void close_ramdisk(fs_node_t * node) {
	return;
}

static int ioctl_ramdisk(fs_node_t * node, int request, void * argp) {
	switch (request) {
		case 0x4001:
			if (current_process->user != 0) {
				return -EPERM;
			} else {
				/* Clear all of the memory used by this ramdisk */
				for (uintptr_t i = node->inode; i < node->inode + node->length; i += 0x1000) {
					clear_frame(i);
				}
				/* Mark the file length as 0 */
				node->length = 0;
				return 0;
			}
		default:
			return -EINVAL;
	}
}

static fs_node_t * ramdisk_device_create(int device_number, uintptr_t location, size_t size) {
	fs_node_t * fnode = malloc(sizeof(fs_node_t));
	memset(fnode, 0x00, sizeof(fs_node_t));
	fnode->inode = location;
	sprintf(fnode->name, "ram%d", device_number);
	fnode->uid = 0;
	fnode->gid = 0;
	fnode->mask    = 0770;
	fnode->length  = size;
	fnode->flags   = FS_BLOCKDEVICE;
	fnode->read    = read_ramdisk;
	fnode->write   = write_ramdisk;
	fnode->open    = open_ramdisk;
	fnode->close   = close_ramdisk;
	fnode->ioctl   = ioctl_ramdisk;
	return fnode;
}

static int last_device_number = 0;
fs_node_t * ramdisk_mount(uintptr_t location, size_t size) {
	fs_node_t * ramdisk = ramdisk_device_create(last_device_number, location, size);
	if (ramdisk) {
		char tmp[64];
		sprintf(tmp, "/dev/%s", ramdisk->name);
		vfs_mount(tmp, ramdisk);
		last_device_number += 1;
		return ramdisk;
	}

	return NULL;
}
