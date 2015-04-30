/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014 Kevin Lange
 */
#include <module.h>
#include <printf.h>
#include <mod/shell.h>

static void note(int length, int freq) {

	uint32_t div = 11931800 / freq;
	uint8_t  t;

	outportb(0x43, 0xb6);
	outportb(0x42, (uint8_t)(div));
	outportb(0x42, (uint8_t)(div >> 8));

	t = inportb(0x61);
	outportb(0x61, t | 0x3);

	unsigned long s, ss;
	relative_time(0, length * 10, &s, &ss);
	sleep_until((process_t *)current_process, s, ss);
	switch_task(0);

	t = inportb(0x61) & 0xFC;
	outportb(0x61, t);

}

struct spkr {
	int length;
	int frequency;
};

static uint32_t write_spkr(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
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
	sprintf(fnode->name, "spkr");
	fnode->flags   = FS_CHARDEVICE;
	fnode->write   = write_spkr;
	return fnode;
}

static int init(void) {
	fs_node_t * node = spkr_device_create();
	vfs_mount("/dev/spkr", node);
	return 0;
}

static int fini(void) {
	return 0;
}

MODULE_DEF(pcspkr, init, fini);
MODULE_DEPENDS(debugshell);
