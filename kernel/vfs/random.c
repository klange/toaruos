/**
 * @file  kernel/vfs/random.c
 * @brief Bad RNG.
 *
 * Provides a terrible little xorshift random number generator.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014-2018 K. Lange
 */
#include <stdint.h>
#include <kernel/vfs.h>
#include <kernel/string.h>

uint32_t rand(void) {
	static uint32_t x = 123456789;
	static uint32_t y = 362436069;
	static uint32_t z = 521288629;
	static uint32_t w = 88675123;

	uint32_t t;

	t = x ^ (x << 11);
	x = y; y = z; z = w;
	return w = w ^ (w >> 19) ^ t ^ (t >> 8);
}

static ssize_t read_random(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
	size_t s = 0;
	while (s < size) {
		buffer[s] = rand() % 0xFF;
		s++;
	}
	return size;
}

static fs_node_t * random_device_create(void) {
	fs_node_t * fnode = malloc(sizeof(fs_node_t));
	memset(fnode, 0x00, sizeof(fs_node_t));
	fnode->inode = 0;
	strcpy(fnode->name, "random");
	fnode->uid = 0;
	fnode->gid = 0;
	fnode->mask = 0444;
	fnode->length  = 1024;
	fnode->flags   = FS_CHARDEVICE;
	fnode->read    = read_random;
	return fnode;
}

void random_initialize(void) {
	vfs_mount("/dev/random", random_device_create());
	vfs_mount("/dev/urandom", random_device_create());
}

