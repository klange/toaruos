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
 * TODO: Better handling of function keys
 */

#include <system.h>
#include <logging.h>
#include <fs.h>
#include <pipe.h>
#include <process.h>

#define KEY_UP_MASK   0x80
#define KEY_CODE_MASK 0x7F
#define KEY_CTRL_MASK 0x40

#define KEY_DEVICE    0x60
#define KEY_PENDING   0x64

#define KEYBOARD_NOTICES 0
#define KEYBOARD_IRQ 1

/* A bit-map to store the keyboard states */
struct keyboard_states {
	uint32_t shift : 1;
	uint32_t alt   : 1;
	uint32_t ctrl  : 1;
} keyboard_state;

typedef void (*keyboard_handler_t)(int scancode);
fs_node_t * keyboard_pipe;

extern uint8_t mouse_cycle;

void
keyboard_handler(
		struct regs *r
		) {
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
void
keyboard_install() {
	blog("Initializing PS/2 keyboard driver...");
	LOG(INFO, "Initializing PS/2 keyboard driver");

	/* Create a device pipe */
	keyboard_pipe = make_pipe(128);
	current_process->fds->entries[0] = keyboard_pipe;

	/* Install the interrupt handler */
	irq_install_handler(KEYBOARD_IRQ, keyboard_handler);

	bfinish(0);
}

/*
 * Wait on the keyboard.
 */
void
keyboard_wait() {
	while(inportb(KEY_PENDING) & 2);
}

/*
 * Add a character to the device buffer.
 */
void
putch(
		unsigned char c
	 ) {
	uint8_t buf[2];
	buf[0] = c;
	buf[1] = '\0';
	write_fs(keyboard_pipe, 0, 1, buf);
}

