/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Low-level keyboard interrupt driver.
 *
 * Creates a device file (keyboard_pipe) that can be read
 * to retreive keyboard events.
 *
 * Part of the ToAruOS Kernel
 * Copyright 2011-2012 Kevin Lange
 *
 * TODO: Move this to a server
 */

#include <system.h>
#include <logging.h>
#include <fs.h>
#include <pipe.h>
#include <process.h>

#define KEY_DEVICE    0x60
#define KEY_PENDING   0x64

#define KEYBOARD_NOTICES 0
#define KEYBOARD_IRQ 1

fs_node_t * keyboard_pipe;

void keyboard_handler(struct regs *r) {
	unsigned char scancode;
	keyboard_wait();
	scancode = inportb(KEY_DEVICE);
	irq_ack(KEYBOARD_IRQ);

	putch(scancode);
}

/*
 * Install the keyboard driver and initialize the
 * pipe device for userspace.
 */
void keyboard_install(void) {
	debug_print(NOTICE, "Initializing PS/2 keyboard driver");

	/* Create a device pipe */
	keyboard_pipe = make_pipe(128);
	current_process->fds->entries[0] = keyboard_pipe;

	/* Install the interrupt handler */
	irq_install_handler(KEYBOARD_IRQ, keyboard_handler);
}

void keyboard_reset_ps2(void) {
	uint8_t tmp = inportb(0x61);
	outportb(0x61, tmp | 0x80);
	outportb(0x61, tmp & 0x7F);
	inportb(KEY_DEVICE);
}

/*
 * Wait on the keyboard.
 */
void keyboard_wait(void) {
	while(inportb(KEY_PENDING) & 2);
}

/*
 * Add a character to the device buffer.
 */
void putch(unsigned char c) {
	uint8_t buf[2];
	buf[0] = c;
	buf[1] = '\0';
	write_fs(keyboard_pipe, 0, 1, buf);
}

