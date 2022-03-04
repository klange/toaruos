/**
 * @file  kernel/vfs/ramdisk.c
 * @brief VFS wrapper for physical memory blocks.
 *
 * Allows raw physical memory blocks provided by the loader to be
 * used like a block file. Used to provide multiboot payloads
 * as /dev/ram* files.
 *
 * Note that the ramdisk driver really does deal with physical
 * memory addresses, not virtual address, and once a block of
 * pages has been handed over to the ramdisk driver it is owned
 * by the ramdisk driver which may mark those pages as available
 * (via an ioctl request).
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014-2021 K. Lange
 */

#include <errno.h>
#include <kernel/types.h>
#include <kernel/vfs.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/process.h>
#include <kernel/mmu.h>

static ssize_t read_ramdisk(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer);
static ssize_t write_ramdisk(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer);
static void     open_ramdisk(fs_node_t *node, unsigned int flags);
static void     close_ramdisk(fs_node_t *node);

static ssize_t read_ramdisk(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {

	if ((size_t)offset > node->length) {
		return 0;
	}

	if ((size_t)offset + size > node->length) {
		size_t i = node->length - offset;
		size = i;
	}

	memcpy(buffer, (void *)((uintptr_t)mmu_map_from_physical(node->inode) + (uintptr_t)offset), size);

	return size;
}

static ssize_t write_ramdisk(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
	if ((size_t)offset > node->length) {
		return 0;
	}

	if (offset + size > node->length) {
		unsigned int i = node->length - offset;
		size = i;
	}

	memcpy((void *)((uintptr_t)mmu_map_from_physical(node->inode) + (uintptr_t)offset), buffer, size);
	return size;
}

static void open_ramdisk(fs_node_t * node, unsigned int flags) {
	return;
}

static void close_ramdisk(fs_node_t * node) {
	return;
}

static int ioctl_ramdisk(fs_node_t * node, unsigned long request, void * argp) {
	switch (request) {
		case 0x4001:
			if (this_core->current_process->user != 0) {
				return -EPERM;
			} else {
				/* Clear all of the memory used by this ramdisk */
				if (node->length >= 0x1000) {
					if (node->length % 0x1000) {
						/* It would be a very bad idea to wipe the wrong page here. */
						node->length -= node->length % 0x1000;
					}
					for (uintptr_t i = node->inode; i < (node->inode + node->length); i += 0x1000) {
						mmu_frame_release(i);
					}
				}
				/* Mark the file length as 0 */
				node->length = 0;
				((fs_node_t*)node->device)->length = 0;
				return 0;
			}
		default:
			return -EINVAL;
	}
	return -1;
}

static fs_node_t * ramdisk_device_create(int device_number, uintptr_t location, size_t size) {
	fs_node_t * fnode = malloc(sizeof(fs_node_t));
	memset(fnode, 0x00, sizeof(fs_node_t));
	fnode->inode = location;
	snprintf(fnode->name, 10, "ram%d", device_number);
	fnode->device = fnode; /* stupid vfs */
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
		snprintf(tmp, 63, "/dev/%s", ramdisk->name);
		vfs_mount(tmp, ramdisk);
		last_device_number += 1;
		return ramdisk;
	}

	return NULL;
}

