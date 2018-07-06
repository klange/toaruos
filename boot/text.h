#pragma once

static int txt_debug = 0;

unsigned short * textmemptr = (unsigned short *)0xB8000;
static void placech(unsigned char c, int x, int y, int attr) {
#ifdef EFI_PLATFORM
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
#else
	unsigned short *where;
	unsigned att = attr << 8;
	where = textmemptr + (y * 80 + x);
	*where = c | att;
#endif
}

static int x = 0;
static int y = 0;
static int attr = 0x07;
static void print_(char * str) {
	while (*str) {
		if (*str == '\n') {
			for (; x < 80; ++x) {
				placech(' ', x, y, attr);
			}
			x = 0;
			y += 1;
			if (y == 24) {
				y = 0;
			}
		} else {
			placech(*str, x, y, attr);
			x++;
			if (x == 80) {
				x = 0;
				y += 1;
				if (y == 24) {
					y = 0;
				}
			}
		}
		str++;
	}
}

static void move_cursor(int _x, int _y) {
	x = _x;
	y = _y;
}

static void set_attr(int _attr) {
	attr = _attr;
}

static void print_banner(char * str) {
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

static void print_hex_(unsigned int value) {
	char out[9] = {0};
	for (int i = 7; i > -1; i--) {
		out[i] = "0123456789abcdef"[(value >> (4 * (7 - i))) & 0xF];
	}
	print_(out);
}

static void clear_() {
	x = 0;
	y = 0;
	for (int y = 0; y < 24; ++y) {
		for (int x = 0; x < 80; ++x) {
			placech(' ', x, y, 0x00);
		}
	}
}

#define print(s) do {if (txt_debug) {print_(s);}} while(0)
#define clear() do {if (txt_debug) {clear_();}} while(0)
#define print_hex(d) do {if (txt_debug) {print_hex_(d);}} while(0)

