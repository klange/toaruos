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

char kbd_us[128] = {
	0, 27,
	'1','2','3','4','5','6','7','8','9','0',
	'-','=','\b',
	'\t', /* tab */
	'q','w','e','r','t','y','u','i','o','p','[',']','\n',
	0, /* control */
	'a','s','d','f','g','h','j','k','l',';','\'', '`',
	0, /* left shift */
	'\\','z','x','c','v','b','n','m',',','.','/',
	0, /* right shift */
	'*',
	0, /* alt */
	' ', /* space */
	0, /* caps lock */
	0, /* F1 [59] */
	0, 0, 0, 0, 0, 0, 0, 0,
	0, /* ... F10 */
	0, /* 69 num lock */
	0, /* scroll lock */
	0, /* home */
	0, /* up */
	0, /* page up */
	'-',
	0, /* left arrow */
	0,
	0, /* right arrow */
	'+',
	0, /* 79 end */
	0, /* down */
	0, /* page down */
	0, /* insert */
	0, /* delete */
	0, 0, 0,
	0, /* F11 */
	0, /* F12 */
	0, /* everything else */
};

char kbd_us_l2[128] = {
	0, 27,
	'!','@','#','$','%','^','&','*','(',')',
	'_','+','\b',
	'\t', /* tab */
	'Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
	0, /* control */
	'A','S','D','F','G','H','J','K','L',':','"', '~',
	0, /* left shift */
	'|','Z','X','C','V','B','N','M','<','>','?',
	0, /* right shift */
	'*',
	0, /* alt */
	' ', /* space */
	0, /* caps lock */
	0, /* F1 [59] */
	0, 0, 0, 0, 0, 0, 0, 0,
	0, /* ... F10 */
	0, /* 69 num lock */
	0, /* scroll lock */
	0, /* home */
	0, /* up */
	0, /* page up */
	'-',
	0, /* left arrow */
	0,
	0, /* right arrow */
	'+',
	0, /* 79 end */
	0, /* down */
	0, /* page down */
	0, /* insert */
	0, /* delete */
	0, 0, 0,
	0, /* F11 */
	0, /* F12 */
	0, /* everything else */
};


/*
 * "Normal" key handler
 * Used for visible text keys and handles
 * application of shift/alt/control
 *
 * Shells out to putch() to do most of the work.
 */
void norm(int scancode) {
	if (scancode & KEY_UP_MASK) {
		return;
	}
	if (!kbd_us[scancode]) {
		return;
	}
	if (keyboard_state.shift) {
		putch(kbd_us_l2[scancode]);
	} else if (keyboard_state.ctrl) {
		int out = (int)(kbd_us_l2[scancode] - KEY_CTRL_MASK);
		if (out < 0 || out > 0x1F) {
			putch(kbd_us[scancode]);
		} else {
			putch((char)out);
		}
	} else {
		putch(kbd_us[scancode]);
	}
}

/*
 * Toggle Shift
 */
void shft(int scancode) {
	keyboard_state.shift = !((scancode & KEY_UP_MASK) == KEY_UP_MASK);
}

/*
 * Toggle Alt
 */
void altk(int scancode) {
	keyboard_state.alt = !((scancode & KEY_UP_MASK) == KEY_UP_MASK);
}

/*
 * Toggle Control
 */
void ctlk(int scancode) {
	keyboard_state.ctrl = !((scancode & KEY_UP_MASK) == KEY_UP_MASK);
}

/*
 * Function keys
 */
void func(int scancode) {
	if (scancode & KEY_UP_MASK) {
		return;
	}
#if KEYBOARD_NOTICES
	kprintf("[NOTICE] Function key %d pressed\n", scancode);
#endif
}

/*
 * "Special" keys
 * Extra keys on your keyboard will produce these
 * sorts of key presses.
 */
void spec(int scancode) {
	if (scancode & KEY_UP_MASK) {
		return;
	}
	switch (scancode) {
		case 75:
			putch('\033');
			putch('[');
			putch('D');
			break;
		case 72:
			putch('\033');
			putch('[');
			putch('A');
			break;
		case 77:
			putch('\033');
			putch('[');
			putch('C');
			break;
		case 80:
			putch('\033');
			putch('[');
			putch('B');
			break;
		case 1:
			putch('\033');
		default:
#if KEYBOARD_NOTICES
			kprintf("[NOTICE] Special key %d pressed\n", scancode);
#endif
			break;
	}
}

keyboard_handler_t key_method[] = {
	/* 00 */ NULL, spec, norm, norm, norm, norm, norm, norm,
	/* 08 */ norm, norm, norm, norm, norm, norm, norm, norm,
	/* 10 */ norm, norm, norm, norm, norm, norm, norm, norm,
	/* 18 */ norm, norm, norm, norm, norm, ctlk, norm, norm,
	/* 20 */ norm, norm, norm, norm, norm, norm, norm, norm,
	/* 28 */ norm, norm, shft, norm, norm, norm, norm, norm,
	/* 30 */ norm, norm, norm, norm, norm, norm, shft, norm,
	/* 38 */ altk, norm, spec, func, func, func, func, func,
	/* 40 */ func, func, func, func, func, spec, spec, spec,
	/* 48 */ spec, spec, spec, spec, spec, spec, spec, spec,
	/* 50 */ spec, spec, spec, spec, spec, spec, spec, func,
	/* 58 */ func, spec, spec, spec, spec, spec, spec, spec,
	/* 60 */ spec, spec, spec, spec, spec, spec, spec, spec,
	/* 68 */ spec, spec, spec, spec, spec, spec, spec, spec,
	/* 70 */ spec, spec, spec, spec, spec, spec, spec, spec,
	/* 78 */ spec, spec, spec, spec, spec, spec, spec, spec,
};


extern uint8_t mouse_cycle;

void
keyboard_handler(
		struct regs *r
		) {
	unsigned char scancode;
	keyboard_wait();
	scancode = inportb(KEY_DEVICE);
	irq_ack(KEYBOARD_IRQ);
	if (keyboard_direct_handler) {
		keyboard_direct_handler(scancode);
		return;
	}
	keyboard_handler_t handler;

	handler = key_method[(int)scancode & KEY_CODE_MASK];
	if (handler) {
		handler(scancode);
	}

}

/*
 * Install the keyboard driver and initialize the
 * pipe device for userspace.
 */
void
keyboard_install() {
	blog("Initializing PS/2 keyboard driver...");
	LOG(INFO, "Initializing PS/2 keyboard driver");

	/* Clear the buffer handlers */
	keyboard_buffer_handler = NULL;
	keyboard_direct_handler = NULL;

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
	if (keyboard_buffer_handler) {
		keyboard_buffer_handler(c);
	} else {
		uint8_t buf[2];
		buf[0] = c;
		buf[1] = '\0';
		write_fs(keyboard_pipe, 0, 1, buf);
	}
}

/*
 * Externally Set Keyboard States
 */
void
set_kbd(
		int shift,
		int alt,
		int ctrl
	   ) {
	keyboard_state.shift = shift;
	keyboard_state.alt   = alt;
	keyboard_state.ctrl  = ctrl;
}

