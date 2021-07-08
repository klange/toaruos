#include "kbd.h"
#include "util.h"

int read_cmos_seconds(void) {
	outportb(0x70,0);
	return inportb(0x71);
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
