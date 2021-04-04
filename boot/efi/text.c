#include <efi.h>
#include <efilib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

#include "text.h"

int txt_debug = 0;
int x = 0;
int y = 0;
int scroll_disabled = 0;

static unsigned short bad_ucs2(int c) {
	switch (c) {
		case '\030':
			return L'↑';
		case '\031':
			return L'↓';
		case '\032':
			return L'←';
		case '\033':
			return L'→';
		default:
			return c;
	}
}

void print_(char * str) {
	while (*str) {
		if (*str == '\n') print_("\r");
		uint16_t string[] = {bad_ucs2(*str), 0};
		uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, string);
		x = ST->ConOut->Mode->CursorColumn;
		y = ST->ConOut->Mode->CursorRow;
		str++;
	}
}

void print_wchar(int wch) {
	uint16_t string[] = {wch, 0};
	uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, string);
}

void move_cursor(int _x, int _y) {
	uefi_call_wrapper(ST->ConOut->SetCursorPosition, 3, ST->ConOut,
		_x, _y);
	x = _x;
	y = _y;
}

void move_cursor_rel(int _x, int _y) {
	x += _x; if (x < 0) x = 0;
	y += _y; if (y < 0) y = 0;
	move_cursor(x,y);
}

void move_cursor_x(int _x) {
	uefi_call_wrapper(ST->ConOut->SetCursorPosition, 3, ST->ConOut,
		_x, ST->ConOut->Mode->CursorRow);
}

void set_attr(int attr) {
	uefi_call_wrapper(ST->ConOut->SetAttribute, 2, ST->ConOut, attr);
}

void print_banner(char * str) {
	int pad = (79 - strlen(str));
	int left_pad = pad / 2;
	int right_pad = pad - left_pad;

	for (int i = 0; i < left_pad; ++i) {
		print_(" ");
	}
	print_(str);
	for (int i = 0; i < right_pad; ++i) {
		print_(" ");
	}
	print_("\n");
}

void print_hex_(unsigned int value) {
	char out[9] = {0};
	for (int i = 7; i > -1; i--) {
		out[i] = "0123456789abcdef"[(value >> (4 * (7 - i))) & 0xF];
	}
	print_(out);
}

void clear_() {
	move_cursor(0,0);
	uefi_call_wrapper(ST->ConOut->ClearScreen, 1, ST->ConOut);
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
