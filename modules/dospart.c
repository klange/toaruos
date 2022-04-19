/**
 * @file modules/dospart.c
 * @brief DOS MBR partition table mapper
 * @package x86_64
 * @package aarch64
 *
 * Provides partition entries for disks.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014-2021 K. Lange
 */
#include <kernel/types.h>
#include <kernel/module.h>
#include <kernel/vfs.h>
#include <kernel/printf.h>
#include <kernel/tokenize.h>

#define SECTORSIZE      512

typedef struct {
	uint8_t  status;
	uint8_t  chs_first_sector[3];
	uint8_t  type;
	uint8_t  chs_last_sector[3];
	uint32_t lba_first_sector;
	uint32_t sector_count;
} partition_t;

typedef struct {
	uint8_t     boostrap[446];
	partition_t partitions[4];
	uint8_t     signature[2];
} __attribute__((packed)) mbr_t;

struct dos_partition_entry {
	fs_node_t * device;
	partition_t partition;
};

static ssize_t read_part(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
	struct dos_partition_entry * device = (struct dos_partition_entry *)node->device;

	if ((size_t)offset > device->partition.sector_count * SECTORSIZE) {
		return 0;
	}

	if (offset + size > device->partition.sector_count * SECTORSIZE) {
		size = device->partition.sector_count * SECTORSIZE - offset;
	}

	return read_fs(device->device, offset + device->partition.lba_first_sector * SECTORSIZE, size, buffer);
}

static ssize_t write_part(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
	struct dos_partition_entry * device = (struct dos_partition_entry *)node->device;

	if ((size_t)offset > device->partition.sector_count * SECTORSIZE) {
		return 0;
	}

	if (offset + size > device->partition.sector_count * SECTORSIZE) {
		size = device->partition.sector_count * SECTORSIZE - offset;
	}

	return write_fs(device->device, offset + device->partition.lba_first_sector * SECTORSIZE, size, buffer);
}

static void open_part(fs_node_t * node, unsigned int flags) {
	return;
}

static void close_part(fs_node_t * node) {
	return;
}

static fs_node_t * dospart_device_create(int i, fs_node_t * dev, mbr_t * mbr, int id) {

	vfs_lock(dev);

	struct dos_partition_entry * device = malloc(sizeof(struct dos_partition_entry));
	memcpy(&device->partition, &mbr->partitions[id], sizeof(partition_t));
	device->device = dev;

	fs_node_t * fnode = malloc(sizeof(fs_node_t));
	memset(fnode, 0x00, sizeof(fs_node_t));
	fnode->inode = 0;
	snprintf(fnode->name, 20, "dospart%d", i);
	fnode->device  = device;
	fnode->uid = 0;
	fnode->gid = 0;
	fnode->mask    = 0660;
	fnode->length  = device->partition.sector_count * SECTORSIZE; /* TODO */
	fnode->flags   = FS_BLOCKDEVICE;
	fnode->read    = read_part;
	fnode->write   = write_part;
	fnode->open    = open_part;
	fnode->close   = close_part;
	fnode->readdir = NULL;
	fnode->finddir = NULL;
	fnode->ioctl   = NULL; /* TODO, identify, etc? */
	return fnode;
}

static fs_node_t * dospart_map(const char * device, const char * mount_path) {
	char * arg = strdup(device);
	char * argv[10];
	tokenize(arg, ",", argv);

	fs_node_t * dev = kopen(argv[0], 0);
	if (!dev) {
		return NULL;
	}

	mbr_t mbr;
	read_fs(dev, 0, SECTORSIZE, (uint8_t *)&mbr);

	if (mbr.signature[0] == 0x55 && mbr.signature[1] == 0xAA) {
		for (int i = 0; i < 4; ++i) {
			if (mbr.partitions[i].status & 0x80) {
				fs_node_t * node = dospart_device_create(i, dev, &mbr, i);
				char tmp[64];
				snprintf(tmp, 20, "%s%d", device, i);
				vfs_mount(tmp, node);
			}
		}
	}

	/* VFS_MOUNT_PARTITION_MAPPER_SUCCESS? */
	return (fs_node_t*)1;
}

static int dospart_initialize(int argc, char * argv[]) {
	vfs_register("mbr", dospart_map);
	return 0;
}

static int dospart_finalize(void) {
	return 0;
}

struct Module metadata = {
	.name = "dospart",
	.init = dospart_initialize,
	.fini = dospart_finalize,
};

