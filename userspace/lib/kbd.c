/*
 * General-purpose keyboard conversion library.
 *
 * This provides similar functionality to xkb:
 *   - It provides mappings for keyboards from locales
 *   - It translates incoming key presses to key names
 *   - It translates incoming keys to escape sequences
 */

#include <stdio.h>
#include "kbd.h"

int kbd_state = 0;

#define DEBUG_SCANCODES 0

#define KEY_UP_MASK   0x80
#define KEY_CODE_MASK 0x7F
#define KEY_CTRL_MASK 0x40

#define norm 0x01
#define spec 0x02
#define func 0x03

#define SET_UNSET(a,b,c) (a) = (c) ? ((a) | (b)) : ((a) & ~(b))

char key_method[] = {
	/* 00 */ 0,    spec, norm, norm, norm, norm, norm, norm,
	/* 08 */ norm, norm, norm, norm, norm, norm, norm, norm,
	/* 10 */ norm, norm, norm, norm, norm, norm, norm, norm,
	/* 18 */ norm, norm, norm, norm, norm, spec, norm, norm,
	/* 20 */ norm, norm, norm, norm, norm, norm, norm, norm,
	/* 28 */ norm, norm, spec, norm, norm, norm, norm, norm,
	/* 30 */ norm, norm, norm, norm, norm, norm, spec, norm,
	/* 38 */ spec, norm, spec, func, func, func, func, func,
	/* 40 */ func, func, func, func, func, spec, spec, spec,
	/* 48 */ spec, spec, spec, spec, spec, spec, spec, spec,
	/* 50 */ spec, spec, spec, spec, spec, spec, spec, func,
	/* 58 */ func, spec, spec, spec, spec, spec, spec, spec,
	/* 60 */ spec, spec, spec, spec, spec, spec, spec, spec,
	/* 68 */ spec, spec, spec, spec, spec, spec, spec, spec,
	/* 70 */ spec, spec, spec, spec, spec, spec, spec, spec,
	/* 78 */ spec, spec, spec, spec, spec, spec, spec, spec,
};

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
 * Converts from incoming terminal keys to kbd_keys
 */
kbd_key_t kbd_key(unsigned char c) {
	switch (kbd_state) {
		case KBD_NORMAL:
			switch (c) {
				case 0x1b:
					kbd_state = KBD_ESC_A;
					return KEY_NONE;
				default:
					return c;
			}
		case KBD_ESC_A:
			switch (c) {
				case 0x5b:
					kbd_state = KBD_ESC_B;
					return KEY_NONE;
				default:
					kbd_state = KBD_NORMAL;
					return c;
			}
		case KBD_ESC_B:
			switch (c) {
				case 0x41:
					kbd_state = KBD_NORMAL;
					return KEY_ARROW_UP;
				case 0x42:
					kbd_state = KBD_NORMAL;
					return KEY_ARROW_DOWN;
				case 0x43:
					kbd_state = KBD_NORMAL;
					return KEY_ARROW_RIGHT;
				case 0x44:
					kbd_state = KBD_NORMAL;
					return KEY_ARROW_LEFT;
				default:
				kbd_state = KBD_NORMAL;
				return c;
			}
		default:
			return KEY_BAD_STATE;
	}

	return KEY_BAD_STATE;
}

int kbd_s_state = 0;

int k_ctrl  = 0;
int k_shift = 0;
int k_alt   = 0;
int k_super = 0;

int kl_ctrl  = 0;
int kl_shift = 0;
int kl_alt   = 0;
int kl_super = 0;

int kr_ctrl  = 0;
int kr_shift = 0;
int kr_alt   = 0;
int kr_super = 0;

