/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014 Kevin Lange
 */
#include <system.h>
#include <logging.h>
#include <module.h>
#include <printf.h>
#include <ata.h>

#define SECTORSIZE      512

static mbr_t mbr;

struct dos_partition_entry {
	fs_node_t * device;
	partition_t partition;
};

static uint32_t read_part(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	struct dos_partition_entry * device = (struct dos_partition_entry *)node->device;

	if (offset > device->partition.sector_count * SECTORSIZE) {
		debug_print(WARNING, "Read beyond partition!");
		return 0;
	}

	if (offset + size > device->partition.sector_count * SECTORSIZE) {
		size = device->partition.sector_count * SECTORSIZE - offset;
		debug_print(WARNING, "Tried to read past end of partition, clamped to %d", size);
	}

	return read_fs(device->device, offset + device->partition.lba_first_sector * SECTORSIZE, size, buffer);
}

static uint32_t write_part(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	struct dos_partition_entry * device = (struct dos_partition_entry *)node->device;

	if (offset > device->partition.sector_count * SECTORSIZE) {
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

static fs_node_t * dospart_device_create(int i, fs_node_t * dev, partition_t * part) {

	vfs_lock(dev);

	struct dos_partition_entry * device = malloc(sizeof(struct dos_partition_entry));
	memcpy(&device->partition, part, sizeof(partition_t));
	device->device = dev;

	fs_node_t * fnode = malloc(sizeof(fs_node_t));
	memset(fnode, 0x00, sizeof(fs_node_t));
	fnode->inode = 0;
	sprintf(fnode->name, "dospart%d", i);
	fnode->device  = device;
	fnode->uid = 0;
	fnode->gid = 0;
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

static int read_partition_map(char * name) {
	fs_node_t * device = kopen(name, 0);
	if (!device) return 1;

	read_fs(device, 0, SECTORSIZE, (uint8_t *)&mbr);

	if (mbr.signature[0] == 0x55 && mbr.signature[1] == 0xAA) {
		debug_print(INFO, "Partition table found.");

		for (int i = 0; i < 4; ++i) {
			if (mbr.partitions[i].status & 0x80) {
				debug_print(NOTICE, "Partition #%d: @%d+%d", i+1, mbr.partitions[i].lba_first_sector, mbr.partitions[i].sector_count);
				fs_node_t * node = dospart_device_create(i, device, &mbr.partitions[i]);

				char tmp[64];
				sprintf(tmp, "%s%d", name, i);
				vfs_mount(tmp, node);
			} else {
				debug_print(NOTICE, "Partition #%d: inactive", i+1);
			}
		}
	} else {
		debug_print(NOTICE, "No partition table on %s", name);
	}

	return 0;
}

static int dospart_initialize(void) {
	for (char l = 'a'; l < 'z'; ++l) {
		char name[64];
		sprintf(name, "/dev/hd%c", l);
		if (read_partition_map(name)) {
			break;
		}
	}
	return 0;
}

static int dospart_finalize(void) {
	return 0;
}

MODULE_DEF(dospart, dospart_initialize, dospart_finalize);
