/*
 * General-purpose keyboard conversion library.
 *
 * This provides similar functionality to xkb:
 *   - It provides mappings for keyboards from locales
 *   - It translates incoming key presses to key names
 *   - It translates incoming keys to escape sequences
 */
#include "kbd.h"

int kbd_state = 0;

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

