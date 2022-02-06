/**
 * @file modules/pcspkr.c
 * @brief PC beeper device interface
 * @package x86_64
 *
 * Use with @ref apps/beep.c to play music.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014-2021 K. Lange
 */
#include <kernel/module.h>
#include <kernel/printf.h>
#include <kernel/process.h>
#include <kernel/time.h>
#include <kernel/vfs.h>

#include <kernel/arch/x86_64/ports.h>

static void note(int length, int freq) {

	uint8_t  t;

	if (length == 0) {
		t = inportb(0x61) & 0xFC;
		outportb(0x61, t);
		return;
	}

	uint32_t div = 11931800 / freq;

	outportb(0x43, 0xb6);
	outportb(0x42, (uint8_t)(div));
	outportb(0x42, (uint8_t)(div >> 8));

	t = inportb(0x61);
	outportb(0x61, t | 0x3);

	if (length > 0) {
		unsigned long s, ss;
		relative_time(length / 1000, (length % 1000) * 1000, &s, &ss);
		sleep_until((process_t*)this_core->current_process, s, ss);
		switch_task(0);

		t = inportb(0x61) & 0xFC;
		outportb(0x61, t);
	}

}

struct spkr {
	int length;
	int frequency;
};

static ssize_t write_spkr(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
	if (!size % (sizeof(struct spkr))) {
		return 0;
	}

	struct spkr * s = (struct spkr *)buffer;
	while ((uintptr_t)s < (uintptr_t)buffer + size) {
		note(s->length, s->frequency);
		s++;
	}

	return (uintptr_t)s - (uintptr_t)buffer;
}

static fs_node_t * spkr_device_create(void) {
	fs_node_t * fnode = malloc(sizeof(fs_node_t));
	memset(fnode, 0x00, sizeof(fs_node_t));
	snprintf(fnode->name, 5, "spkr");
	fnode->mask    = 0660; /* TODO need a speaker group */
	fnode->gid     = 1;
	fnode->flags   = FS_CHARDEVICE;
	fnode->write   = write_spkr;
	return fnode;
}

static int init(int argc, char * argv[]) {
	fs_node_t * node = spkr_device_create();
	vfs_mount("/dev/spkr", node);
	return 0;
}

static int fini(void) {
	return 0;
}

struct Module metadata = {
	.name = "pcspkr",
	.init = init,
	.fini = fini,
};

