#include <efi.h>
#include <efilib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

#include "text.h"

int txt_debug = 0;
int x = 0;
int y = 0;
int attr = 0x07;
int scroll_disabled = 0;

static void placech(unsigned char c, int x, int y, int attr) {
	unsigned short ch;
	switch (c) {
		case '\030':
			ch = L'↑';
			break;
		case '\031':
			ch = L'↓';
			break;
		case '\032':
			ch = L'←';
			break;
		case '\033':
			ch = L'→';
			break;
		default:
			ch = c;
			break;
	}
	uint16_t string[] = {ch, 0};
	uefi_call_wrapper(ST->ConOut->SetAttribute, 2, ST->ConOut, attr);
	uefi_call_wrapper(ST->ConOut->SetCursorPosition, 3, ST->ConOut, x, y);
	uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, string);
}

void print_(char * str) {
	while (*str) {
		if (*str == '\n') {
			for (; x < 80; ++x) {
				placech(' ', x, y, attr);
			}
			x = 0;
			y += 1;
			if (y == 25) {
				y = 0;
			}
		} else {
			placech(*str, x, y, attr);
			x++;
			if (x == 80) {
				x = 0;
				y += 1;
				if (y == 25) {
					y = 0;
				}
			}
		}
		str++;
	}
}

void move_cursor(int _x, int _y) {
	x = _x;
	y = _y;
}

void move_cursor_rel(int _x, int _y) {
	x += _x; if (x < 0) x = 0;
	y += _y; if (y < 0) y = 0;
}

void set_attr(int _attr) {
	attr = _attr;
}

void print_banner(char * str) {
	if (!str) {
		for (int i = 0; i < 80; ++i) {
			placech(' ', i, y, attr);
		}
		y++;
		return;
	}
	int len = 0;
	char *c = str;
	while (*c) {
		len++;
		c++;
	}
	int off = (80 - len) / 2;

	for (int i = 0; i < 80; ++i) {
		placech(' ', i, y, attr);
	}
	for (int i = 0; i < len; ++i) {
		placech(str[i], i + off, y, attr);
	}

	y++;
}

void print_hex_(unsigned int value) {
	char out[9] = {0};
	for (int i = 7; i > -1; i--) {
		out[i] = "0123456789abcdef"[(value >> (4 * (7 - i))) & 0xF];
	}
	print_(out);
}

void clear_() {
	x = 0;
	y = 0;
	for (int y = 0; y < 24; ++y) {
		for (int x = 0; x < 80; ++x) {
			placech(' ', x, y, attr);
		}
	}
}

void print_int_(unsigned int value) {
	unsigned int n_width = 1;
	unsigned int i = 9;
	while (value > i && i < UINT32_MAX) {
		n_width += 1;
		i *= 10;
		i += 9;
	}

	char buf[n_width+1];
	buf[n_width] = 0;
	i = n_width;
	while (i > 0) {
		unsigned int n = value / 10;
		int r = value % 10;
		buf[i - 1] = r + '0';
		i--;
		value = n;
	}
	print_(buf);
}
