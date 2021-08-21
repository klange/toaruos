#include "kbd.h"
#include "util.h"

int read_cmos_seconds(void) {
	outportb(0x70,0);
	return inportb(0x71);
}

static char kbd_us[128] = {
	0, 27, '1','2','3','4','5','6','7','8','9','0',
	'-','=','\b', '\t', 'q','w','e','r','t','y','u','i','o','p','[',']','\n',
	0, 'a','s','d','f','g','h','j','k','l',';','\'', '`',
	0, '\\','z','x','c','v','b','n','m',',','.','/',
	0, '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	'-', 0, 0, 0, '+', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

static char kbd_us_l2[128] = {
	0, 27, '!','@','#','$','%','^','&','*','(',')',
	'_','+','\b', '\t', 'Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
	0, 'A','S','D','F','G','H','J','K','L',':','"', '~',
	0, '|','Z','X','C','V','B','N','M','<','>','?',
	0, '*', 0, '\037', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	'-', 0, 0, 0, '+', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

int read_key(char * c) {
	static int shift_state = 0;

	int sc = read_scancode(0);

	switch (sc) {
		/* Shift down */
		case 0x2A:
		case 0x36:
			shift_state = 1;
			return 1;
		/* Shift up */
		case 0xAA:
		case 0xB6:
			shift_state = 0;
			return 1;

		/* Keft left and right */
		case 0x4B: return shift_state ? 4 : 2;
		case 0x4D: return shift_state ? 5 : 3;
	}

	if (!(sc & 0x80)) {
		*c = shift_state ? kbd_us_l2[sc] : kbd_us[sc];
		return *c == 0;
	}

	return 1;
}

int read_scancode(int timeout) {
	if (timeout) {
		int start_s = read_cmos_seconds();
		while (!(inportb(0x64) & 1)) {
			int now_s = read_cmos_seconds();
			if (now_s != start_s) return -1;
		}
	} else {
		while (!(inportb(0x64) & 1));
	}
	int out;
	while (inportb(0x64) & 1) {
		out = inportb(0x60);
	}
	return out;
}
