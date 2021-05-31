/**
 * @file  kernel/misc/fbterm.c
 * @brief Crude framebuffer terminal for 32bpp framebuffer devices.
 * @author K. Lange
 *
 * Provides a simple graphical text renderer for early startup, with
 * support for simple escape sequences, on top of a framebuffer set up
 * with the `lfbvideo` module.
 */
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/args.h>

/* Exported cell sizing */
size_t fbterm_width = 0;
size_t fbterm_height = 0;

/* Whether to scroll or wrap when cursor reaches the bottom. */
static int fbterm_scroll = 0;

/* Is this in a header somewhere? */
extern uint8_t * lfb_vid_memory;
extern uint16_t lfb_resolution_x;
extern uint16_t lfb_resolution_y;
extern uint16_t lfb_resolution_b;
extern uint32_t lfb_resolution_s;
extern size_t lfb_memsize;

/* Bitmap font details */
#include "../../apps/terminal-font.h"
#define char_height 20
#define char_width  9

/* Default colors */
#define BG_COLOR 0xFF050505 /* Background */
#define FG_COLOR 0xFFCCCCCC /* Main text color */

static uint32_t fg_color = FG_COLOR;
static uint32_t bg_color = BG_COLOR;

static void set_point(int x, int y, uint32_t value) {
	((uint32_t*)lfb_vid_memory)[y * lfb_resolution_x + x] = value;
}

static void write_char(int x, int y, int val, uint32_t color) {
	if (val > 128) {
		val = 4;
	}
	uint16_t * c = large_font[val];
	for (uint8_t i = 0; i < char_height; ++i) {
		for (uint8_t j = 0; j < char_width; ++j) {
			if (c[i] & (1 << (15-j))) {
				set_point(x+j,y+i,color);
			} else {
				set_point(x+j,y+i,bg_color);
			}
		}
	}
}

/* We push text in one pixel, which unfortunately means we have slightly less room,
 * but it also means the text doesn't run right into the left and right edges which
 * just looks kinda bad. */
#define LEFT_PAD 1
static int x = LEFT_PAD;
static int y = 0;

/**
 * @brief Basic 16-color ANSI palette with Tango colors.
 */
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

static int term_state = 0;
static char term_buf[1024] = {0};
static int term_buf_c = 0;

static void invert_at(int x, int y) {
	for (uint8_t i = 0; i < char_height; ++i) {
		for (uint8_t j = 0; j < char_width; ++j) {
			uint32_t current = ((uint32_t*)lfb_vid_memory)[(y+i) * lfb_resolution_x + (x+j)];
			uint8_t r = (current >> 16) & 0xFF;
			uint8_t g = (current >> 8) & 0xFF;
			uint8_t b = (current) & 0xFF;
			current = 0xFF000000 | ((0xFF-r) << 16) | ((0xFF-g) << 8) | ((0xFF-b));
			((uint32_t*)lfb_vid_memory)[(y+i) * lfb_resolution_x + (x+j)] = current;
		}
	}
}

static void process_char(char ch) {
	if (term_state == 1) {
		if (ch == '[') {
			term_buf_c = 0;
			term_buf[term_buf_c] = '\0';
			term_state = 2;
		} else {
			term_state = 0;
			process_char(ch);
		}
		return;
	} else if (term_state == 2) {
		if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')) {
			/* do the thing */
			switch (ch) {
				case 'm': {
					char * arg = &term_buf[0];
					char * next;
					int argC = 0;
					int isBold = 0;
					do {
						next = strchr(arg, ';');
						if (next) { *next = '\0'; next++; }
						int asInt = atoi(arg);
						if (asInt == 0) {
							fg_color = FG_COLOR;
							bg_color = BG_COLOR;
							isBold = 0;
						} else if (asInt == 1) {
							isBold = 1;
						} else if (asInt == 22) {
							fg_color = FG_COLOR;
							isBold = 0;
						} else if (asInt >= 30 && asInt <= 37) {
							fg_color = term_colors[asInt-30 + (isBold ? 8 : 0)];
						} else if (asInt >= 90 && asInt <= 97) {
							fg_color = term_colors[asInt-90 + 8];
						} else if (asInt >= 40 && asInt <= 47) {
							bg_color = term_colors[asInt-40 + (isBold ? 8 : 0)];
						} else if (asInt >= 100 && asInt <= 107) {
							bg_color = term_colors[asInt-100 + 8];
						} else if (asInt == 38) {
							fg_color = FG_COLOR;
						} else if (asInt == 48) {
							bg_color = BG_COLOR;
						} else if (asInt == 7) {
							uint32_t tmp = fg_color;
							fg_color = bg_color;
							bg_color = tmp;
						}
						arg = next;
						argC++;
					} while (arg);
					break;
				}
				case 'G': {
					/* Set cursor column */
					invert_at(x,y);
					x = LEFT_PAD + (atoi(term_buf) - 1) * char_width;
					invert_at(x,y);
					break;
				}
				case 'K': {
					if (atoi(term_buf) == 0) {
						for (int j = y; j < y + char_height; ++j) {
							for (int i = x; i < lfb_resolution_x; ++i) {
								set_point(i,j,bg_color);
							}
						}
						invert_at(x,y);
					}
					break;
				}
			}
			term_state = 0;
		} else {
			term_buf[term_buf_c++] = ch;
			term_buf[term_buf_c] = '\0';
		}
		return;
	} else if (ch == '\033') {
		term_state = 1;
		return;
	}

	write_char(x,y,' ',bg_color);
	switch (ch) {
		case '\n':
			x = LEFT_PAD;
			y += char_height;
			break;
		case '\r':
			x = LEFT_PAD;
			break;
		case '\b':
			if (x > LEFT_PAD) {
				x -= char_width;
				write_char(x,y,' ',fg_color);
			}
			break;
		default:
			if ((unsigned int)ch > 127) return;
			write_char(x,y,ch,fg_color);
			x += char_width;
			break;
	}
	if (x > lfb_resolution_x) {
		y += char_height;
		x = LEFT_PAD;
	}
	if (y > lfb_resolution_y - char_height) {
		if (fbterm_scroll) {
			y -= char_height;
			memmove(lfb_vid_memory, lfb_vid_memory + sizeof(uint32_t) * lfb_resolution_x * char_height, (lfb_resolution_y - char_height) * lfb_resolution_x * 4);
			memset(lfb_vid_memory + sizeof(uint32_t) * (lfb_resolution_y - char_height) * lfb_resolution_x, 0x05, char_height * lfb_resolution_x * 4);
		} else {
			y = 0;
		}
	}
	invert_at(x,y);
}

static size_t (*previous_writer)(size_t,uint8_t*) = NULL;

size_t fbterm_write(size_t size, uint8_t *buffer) {
	if (!buffer) return 0;
	for (unsigned int i = 0; i < size; ++i) {
		process_char(buffer[i]);
	}
	if (previous_writer) previous_writer(size,buffer);
	return size;
}

void fbterm_initialize(void) {
	memset(lfb_vid_memory, 0x05, lfb_resolution_x * lfb_resolution_y * 4);

	if (args_present("fbterm-scroll")) {
		fbterm_scroll = 1;
	}

	fbterm_width = (lfb_resolution_x - LEFT_PAD) / char_width;
	fbterm_height = (lfb_resolution_y) / char_height;
	previous_writer = printf_output;
	printf_output = fbterm_write;
}
