#include <stdint.h>
#include <kernel/string.h>
#include <kernel/printf.h>

static int fbterm_scroll = 0;
static void (*write_char)(int, int, int, uint32_t) = NULL;
static int (*get_width)(void) = NULL;
static int (*get_height)(void) = NULL;
static void (*scroll_terminal)(void) = NULL;

static int x = 0;
static int y = 0;
static int term_state = 0;
static char term_buf[1024] = {0};
static int term_buf_c = 0;

/* Is this in a header somewhere? */
extern uint8_t * lfb_vid_memory;
extern uint16_t lfb_resolution_x;
extern uint16_t lfb_resolution_y;
extern uint16_t lfb_resolution_b;
extern uint32_t lfb_resolution_s;
extern size_t lfb_memsize;

/* Bitmap font details */
#include "../../../../apps/terminal-font.h"
#define char_height LARGE_FONT_CELL_HEIGHT
#define char_width  LARGE_FONT_CELL_WIDTH

/* Default colors */
#define BG_COLOR 0xFF000000 /* Background */
#define FG_COLOR 0xFFCCCCCC /* Main text color */

static uint32_t fg_color = FG_COLOR;
static uint32_t bg_color = BG_COLOR;

extern uint32_t lfb_resolution_s;

static inline void set_point(int x, int y, uint32_t value) {
	if (lfb_resolution_b == 32) {
		((uint32_t*)lfb_vid_memory)[y * (lfb_resolution_s/4) + x] = value;
	} else if (lfb_resolution_b == 24) {
		lfb_vid_memory[y * lfb_resolution_s + x * 3 + 0] = (value >> 0) & 0xFF;
		lfb_vid_memory[y * lfb_resolution_s + x * 3 + 1] = (value >> 8) & 0xFF;
		lfb_vid_memory[y * lfb_resolution_s + x * 3 + 2] = (value >> 16) & 0xFF;
	}
}

static void fb_write_char(int _x, int _y, int val, uint32_t color) {
	if (val > 128) {
		val = 4;
	}

	int x = 1 + _x * char_width;
	int y = _y * char_height;

	uint8_t * c = large_font[val];
	for (uint8_t i = 0; i < char_height; ++i) {
		for (uint8_t j = 0; j < char_width; ++j) {
			if (c[i] & (1 << (LARGE_FONT_MASK-j))) {
				set_point(x+j,y+i,color);
			} else {
				set_point(x+j,y+i,bg_color);
			}
		}
	}
}

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

static int fb_get_width(void) {
	return (lfb_resolution_x - 1) / char_width;
}

static int fb_get_height(void) {
	return lfb_resolution_y / char_height;
}

static void fb_scroll_terminal(void) {
	memmove(lfb_vid_memory, lfb_vid_memory + sizeof(uint32_t) * lfb_resolution_x * char_height, (lfb_resolution_y - char_height) * lfb_resolution_x * 4);
	memset(lfb_vid_memory + sizeof(uint32_t) * (lfb_resolution_y - char_height) * lfb_resolution_x, 0x00, char_height * lfb_resolution_x * 4);
}

static void draw_square(int x, int y) {
	int center_x = lfb_resolution_x / 2;
	int center_y = lfb_resolution_y / 2;
	for (size_t _y = 0; _y < 7; ++_y) {
		uint32_t color = 0xFF00B2FF - (y * 8 + _y) * 0x200;
		for (size_t _x = 0; _x < 7; ++_x) {
			set_point(center_x - 32 + x * 8 + _x, center_y - 32 + y * 8 + _y, color);
		}
	}
}

static void fbterm_draw_logo(void) {
	uint64_t logo_squares = 0x981818181818FFFFUL;
	for (size_t y = 0; y < 8; ++y) {
		for (size_t x = 0; x < 8; ++x) {
			if (logo_squares & (1 << x)) {
				draw_square(x,y);
			}
		}
		logo_squares >>= 8;
	}
}

static void fbterm_init_framebuffer(void) {
	write_char = fb_write_char;
	get_width = fb_get_width;
	get_height = fb_get_height;
	scroll_terminal = fb_scroll_terminal;
	fbterm_draw_logo();
}

static void cursor_update(void) {
	if (x >= get_width()) {
		x = 0;
		y++;
	}
	if (y >= get_height()) {
		if (fbterm_scroll) {
			y--;
			scroll_terminal();
		} else {
			y = 0;
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
					x = atoi(term_buf) - 1;
					break;
				}
				case 'K': {
					if (atoi(term_buf) == 0) {
						for (int i = x; i < get_width(); ++i) {
							write_char(i,y,' ',bg_color);
						}
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
			x = 0;
			y++;
			break;
		case '\r':
			x = 0;
			break;
		case '\b':
			if (x) {
				x--;
				write_char(x,y,' ',fg_color);
			}
			break;
		default:
			if ((unsigned int)ch > 127) return;
			write_char(x,y,ch,fg_color);
			x++;
			break;
	}
	cursor_update();
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
	if (!lfb_resolution_x) {
		return;
	}
	fbterm_init_framebuffer();
	previous_writer = printf_output;
	printf_output = fbterm_write;
	printf("fbterm: Generic framebuffer text output enabled.\n");
}

