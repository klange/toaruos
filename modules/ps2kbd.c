/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2011-2015 Kevin Lange
 *
 * Low-level keyboard interrupt driver.
 *
 * Creates a device file (keyboard_pipe) that can be read
 * to retreive keyboard events.
 *
 */

#include <system.h>
#include <logging.h>
#include <fs.h>
#include <pipe.h>
#include <process.h>

#include <module.h>

#define KEY_DEVICE  0x60
#define KEY_PENDING 0x64
#define KEY_IRQ     1

static fs_node_t * keyboard_pipe;

/*
 * Wait on the keyboard.
 */
static void keyboard_wait(void) {
	while(inportb(KEY_PENDING) & 2);
}

/*
 * Keyboard interrupt callback
 */
static int keyboard_handler(struct regs *r) {
	unsigned char scancode;
	keyboard_wait();
	scancode = inportb(KEY_DEVICE);
	irq_ack(KEY_IRQ);

	write_fs(keyboard_pipe, 0, 1, (uint8_t []){scancode});
	return 1;
}

/*
 * Install the keyboard driver and initialize the
 * pipe device for userspace.
 */
static int keyboard_install(void) {
	debug_print(NOTICE, "Initializing PS/2 keyboard driver");

	/* Create a device pipe */
	keyboard_pipe = make_pipe(128);
	current_process->fds->entries[0] = keyboard_pipe;

	keyboard_pipe->flags = FS_CHARDEVICE;

	vfs_mount("/dev/kbd", keyboard_pipe);

	/* Install the interrupt handler */
	irq_install_handler(KEY_IRQ, keyboard_handler);

	return 0;
}

static void keyboard_reset_ps2(void) {
	uint8_t tmp = inportb(0x61);
	outportb(0x61, tmp | 0x80);
	outportb(0x61, tmp & 0x7F);
	inportb(KEY_DEVICE);
}

static int keyboard_uninstall(void) {
	/* TODO */
	return 0;
}

MODULE_DEF(ps2kbd, keyboard_install, keyboard_uninstall);
