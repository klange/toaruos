#include "text.h"
#include "util.h"
int txt_debug = 0;

#ifdef EFI_PLATFORM
#include <efi.h>
extern EFI_SYSTEM_TABLE *ST;

#include "../apps/terminal-font.h"

#define char_height LARGE_FONT_CELL_HEIGHT
#define char_width  LARGE_FONT_CELL_WIDTH

EFI_GRAPHICS_OUTPUT_PROTOCOL * GOP;

static int offset_x = 0;
static int offset_y = 0;

static EFI_GUID efi_graphics_output_protocol_guid =
  {0x9042a9de,0x23dc,0x4a38,  {0x96,0xfb,0x7a,0xde,0xd0,0x80,0x51,0x6a}};

int init_graphics() {
	UINTN count;
	EFI_HANDLE * handles;
	EFI_GRAPHICS_OUTPUT_PROTOCOL * gfx;
	EFI_STATUS status;

	status = uefi_call_wrapper(ST->BootServices->LocateHandleBuffer,
			5, ByProtocol, &efi_graphics_output_protocol_guid, NULL, &count, &handles);
	if (EFI_ERROR(status)) goto no_graphics;
	status = uefi_call_wrapper(ST->BootServices->HandleProtocol,
			3, handles[0], &efi_graphics_output_protocol_guid, (void **)&gfx);
	if (EFI_ERROR(status)) goto no_graphics;

	GOP = gfx;

	int total_width = GOP->Mode->Info->HorizontalResolution;
	int total_height = GOP->Mode->Info->VerticalResolution;

	offset_x = (total_width - 80 * char_width) / 2;
	offset_y = (total_height - 24 * char_height) / 2;

	return 0;

no_graphics:
	return 1;
}

static uint32_t term_colors[] = {
	0xFF000000,
	0xFFCC0000,
	0xFF4E9A06,
	0xFFC4A000,
	0xFF3465A4,
	0xFF75507B,
	0xFF06989A,
	0xFFD3D7CF,

	0xFF555753,
	0xFFEF2929,
	0xFF8AE234,
	0xFFFCE94F,
	0xFF729FCF,
	0xFFAD7FA8,
	0xFF34E2E2,
	0xFFEEEEEC,
};

char vga_to_ansi[] = {
	0, 4, 2, 6, 1, 5, 3, 7,
	8,12,10,14, 9,13,11,15
};

static void set_point(int x, int y, uint32_t color) {

	((uint32_t *)GOP->Mode->FrameBufferBase)[(x + offset_x) + (y + offset_y) * GOP->Mode->Info->PixelsPerScanLine] = color;
}

static void write_char(int x, int y, int val, int attr) {
	if (val > 128) {
		val = 4;
	}

	uint32_t fg_color = term_colors[vga_to_ansi[attr & 0xF]];
	uint32_t bg_color = term_colors[vga_to_ansi[(attr >> 4) & 0xF]];

	uint16_t * c = large_font[val];
	for (uint8_t i = 0; i < char_height; ++i) {
		for (uint8_t j = 0; j < char_width; ++j) {
			if (c[i] & (1 << (LARGE_FONT_MASK-j))) {
				set_point(x+j,y+i,fg_color);
			} else {
				set_point(x+j,y+i,bg_color);
			}
		}
	}
}

static void placech(unsigned char c, int x, int y, int attr) {
	write_char(x * char_width, y * char_height, c, attr);
}

void clear_() {
	x = 0;
	y = 0;
	memset((void*)GOP->Mode->FrameBufferBase,0,GOP->Mode->FrameBufferSize);
}

#else

static unsigned short * textmemptr = (unsigned short *)0xB8000;

static void placech(unsigned char c, int x, int y, int attr) {
	unsigned short *where;
	unsigned att = attr << 8;
	where = textmemptr + (y * 80 + x);
	*where = c | att;
}

void clear_() {
	x = 0;
	y = 0;
	for (int y = 0; y < 24; ++y) {
		for (int x = 0; x < 80; ++x) {
			placech(' ', x, y, 0x00);
		}
	}
}
#endif

int x = 0;
int y = 0;
int attr = 0x07;

void print_(char * str) {
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

void move_cursor(int _x, int _y) {
	x = _x;
	y = _y;
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

