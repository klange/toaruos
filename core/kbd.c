#include <system.h>

struct keyboard_states {
	uint32_t shift : 1;
	uint32_t alt   : 1;
	uint32_t ctrl  : 1;
} keyboard_state;

typedef void (*keyboard_handler_t)(int scancode);

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



void norm(int scancode) {
	if (scancode & 0x80) {
		return;
	}
	if (!kbd_us[scancode]) {
		return;
	}
	if (keyboard_state.shift) {
		putch(kbd_us_l2[scancode]);
	} else if (keyboard_state.ctrl) {
		putch('^');
		putch(kbd_us_l2[scancode]);
	} else {
		putch(kbd_us[scancode]);
	}
}

void shft(int scancode) {
	keyboard_state.shift ^= 1;
}

void altk(int scancode) {
	keyboard_state.alt ^= 1;
}

void ctlk(int scancode) {
	keyboard_state.ctrl ^= 1;
	
}

void func(int scancode) {
	
}

keyboard_handler_t key_method[] = {
	/* 00 */ NULL, NULL, norm, norm, norm, norm, norm, norm,
	/* 08 */ norm, norm, norm, norm, norm, norm, norm, norm,
	/* 10 */ norm, norm, norm, norm, norm, norm, norm, norm,
	/* 18 */ norm, norm, norm, norm, norm, ctlk, norm, norm,
	/* 20 */ norm, norm, norm, norm, norm, norm, norm, norm,
	/* 28 */ norm, norm, shft, norm, norm, norm, norm, norm,
	/* 30 */ norm, norm, norm, norm, norm, norm, shft, norm,
	/* 38 */ altk, norm, NULL, func, func, func, func, func,
	/* 40 */ func, func, func, func, func, NULL, NULL, NULL,
	/* 48 */ NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	/* 50 */ NULL, NULL, NULL, NULL, NULL, NULL, NULL, func,
	/* 58 */ func, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	/* 60 */ NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	/* 68 */ NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	/* 70 */ NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	/* 78 */ NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
};


void
keyboard_handler(
		struct regs *r
		) {
	unsigned char scancode;
	scancode = inportb(0x60);
	keyboard_handler_t handler;
	handler = key_method[(int)scancode & 0x7f];
	if (handler) {
		handler(scancode);
	}
}

void
keyboard_install() {
	/* IRQ installer */
	irq_install_handler(1, keyboard_handler);
	keyboard_buffer_handler = NULL;
}

void
keyboard_wait() {
	while(inportb(0x64) & 2);
}

/*
 * putch
 */
void
putch(
		unsigned char c
	 ) {
	if (keyboard_buffer_handler) {
		keyboard_buffer_handler(c);
	} else {
		writech(c);
	}
}