int kbd_scancode(unsigned char c, key_event_t * event) {
	/* Convert scancodes to a series of keys */

	event->keycode   = 0;
	event->action    = 0;
	event->modifiers = 0;
	event->key       = 0;

#if DEBUG_SCANCODES
	fprintf(stderr, "[%d] %d\n", kbd_s_state, (int)c);
#endif

	event->modifiers |= kl_ctrl  ? KEY_MOD_LEFT_CTRL   : 0;
	event->modifiers |= kl_shift ? KEY_MOD_LEFT_SHIFT  : 0;
	event->modifiers |= kl_alt   ? KEY_MOD_LEFT_ALT    : 0;
	event->modifiers |= kl_super ? KEY_MOD_LEFT_SUPER  : 0;

	event->modifiers |= kr_ctrl  ? KEY_MOD_RIGHT_CTRL  : 0;
	event->modifiers |= kr_shift ? KEY_MOD_RIGHT_SHIFT : 0;
	event->modifiers |= kr_alt   ? KEY_MOD_RIGHT_ALT   : 0;
	event->modifiers |= kr_super ? KEY_MOD_RIGHT_SUPER : 0;

	if (!kbd_s_state) {
		if (c == 0xE0) {
			kbd_s_state = 1;
			/* Literally nothing */
			return 0;
		}

		if (c & KEY_UP_MASK) {
			c ^= KEY_UP_MASK;
			event->action = KEY_ACTION_UP;
		} else {
			event->action = KEY_ACTION_DOWN;
		}
		int down = (event->action == KEY_ACTION_DOWN);

		switch (key_method[c]) {
			case norm:
				{
					event->keycode = kbd_us[c];
					if (k_ctrl) {
						int out = (int)(kbd_us_l2[c] - KEY_CTRL_MASK);
						if (out < 0 || out > 0x1F) {
							event->key = kbd_us[c];
						} else {
							event->key = out;
						}
					} else {
						event->key = k_shift ? kbd_us_l2[c] : kbd_us[c];
					}
				}
				break;
			case spec:
				switch (c) {
					case 0x01:
						event->key     = '\033';
						event->keycode = KEY_ESCAPE;
						break;
					case 0x1D:
						k_ctrl   = down;
						kl_ctrl  = down;
						SET_UNSET(event->modifiers, KEY_MOD_LEFT_CTRL, down);
						break;
					case 0x2A:
						k_shift  = down;
						kl_shift = down;
						SET_UNSET(event->modifiers, KEY_MOD_LEFT_SHIFT, down);
						break;
					case 0x36:
						k_shift  = down;
						kr_shift = down;
						SET_UNSET(event->modifiers, KEY_MOD_RIGHT_SHIFT, down);
						break;
					case 0x38:
						k_alt    = down;
						kl_alt   = down;
						SET_UNSET(event->modifiers, KEY_MOD_LEFT_ALT, down);
						break;
					default:
						break;
				}
				break;
			case func:
				switch (c) {
					case KEY_SCANCODE_F1:
						event->keycode = KEY_F1;
						break;
					case KEY_SCANCODE_F2:
						event->keycode = KEY_F2;
						break;
					case KEY_SCANCODE_F3:
						event->keycode = KEY_F3;
						break;
					case KEY_SCANCODE_F4:
						event->keycode = KEY_F4;
						break;
					case KEY_SCANCODE_F5:
						event->keycode = KEY_F5;
						break;
					case KEY_SCANCODE_F6:
						event->keycode = KEY_F6;
						break;
					case KEY_SCANCODE_F7:
						event->keycode = KEY_F7;
						break;
					case KEY_SCANCODE_F8:
						event->keycode = KEY_F8;
						break;
					case KEY_SCANCODE_F9:
						event->keycode = KEY_F9;
						break;
					case KEY_SCANCODE_F10:
						event->keycode = KEY_F10;
						break;
					case KEY_SCANCODE_F11:
						event->keycode = KEY_F11;
						break;
					case KEY_SCANCODE_F12:
						event->keycode = KEY_F12;
						break;
				}
				break;
			default:
				break;
		}

		if (event->key) {
			return down;
		}

		return 0;
	} else if (kbd_s_state == 1) {

		if (c & KEY_UP_MASK) {
			c ^= KEY_UP_MASK;
			event->action = KEY_ACTION_UP;
		} else {
			event->action = KEY_ACTION_DOWN;
		}

		int down = (event->action == KEY_ACTION_DOWN);
		switch (c) {
			case 0x5B:
				k_super  = down;
				kl_super = down;
				SET_UNSET(event->modifiers, KEY_MOD_LEFT_SUPER, down);
				break;
			case 0x5C:
				k_super  = down;
				kr_super = down;
				SET_UNSET(event->modifiers, KEY_MOD_RIGHT_SUPER, down);
				break;
			case 0x1D:
				kr_ctrl  = down;
				k_ctrl   = down;
				SET_UNSET(event->modifiers, KEY_MOD_RIGHT_CTRL, down);
				break;
			case 0x38:
				kr_alt   = down;
				k_alt    = down;
				SET_UNSET(event->modifiers, KEY_MOD_RIGHT_ALT, down);
				break;
			case 0x48:
				event->keycode = KEY_ARROW_UP;
				break;
			case 0x4D:
				event->keycode = KEY_ARROW_RIGHT;
				break;
			case 0x50:
				event->keycode = KEY_ARROW_DOWN;
				break;
			case 0x4B:
				event->keycode = KEY_ARROW_LEFT;
				break;
			case 0x49:
				event->keycode = KEY_PAGE_UP;
				break;
			case 0x51:
				event->keycode = KEY_PAGE_DOWN;
				break;
			default:
				break;
		}

		kbd_s_state = 0;
		return 0;
	}
	return 0;
}
