#include "kbd.h"
#include "util.h"

#define norm 0x01
#define spec 0x02
#define func 0x03

static int key_state = 0;
static int key_ctrl_state = 0;
static int key_shift_state = 0;

#define KEY_UP_MASK   0x80
#define KEY_CODE_MASK 0x7F
#define KEY_CTRL_MASK 0x40

static char key_method[] = {
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

static char kbd_us[128] = {
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

static char kbd_us_l2[128] = {
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

int read_scancode(void) {
	while (!(inportb(0x64) & 1));
	return inportb(0x60);
}

int read_key(void) {
	while (1) {
		int _key_out = -1;
		int c = read_scancode();
		if (!key_state) {
			if (c == 0xE0) {
				key_state = 1;
				continue;
			}
			int down = 1;
			if (c & KEY_UP_MASK) {
				c ^= KEY_UP_MASK;
				down = 0;
			}
			switch (key_method[c]) {
				case norm:
					{
						if (key_ctrl_state) {
							int s = kbd_us[c];
							if (s >= 'a' && s <= 'z') s -= 'a' - 'A';
							if (s == '-') s = '_';
							if (s == '`') s = '@';
							int out = (int)(s - KEY_CTRL_MASK);
							if (out < 0 || out > 0x1F) {
								_key_out = kbd_us[c];
							} else {
								_key_out = out;
							}
						} else {
							_key_out = key_shift_state ? kbd_us_l2[c] : kbd_us[c];
						}
					}
					break;
				case spec:
					switch (c) {
						case 0x01:
							_key_out = '\033';
							break;
						case 0x1D:
							key_ctrl_state = down;
							break;
						case 0x2A:
							key_shift_state = down;
							break;
						case 0x36:
							key_shift_state = down;
							break;
						default:
							break;
					}
					break;
				default:
					break;
			}
			if (_key_out != -1 && down) return _key_out;
		} else if (key_state == 1) {
			int down = 0;
			if (c & KEY_UP_MASK) {
				c ^= KEY_UP_MASK;
				down = 1;
			}
			key_state = 0;
		}
	}
}

