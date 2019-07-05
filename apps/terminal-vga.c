/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014-2018 K. Lange
 *
 * Terminal Emulator - VGA
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <getopt.h>
#include <errno.h>
#include <pty.h>
#include <sys/fswait.h>

#include <wchar.h>

#include <toaru/decodeutf8.h>
#include <toaru/kbd.h>
#include <toaru/graphics.h>
#include <toaru/termemu.h>
#include <toaru/mouse.h>

#include "vga-palette.h"

#define USE_BELL 0

/* master and slave pty descriptors */
static int fd_master, fd_slave;
static FILE * terminal;

static uint16_t term_width     = 80;    /* Width of the terminal (in cells) */
static uint16_t term_height    = 25;    /* Height of the terminal (in cells) */
static uint16_t csr_x          = 0;    /* Cursor X */
static uint16_t csr_y          = 0;    /* Cursor Y */
static term_cell_t * term_buffer    = NULL; /* The terminal cell buffer */
static term_cell_t * term_buffer_a = NULL;
static term_cell_t * term_buffer_b = NULL;
static int active_buffer  = 0;
static int _orig_x = 0;
static int _orig_y = 0;
static uint32_t _orig_fg = 7;
static uint32_t _orig_bg = 0;
static uint32_t current_fg     = 7;    /* Current foreground color */
static uint32_t current_bg     = 0;    /* Current background color */
static uint8_t  cursor_on      = 1;    /* Whether or not the cursor should be rendered */

static uint8_t  _login_shell   = 0;    /* Whether we're going to display a login shell or not */

static uint64_t mouse_ticks = 0;

static int selection = 0;
static int selection_start_x = 0;
static int selection_start_y = 0;
static int selection_end_x = 0;
static int selection_end_y = 0;
static char * selection_text = NULL;

#define char_width 1
#define char_height 1

term_state_t * ansi_state = NULL;

void reinit(void); /* Defined way further down */
void term_redraw_cursor();

void term_clear();

void dump_buffer();

static uint64_t get_ticks(void) {
	struct timeval now;
	gettimeofday(&now, NULL);

	return (uint64_t)now.tv_sec * 1000000LL + (uint64_t)now.tv_usec;
}

static int color_distance(uint32_t a, uint32_t b) {
	int a_r = (a & 0xFF0000) >> 16;
	int a_g = (a & 0xFF00) >> 8;
	int a_b = (a & 0xFF);

	int b_r = (b & 0xFF0000) >> 16;
	int b_g = (b & 0xFF00) >> 8;
	int b_b = (b & 0xFF);

	int distance = 0;
	distance += abs(a_r - b_r) * 3;
	distance += abs(a_g - b_g) * 6;
	distance += abs(a_b - b_b) * 10;

	return distance;
}

static uint32_t vga_base_colors[] = {
	0x000000,
	0xAA0000,
	0x00AA00,
	0xAA5500,
	0x0000AA,
	0xAA00AA,
	0x00AAAA,
	0xAAAAAA,
	0x555555,
	0xFF5555,
	0x55AA55,
	0xFFFF55,
	0x5555FF,
	0xFF55FF,
	0x55FFFF,
	0xFFFFFF,
};

#if 0
static int is_gray(uint32_t a) {
	int a_r = (a & 0xFF0000) >> 16;
	int a_g = (a & 0xFF00) >> 8;
	int a_b = (a & 0xFF);

	return (a_r == a_g && a_g == a_b);
}
#endif

static int best_match(uint32_t a) {
	int best_distance = INT32_MAX;
	int best_index = 0;
	for (int j = 0; j < 16; ++j) {
		int distance = color_distance(a, vga_base_colors[j]);
		if (distance < best_distance) {
			best_index = j;
			best_distance = distance;
		}
	}
	return best_index;
}


volatile int exit_application = 0;

/* Returns the lower of two shorts */
uint16_t min(uint16_t a, uint16_t b) {
	return (a < b) ? a : b;
}

/* Returns the higher of two shorts */
uint16_t max(uint16_t a, uint16_t b) {
	return (a > b) ? a : b;
}

void set_title(char * c) {
	/* Do nothing */
}

static void cell_redraw(uint16_t x, uint16_t y);
static void cell_redraw_inverted(uint16_t x, uint16_t y);

void iterate_selection(void (*func)(uint16_t x, uint16_t y)) {
	if (selection_end_y < selection_start_y) {
		for (int x = selection_end_x; x < term_width; ++x) {
			func(x, selection_end_y);
		}
		for (int y = selection_end_y + 1; y < selection_start_y; ++y) {
			for (int x = 0; x < term_width; ++x) {
				func(x, y);
			}
		}
		for (int x = 0; x <= selection_start_x; ++x) {
			func(x, selection_start_y);
		}
	} else if (selection_start_y == selection_end_y) {
		if (selection_start_x > selection_end_x) {
			for (int x = selection_end_x; x <= selection_start_x; ++x) {
				func(x, selection_start_y);
			}
		} else {
			for (int x = selection_start_x; x <= selection_end_x; ++x) {
				func(x, selection_start_y);
			}
		}
	} else {
		for (int x = selection_start_x; x < term_width; ++x) {
			func(x, selection_start_y);
		}
		for (int y = selection_start_y + 1; y < selection_end_y; ++y) {
			for (int x = 0; x < term_width; ++x) {
				func(x, y);
			}
		}
		for (int x = 0; x <= selection_end_x; ++x) {
			func(x, selection_end_y);
		}
	}

}

void redraw_selection(void) {
	iterate_selection(cell_redraw_inverted);
}

static void redraw_new_selection(int old_x, int old_y) {
	if (selection_end_y == selection_start_y && old_y != selection_start_y) {
		int a, b;
		a = selection_end_x;
		b = selection_end_y;
		selection_end_x = old_x;
		selection_end_y = old_y;
		iterate_selection(cell_redraw);
		selection_end_x = a;
		selection_end_y = b;
		iterate_selection(cell_redraw_inverted);
	} else {
		int a, b;
		a = selection_start_x;
		b = selection_start_y;

		selection_start_x = old_x;
		selection_start_y = old_y;

		/* Figure out direction */
		if (old_y < b) {
			/* Backwards */
			if (selection_end_y < old_y || (selection_end_y == old_y && selection_end_x < old_x)) {
				/* Selection extended */
				iterate_selection(cell_redraw_inverted);
			} else {
				/* Selection got smaller */
				iterate_selection(cell_redraw);
			}
		} else if (old_y == b) {
			/* Was a single line */
			if (selection_end_y == b) {
				/* And still is */
				if (old_x < a) {
					/* Backwards */
					if (selection_end_x < old_x) {
						iterate_selection(cell_redraw_inverted);
					} else {
						iterate_selection(cell_redraw);
					}
				} else {
					if (selection_end_x < old_x) {
						iterate_selection(cell_redraw);
					} else {
						iterate_selection(cell_redraw_inverted);
					}
				}
			} else if (selection_end_y < b) {
				/* Moved up */
				if (old_x <= a) {
					/* Should be fine with just append */
					iterate_selection(cell_redraw_inverted);
				} else {
					/* Need to erase first */
					iterate_selection(cell_redraw);
					selection_start_x = a;
					selection_start_y = b;
					iterate_selection(cell_redraw_inverted);
				}
			} else if (selection_end_y > b) {
				if (old_x >= a) {
					/* Should be fine with just append */
					iterate_selection(cell_redraw_inverted);
				} else {
					/* Need to erase first */
					iterate_selection(cell_redraw);
					selection_start_x = a;
					selection_start_y = b;
					iterate_selection(cell_redraw_inverted);
				}
			}
		} else {
			/* Forward */
			if (selection_end_y < old_y || (selection_end_y == old_y && selection_end_x < old_x)) {
				/* Selection got smaller */
				iterate_selection(cell_redraw);
			} else {
				/* Selection extended */
				iterate_selection(cell_redraw_inverted);
			}
		}

		cell_redraw_inverted(a,b);
		cell_redraw_inverted(selection_end_x, selection_end_y);

		/* Restore */
		selection_start_x = a;
		selection_start_y = b;
	}
}

static int _selection_count = 0;
static int _selection_i = 0;

static int to_eight(uint32_t codepoint, char * out) {
	memset(out, 0x00, 7);

	if (codepoint < 0x0080) {
		out[0] = (char)codepoint;
	} else if (codepoint < 0x0800) {
		out[0] = 0xC0 | (codepoint >> 6);
		out[1] = 0x80 | (codepoint & 0x3F);
	} else if (codepoint < 0x10000) {
		out[0] = 0xE0 | (codepoint >> 12);
		out[1] = 0x80 | ((codepoint >> 6) & 0x3F);
		out[2] = 0x80 | (codepoint & 0x3F);
	} else if (codepoint < 0x200000) {
		out[0] = 0xF0 | (codepoint >> 18);
		out[1] = 0x80 | ((codepoint >> 12) & 0x3F);
		out[2] = 0x80 | ((codepoint >> 6) & 0x3F);
		out[3] = 0x80 | ((codepoint) & 0x3F);
	} else if (codepoint < 0x4000000) {
		out[0] = 0xF8 | (codepoint >> 24);
		out[1] = 0x80 | (codepoint >> 18);
		out[2] = 0x80 | ((codepoint >> 12) & 0x3F);
		out[3] = 0x80 | ((codepoint >> 6) & 0x3F);
		out[4] = 0x80 | ((codepoint) & 0x3F);
	} else {
		out[0] = 0xF8 | (codepoint >> 30);
		out[1] = 0x80 | ((codepoint >> 24) & 0x3F);
		out[2] = 0x80 | ((codepoint >> 18) & 0x3F);
		out[3] = 0x80 | ((codepoint >> 12) & 0x3F);
		out[4] = 0x80 | ((codepoint >> 6) & 0x3F);
		out[5] = 0x80 | ((codepoint) & 0x3F);
	}

	return strlen(out);
}

void count_selection(uint16_t x, uint16_t y) {
	term_cell_t * cell = (term_cell_t *)((uintptr_t)term_buffer + (y * term_width + x) * sizeof(term_cell_t));
	if (((uint32_t *)cell)[0] != 0x00000000) {
		char tmp[7];
		_selection_count += to_eight(cell->c, tmp);
	}
	if (x == term_width - 1) {
		_selection_count++;
	}
}

void write_selection(uint16_t x, uint16_t y) {
	term_cell_t * cell = (term_cell_t *)((uintptr_t)term_buffer + (y * term_width + x) * sizeof(term_cell_t));
	if (((uint32_t *)cell)[0] != 0x00000000) {
		char tmp[7];
		int count = to_eight(cell->c, tmp);
		for (int i = 0; i < count; ++i) {
			selection_text[_selection_i] = tmp[i];
			_selection_i++;
		}
	}
	if (x == term_width - 1) {
		selection_text[_selection_i] = '\n';;
		_selection_i++;
	}
}


char * copy_selection(void) {
	_selection_count = 0;
	iterate_selection(count_selection);

	if (selection_text) {
		free(selection_text);
	}

	if (_selection_count == 0) {
		return NULL;
	}

	selection_text = malloc(_selection_count + 1);
	selection_text[_selection_count] = '\0';
	_selection_i = 0;
	iterate_selection(write_selection);

	if (selection_text[_selection_count-1] == '\n') {
		/* Don't end on a line feed */
		selection_text[_selection_count-1] = '\0';
	}

	return selection_text;
}

void input_buffer_stuff(char * str) {
	size_t s = strlen(str) + 1;
	write(fd_master, str, s);
}

unsigned short * textmemptr = (unsigned short *)0xB8000;
void placech(unsigned char c, int x, int y, int attr) {
	unsigned short *where;
	unsigned att = attr << 8;
	where = textmemptr + (y * 80 + x);
	*where = c | att;
}

/* ANSI-to-VGA */
char vga_to_ansi[] = {
	0, 4, 2, 6, 1, 5, 3, 7,
	8,12,10,14, 9,13,11,15
};

uint32_t ununicode(uint32_t c) {
	switch (c) {
		case L'☺': return 1;
		case L'☻': return 2;
		case L'♥': return 3;
		case L'♦': return 4;
		case L'♣': return 5;
		case L'♠': return 6;
		case L'•': return 7;
		case L'◘': return 8;
		case L'○': return 9;
		case L'◙': return 10;
		case L'♂': return 11;
		case L'♀': return 12;
		case L'♪': return 13;
		case L'♫': return 14;
		case L'☼': return 15;
		case L'►': return 16;
		case L'◄': return 17;
		case L'↕': return 18;
		case L'‼': return 19;
		case L'¶': return 20;
		case L'§': return 21;
		case L'▬': return 22;
		case L'↨': return 23;
		case L'↑': return 24;
		case L'↓': return 25;
		case L'→': return 26;
		case L'←': return 27;
		case L'∟': return 28;
		case L'↔': return 29;
		case L'▲': return 30;
		case L'▼': return 31;
		/* ASCII text */
		case L'⌂': return 127;
		case L'Ç': return 128;
		case L'ü': return 129;
		case L'é': return 130;
		case L'â': return 131;
		case L'ä': return 132;
		case L'à': return 133;
		case L'å': return 134;
		case L'ç': return 135;
		case L'ê': return 136;
		case L'ë': return 137;
		case L'è': return 138;
		case L'ï': return 139;
		case L'î': return 140;
		case L'ì': return 141;
		case L'Ä': return 142;
		case L'Å': return 143;
		case L'É': return 144;
		case L'æ': return 145;
		case L'Æ': return 146;
		case L'ô': return 147;
		case L'ö': return 148;
		case L'ò': return 149;
		case L'û': return 150;
		case L'ù': return 151;
		case L'ÿ': return 152;
		case L'Ö': return 153;
		case L'Ü': return 154;
		case L'¢': return 155;
		case L'£': return 156;
		case L'¥': return 157;
		case L'₧': return 158;
		case L'ƒ': return 159;
		case L'á': return 160;
		case L'í': return 161;
		case L'ó': return 162;
		case L'ú': return 163;
		case L'ñ': return 164;
		case L'Ñ': return 165;
		case L'ª': return 166;
		case L'º': return 167;
		case L'¿': return 168;
		case L'⌐': return 169;
		case L'¬': return 170;
		case L'½': return 171;
		case L'¼': return 172;
		case L'¡': return 173;
		case L'«': return 174;
		case L'»': return 175;
		case L'░': return 176;
		case L'▒': return 177;
		case L'▓': return 178;
		case L'│': return 179;
		case L'┤': return 180;
		case L'╡': return 181;
		case L'╢': return 182;
		case L'╖': return 183;
		case L'╕': return 184;
		case L'╣': return 185;
		case L'║': return 186;
		case L'╗': return 187;
		case L'╝': return 188;
		case L'╜': return 189;
		case L'╛': return 190;
		case L'┐': return 191;
		case L'└': return 192;
		case L'┴': return 193;
		case L'┬': return 194;
		case L'├': return 195;
		case L'─': return 196;
		case L'┼': return 197;
		case L'╞': return 198;
		case L'╟': return 199;
		case L'╚': return 200;
		case L'╔': return 201;
		case L'╩': return 202;
		case L'╦': return 203;
		case L'╠': return 204;
		case L'═': return 205;
		case L'╬': return 206;
		case L'╧': return 207;
		case L'╨': return 208;
		case L'╤': return 209;
		case L'╥': return 210;
		case L'╙': return 211;
		case L'╘': return 212;
		case L'╒': return 213;
		case L'╓': return 214;
		case L'╫': return 215;
		case L'╪': return 216;
		case L'┘': return 217;
		case L'┌': return 218;
		case L'█': return 219;
		case L'▄': return 220;
		case L'▌': return 221;
		case L'▐': return 222;
		case L'▀': return 223;
		case L'α': return 224;
		case L'ß': return 225;
		case L'Γ': return 226;
		case L'π': return 227;
		case L'Σ': return 228;
		case L'σ': return 229;
		case L'µ': return 230;
		case L'τ': return 231;
		case L'Φ': return 232;
		case L'Θ': return 233;
		case L'Ω': return 234;
		case L'δ': return 235;
		case L'∞': return 236;
		case L'φ': return 237;
		case L'ε': return 238;
		case L'∩': return 239;
		case L'≡': return 240;
		case L'±': return 241;
		case L'≥': return 242;
		case L'≤': return 243;
		case L'⌠': return 244;
		case L'⌡': return 245;
		case L'÷': return 246;
		case L'≈': return 247;
		case L'°': return 248;
		case L'∙': return 249;
		case L'·': return 250;
		case L'√': return 251;
		case L'ⁿ': return 252;
		case L'²': return 253;
		case L'■': return 254;
		/* Special cases */
		case L'▏': return 179; /* Replace with vertical bar to support bim better */
	}
	return 4;
}

void
term_write_char(
		uint32_t val,
		uint16_t x,
		uint16_t y,
		uint32_t fg,
		uint32_t bg,
		uint8_t flags
		) {
	if (val > 128) val = ununicode(val);
	if (fg > 256) {
		fg = best_match(fg);
	}
	if (bg > 256) {
		bg = best_match(bg);
	}
	if (fg > 16) {
		fg = vga_colors[fg];
	}
	if (bg > 16) {
		bg = vga_colors[bg];
	}
	if (fg == 16) fg = 0;
	if (bg == 16) bg = 0;
	placech(val, x, y, (vga_to_ansi[fg] & 0xF) | (vga_to_ansi[bg] << 4));
}

static void cell_set(uint16_t x, uint16_t y, uint32_t c, uint32_t fg, uint32_t bg, uint8_t flags) {
	if (x >= term_width || y >= term_height) return;
	term_cell_t * cell = (term_cell_t *)((uintptr_t)term_buffer + (y * term_width + x) * sizeof(term_cell_t));
	cell->c     = c;
	cell->fg    = fg;
	cell->bg    = bg;
	cell->flags = flags;
}

static void cell_redraw(uint16_t x, uint16_t y) {
	if (x >= term_width || y >= term_height) return;
	term_cell_t * cell = (term_cell_t *)((uintptr_t)term_buffer + (y * term_width + x) * sizeof(term_cell_t));
	if (((uint32_t *)cell)[0] == 0x00000000) {
		term_write_char(' ', x * char_width, y * char_height, TERM_DEFAULT_FG, TERM_DEFAULT_BG, TERM_DEFAULT_FLAGS);
	} else {
		term_write_char(cell->c, x * char_width, y * char_height, cell->fg, cell->bg, cell->flags);
	}
}

static void cell_redraw_inverted(uint16_t x, uint16_t y) {
	if (x >= term_width || y >= term_height) return;
	term_cell_t * cell = (term_cell_t *)((uintptr_t)term_buffer + (y * term_width + x) * sizeof(term_cell_t));
	if (((uint32_t *)cell)[0] == 0x00000000) {
		term_write_char(' ', x * char_width, y * char_height, TERM_DEFAULT_BG, TERM_DEFAULT_FG, TERM_DEFAULT_FLAGS | ANSI_SPECBG);
	} else {
		term_write_char(cell->c, x * char_width, y * char_height, cell->bg, cell->fg, cell->flags | ANSI_SPECBG);
	}
}

#if 0
static void cell_redraw_box(uint16_t x, uint16_t y) {
	if (x >= term_width || y >= term_height) return;
	term_cell_t * cell = (term_cell_t *)((uintptr_t)term_buffer + (y * term_width + x) * sizeof(term_cell_t));
	if (((uint32_t *)cell)[0] == 0x00000000) {
		term_write_char(' ', x * char_width, y * char_height, TERM_DEFAULT_FG, TERM_DEFAULT_BG, TERM_DEFAULT_FLAGS | ANSI_BORDER);
	} else {
		term_write_char(cell->c, x * char_width, y * char_height, cell->fg, cell->bg, cell->flags | ANSI_BORDER);
	}
}
#endif

void render_cursor() {
	if (!cursor_on) return;
	cell_redraw_inverted(csr_x, csr_y);
}

static uint8_t cursor_flipped = 0;
void draw_cursor() {
	if (!cursor_on) return;
	mouse_ticks = get_ticks();
	cursor_flipped = 0;
	render_cursor();
}

void term_redraw_all() { 
	for (uint16_t y = 0; y < term_height; ++y) {
		for (uint16_t x = 0; x < term_width; ++x) {
			cell_redraw(x,y);
		}
	}
}

void term_scroll(int how_much) {
	if (how_much >= term_height || -how_much >= term_height) {
		term_clear();
		return;
	}
	if (how_much == 0) {
		return;
	}
	if (how_much > 0) {
		/* Shift terminal cells one row up */
		memmove(term_buffer, (void *)((uintptr_t)term_buffer + sizeof(term_cell_t) * term_width), sizeof(term_cell_t) * term_width * (term_height - how_much));
		/* Reset the "new" row to clean cells */
		memset((void *)((uintptr_t)term_buffer + sizeof(term_cell_t) * term_width * (term_height - how_much)), 0x0, sizeof(term_cell_t) * term_width * how_much);
		for (int i = 0; i < how_much; ++i) {
			for (uint16_t x = 0; x < term_width; ++x) {
				cell_set(x,term_height - how_much,' ', current_fg, current_bg, ansi_state->flags);
			}
		}
		term_redraw_all();
	} else {
		how_much = -how_much;
		/* Shift terminal cells one row up */
		memmove((void *)((uintptr_t)term_buffer + sizeof(term_cell_t) * term_width), term_buffer, sizeof(term_cell_t) * term_width * (term_height - how_much));
		/* Reset the "new" row to clean cells */
		memset(term_buffer, 0x0, sizeof(term_cell_t) * term_width * how_much);
		term_redraw_all();
	}
}

int is_wide(uint32_t codepoint) {
	if (codepoint < 256) return 0;
	return wcwidth(codepoint) == 2;
}

void term_write(char c) {
	static uint32_t codepoint = 0;
	static uint32_t unicode_state = 0;

	cell_redraw(csr_x, csr_y);
	if (!decode(&unicode_state, &codepoint, (uint8_t)c)) {
		if (c == '\r') {
			csr_x = 0;
			draw_cursor();
			return;
		}
		if (csr_x == term_width) {
			csr_x = 0;
			++csr_y;
			if (c == '\n') return;
		}
		if (csr_y == term_height) {
			term_scroll(1);
			csr_y = term_height - 1;
		}
		if (c == '\n') {
			++csr_y;
			if (csr_y == term_height) {
				term_scroll(1);
				csr_y = term_height - 1;
			}
			draw_cursor();
		} else if (c == '\007') {
			/* bell */
		} else if (c == '\b') {
			if (csr_x > 0) {
				--csr_x;
			}
			cell_redraw(csr_x, csr_y);
			draw_cursor();
		} else if (c == '\t') {
			csr_x += (8 - csr_x % 8);
			draw_cursor();
		} else {
			int wide = is_wide(codepoint);
			uint8_t flags = ansi_state->flags;
			if (wide && csr_x == term_width - 1) {
				csr_x = 0;
				++csr_y;
			}
			if (wide) {
				flags = flags | ANSI_WIDE;
			}
			cell_set(csr_x,csr_y, codepoint, current_fg, current_bg, flags);
			cell_redraw(csr_x,csr_y);
			csr_x++;
			if (wide && csr_x != term_width) {
				cell_set(csr_x, csr_y, 0xFFFF, current_fg, current_bg, ansi_state->flags);
				cell_redraw(csr_x,csr_y);
				cell_redraw(csr_x-1,csr_y);
				csr_x++;
			}
		}
	} else if (unicode_state == UTF8_REJECT) {
		unicode_state = 0;
	}
	draw_cursor();
}

void term_set_csr(int x, int y) {
	cell_redraw(csr_x,csr_y);
	csr_x = x;
	csr_y = y;
	draw_cursor();
}

int term_get_csr_x() {
	return csr_x;
}

int term_get_csr_y() {
	return csr_y;
}

void term_set_csr_show(int on) {
	cursor_on = on;
	if (on) {
		draw_cursor();
	}
}

void term_set_colors(uint32_t fg, uint32_t bg) {
	current_fg = fg;
	current_bg = bg;
}

void term_redraw_cursor() {
	if (term_buffer) {
		draw_cursor();
	}
}

void flip_cursor() {
	if (cursor_flipped) {
		cell_redraw(csr_x, csr_y);
	} else {
		render_cursor();
	}
	cursor_flipped = 1 - cursor_flipped;
}

void term_set_cell(int x, int y, uint32_t c) {
	cell_set(x, y, c, current_fg, current_bg, ansi_state->flags);
	cell_redraw(x, y);
}

void term_redraw_cell(int x, int y) {
	if (x < 0 || y < 0 || x >= term_width || y >= term_height) return;
	cell_redraw(x,y);
}

void term_clear(int i) {
	if (i == 2) {
		/* Oh dear */
		csr_x = 0;
		csr_y = 0;
		memset((void *)term_buffer, 0x00, term_width * term_height * sizeof(term_cell_t));
		term_redraw_all();
	} else if (i == 0) {
		for (int x = csr_x; x < term_width; ++x) {
			term_set_cell(x, csr_y, ' ');
		}
		for (int y = csr_y + 1; y < term_height; ++y) {
			for (int x = 0; x < term_width; ++x) {
				term_set_cell(x, y, ' ');
			}
		}
	} else if (i == 1) {
		for (int y = 0; y < csr_y; ++y) {
			for (int x = 0; x < term_width; ++x) {
				term_set_cell(x, y, ' ');
			}
		}
		for (int x = 0; x < csr_x; ++x) {
			term_set_cell(x, csr_y, ' ');
		}
	}
}

#define INPUT_SIZE 1024
char input_buffer[INPUT_SIZE];
int  input_collected = 0;

void clear_input() {
	memset(input_buffer, 0x0, INPUT_SIZE);
	input_collected = 0;
}

pid_t child_pid = 0;

void handle_input(char c) {
	write(fd_master, &c, 1);
}

void handle_input_s(char * c) {
	write(fd_master, c, strlen(c));
}

void key_event(int ret, key_event_t * event) {
	if (ret) {
		/* Special keys */
		if ((event->modifiers & KEY_MOD_LEFT_SHIFT || event->modifiers & KEY_MOD_RIGHT_SHIFT) &&
			(event->modifiers & KEY_MOD_LEFT_CTRL || event->modifiers & KEY_MOD_RIGHT_CTRL) &&
			(event->keycode == 'c')) {
			if (selection) {
				/* Copy selection */
				copy_selection();
			}
			return;
		}
		if ((event->modifiers & KEY_MOD_LEFT_SHIFT || event->modifiers & KEY_MOD_RIGHT_SHIFT) &&
			(event->modifiers & KEY_MOD_LEFT_CTRL || event->modifiers & KEY_MOD_RIGHT_CTRL) &&
			(event->keycode == 'v')) {
			/* Paste selection */
			if (selection_text) {
				handle_input_s(selection_text);
			}
			return;
		}
		if (event->modifiers & KEY_MOD_LEFT_ALT || event->modifiers & KEY_MOD_RIGHT_ALT) {
			handle_input('\033');
		}
		if ((event->modifiers & KEY_MOD_LEFT_SHIFT || event->modifiers & KEY_MOD_RIGHT_SHIFT) &&
		    event->key == '\t') {
			handle_input_s("\033[Z");
			return;
		}

		/* ENTER = reads as linefeed, should be carriage return */
		if (event->keycode == 10) {
			handle_input('\r');
			return;
		}

		/* BACKSPACE = reads as ^H, should be ^? */
		if (event->keycode == 8) {
			handle_input(0x7F);
			return;
		}

		handle_input(event->key);
	} else {
		if (event->action == KEY_ACTION_UP) return;
		switch (event->keycode) {
			case KEY_F1:
				handle_input_s("\033OP");
				break;
			case KEY_F2:
				handle_input_s("\033OQ");
				break;
			case KEY_F3:
				handle_input_s("\033OR");
				break;
			case KEY_F4:
				handle_input_s("\033OS");
				break;
			case KEY_F5:
				handle_input_s("\033[15~");
				break;
			case KEY_F6:
				handle_input_s("\033[17~");
				break;
			case KEY_F7:
				handle_input_s("\033[18~");
				break;
			case KEY_F8:
				handle_input_s("\033[19~");
				break;
			case KEY_F9:
				handle_input_s("\033[20~");
				break;
			case KEY_F10:
				handle_input_s("\033[21~");
				break;
			case KEY_F11:
				handle_input_s("\033[23~");
				break;
			case KEY_F12:
				handle_input_s("\033[24~");
				break;
			case KEY_ARROW_UP:
				if (event->modifiers & KEY_MOD_LEFT_SHIFT && event->modifiers & KEY_MOD_LEFT_CTRL) {
					handle_input_s("\033[6A");
				} else if (event->modifiers & KEY_MOD_LEFT_CTRL) {
					handle_input_s("\033[5A");
				} else if (event->modifiers & KEY_MOD_LEFT_SHIFT && event->modifiers & KEY_MOD_LEFT_ALT) {
					handle_input_s("\033[4A");
				} else if (event->modifiers & KEY_MOD_LEFT_ALT) {
					handle_input_s("\033[3A");
				} else if (event->modifiers & KEY_MOD_LEFT_SHIFT) {
					handle_input_s("\033[2A");
				} else {
					handle_input_s("\033[A");
				}
				break;
			case KEY_ARROW_DOWN:
				if (event->modifiers & KEY_MOD_LEFT_SHIFT && event->modifiers & KEY_MOD_LEFT_CTRL) {
					handle_input_s("\033[6B");
				} else if (event->modifiers & KEY_MOD_LEFT_CTRL) {
					handle_input_s("\033[5B");
				} else if (event->modifiers & KEY_MOD_LEFT_SHIFT && event->modifiers & KEY_MOD_LEFT_ALT) {
					handle_input_s("\033[4B");
				} else if (event->modifiers & KEY_MOD_LEFT_ALT) {
					handle_input_s("\033[3B");
				} else if (event->modifiers & KEY_MOD_LEFT_SHIFT) {
					handle_input_s("\033[2B");
				} else {
					handle_input_s("\033[B");
				}
				break;
			case KEY_ARROW_RIGHT:
				if (event->modifiers & KEY_MOD_LEFT_SHIFT && event->modifiers & KEY_MOD_LEFT_CTRL) {
					handle_input_s("\033[6C");
				} else if (event->modifiers & KEY_MOD_LEFT_CTRL) {
					handle_input_s("\033[5C");
				} else if (event->modifiers & KEY_MOD_LEFT_SHIFT && event->modifiers & KEY_MOD_LEFT_ALT) {
					handle_input_s("\033[4C");
				} else if (event->modifiers & KEY_MOD_LEFT_ALT) {
					handle_input_s("\033[3C");
				} else if (event->modifiers & KEY_MOD_LEFT_SHIFT) {
					handle_input_s("\033[2C");
				} else {
					handle_input_s("\033[C");
				}
				break;
			case KEY_ARROW_LEFT:
				if (event->modifiers & KEY_MOD_LEFT_SHIFT && event->modifiers & KEY_MOD_LEFT_CTRL) {
					handle_input_s("\033[6D");
				} else if (event->modifiers & KEY_MOD_LEFT_CTRL) {
					handle_input_s("\033[5D");
				} else if (event->modifiers & KEY_MOD_LEFT_SHIFT && event->modifiers & KEY_MOD_LEFT_ALT) {
					handle_input_s("\033[4D");
				} else if (event->modifiers & KEY_MOD_LEFT_ALT) {
					handle_input_s("\033[3D");
				} else if (event->modifiers & KEY_MOD_LEFT_SHIFT) {
					handle_input_s("\033[2D");
				} else {
					handle_input_s("\033[D");
				}
				break;
			case KEY_PAGE_UP:
				handle_input_s("\033[5~");
				break;
			case KEY_PAGE_DOWN:
				handle_input_s("\033[6~");
				break;
			case KEY_HOME:
				handle_input_s("\033[H");
				break;
			case KEY_END:
				handle_input_s("\033[F");
				break;
			case KEY_DEL:
				handle_input_s("\033[3~");
				break;
		}
	}
}

void usage(char * argv[]) {
	printf(
			"VGA Terminal Emulator\n"
			"\n"
			"usage: %s [-b] [-F] [-h]\n"
			"\n"
			" -h --help       \033[3mShow this help message.\033[0m\n"
			"\n",
			argv[0]);
}

int unsupported_int(void) { return 0; }
void unsupported(int x, int y, char * data) { }

#define SWAP(T,a,b) do { T _a = a; a = b; b = _a; } while(0);
static void term_switch_buffer(int buffer) {
	if (buffer != 0 && buffer != 1) return;
	if (buffer != active_buffer) {
		active_buffer = buffer;
		term_buffer = active_buffer == 0 ? term_buffer_a : term_buffer_b;

		SWAP(int, csr_x, _orig_x);
		SWAP(int, csr_y, _orig_y);
		SWAP(uint32_t, current_fg, _orig_fg);
		SWAP(uint32_t, current_bg, _orig_bg);

		term_redraw_all();
	}

}


term_callbacks_t term_callbacks = {
	term_write,
	term_set_colors,
	term_set_csr,
	term_get_csr_x,
	term_get_csr_y,
	term_set_cell,
	term_clear,
	term_scroll,
	term_redraw_cursor,
	input_buffer_stuff,
	set_title,
	unsupported,
	unsupported_int,
	unsupported_int,
	term_set_csr_show,
	term_switch_buffer,
};

void reinit(void) {
	if (term_buffer) {
		/* Do nothing */
	} else {
		term_buffer_a = malloc(sizeof(term_cell_t) * term_width * term_height);
		memset(term_buffer_a, 0x0, sizeof(term_cell_t) * term_width * term_height);

		term_buffer_b = malloc(sizeof(term_cell_t) * term_width * term_height);
		memset(term_buffer_b, 0x0, sizeof(term_cell_t) * term_width * term_height);

		term_buffer = term_buffer_a;
	}

	ansi_state = ansi_init(ansi_state, term_width, term_height, &term_callbacks);
	term_redraw_all();
}


void maybe_flip_cursor(void) {
	uint64_t ticks = get_ticks();
	if (ticks > mouse_ticks + 600000LL) {
		mouse_ticks = ticks;
		flip_cursor();
	}
}


void check_for_exit(void) {
	if (exit_application) return;

	pid_t pid = waitpid(-1, NULL, WNOHANG);

	if (pid != child_pid) return;

	/* Clean up */
	exit_application = 1;
	/* Exit */
	char exit_message[] = "[Process terminated]\n";
	write(fd_slave, exit_message, sizeof(exit_message));
}

static int mouse_x = 0;
static int mouse_y = 0;
static int last_mouse_buttons = 0;
static int mouse_is_dragging = 0;

#define MOUSE_X_R 820
#define MOUSE_Y_R 2621

static int old_x = 0;
static int old_y = 0;

static void mouse_event(int button, int x, int y) {
	char buf[7];
	sprintf(buf, "\033[M%c%c%c", button + 32, x + 33, y + 33);
	handle_input_s(buf);
}

static void redraw_mouse(void) {
	cell_redraw(old_x, old_y);
	cell_redraw_inverted(mouse_x, mouse_y);
	old_x = mouse_x;
	old_y = mouse_y;
}

static unsigned int button_state = 0;

void handle_mouse_event(mouse_device_packet_t * packet) {
	if (ansi_state->mouse_on) {
		/* TODO: Handle shift */
		if (packet->buttons & MOUSE_SCROLL_UP) {
			mouse_event(32+32, mouse_x, mouse_y);
		} else if (packet->buttons & MOUSE_SCROLL_DOWN) {
			mouse_event(32+32+1, mouse_x, mouse_y);
		}

		if (packet->buttons != button_state) {
			if (packet->buttons & LEFT_CLICK && !(button_state & LEFT_CLICK)) mouse_event(0, mouse_x, mouse_y);
			if (packet->buttons & MIDDLE_CLICK && !(button_state & MIDDLE_CLICK)) mouse_event(1, mouse_x, mouse_y);
			if (packet->buttons & RIGHT_CLICK && !(button_state & MIDDLE_CLICK)) mouse_event(2, mouse_x, mouse_y);
			if (!(packet->buttons & LEFT_CLICK) && (button_state & LEFT_CLICK)) mouse_event(3, mouse_x, mouse_y);
			if (!(packet->buttons & MIDDLE_CLICK) && (button_state & MIDDLE_CLICK)) mouse_event(3, mouse_x, mouse_y);
			if (!(packet->buttons & RIGHT_CLICK) && (button_state & MIDDLE_CLICK)) mouse_event(3, mouse_x, mouse_y);
			button_state = packet->buttons;
		} else if (ansi_state->mouse_on == 2) {
			if (old_x != mouse_x || old_y != mouse_y) {
				if (button_state & LEFT_CLICK) mouse_event(32, mouse_x, mouse_y);
				if (button_state & MIDDLE_CLICK) mouse_event(33, mouse_x, mouse_y);
				if (button_state & RIGHT_CLICK) mouse_event(34, mouse_x, mouse_y);
			}
		}

		redraw_mouse();
		return;
	}
	if (mouse_is_dragging) {
		if (packet->buttons & LEFT_CLICK) {
			int old_end_x = selection_end_x;
			int old_end_y = selection_end_y;
			selection_end_x = mouse_x;
			selection_end_y = mouse_y;
			redraw_new_selection(old_end_x, old_end_y);
		} else {
			mouse_is_dragging = 0;
		}
	} else {
		if (packet->buttons & LEFT_CLICK) {
			term_redraw_all();
			selection_start_x = mouse_x;
			selection_start_y = mouse_y;
			selection_end_x = mouse_x;
			selection_end_y = mouse_y;
			selection = 1;
			redraw_selection();
			mouse_is_dragging = 1;
		} else {
			redraw_mouse();
		}
	}


}

static int rel_mouse_x = 0;
static int rel_mouse_y = 0;

void handle_mouse(mouse_device_packet_t * packet) {
	rel_mouse_x += packet->x_difference;
	rel_mouse_y -= packet->y_difference;

	mouse_x = rel_mouse_x / 20;
	mouse_y = rel_mouse_y / 40;

	if (mouse_x < 0) mouse_x = 0;
	if (mouse_y < 0) mouse_y = 0;
	if (mouse_x >= term_width) mouse_x = term_width - 1;
	if (mouse_y >= term_height) mouse_y = term_height - 1;
	handle_mouse_event(packet);
}

void handle_mouse_abs(mouse_device_packet_t * packet) {
	mouse_x = packet->x_difference / MOUSE_X_R;
	mouse_y = packet->y_difference / MOUSE_Y_R;

	rel_mouse_x = mouse_x * 20;
	rel_mouse_y = mouse_y * 40;

	handle_mouse_event(packet);
}

static int input_stopped = 0;

void sig_suspend_input(int sig) {
	(void)sig;
	char exit_message[] = "[Input stopped]\n";
	write(fd_slave, exit_message, sizeof(exit_message));

	input_stopped = 1;

	signal(SIGUSR2, sig_suspend_input);
}

int main(int argc, char ** argv) {

	_login_shell = 0;

	static struct option long_opts[] = {
		{"login",      no_argument,       0, 'l'},
		{"help",       no_argument,       0, 'h'},
		{0,0,0,0}
	};

	/* Read some arguments */
	int index, c;
	while ((c = getopt_long(argc, argv, "hl", long_opts, &index)) != -1) {
		switch (c) {
			case 'l':
				_login_shell = 1;
				break;
			case 'h':
				usage(argv);
				return 0;
				break;
			case '?':
				break;
			default:
				break;
		}
	}

	putenv("TERM=toaru-vga");

	openpty(&fd_master, &fd_slave, NULL, NULL, NULL);

	terminal = fdopen(fd_slave, "w");

	struct winsize w;
	w.ws_row = term_height;
	w.ws_col = term_width;
	w.ws_xpixel = 0;
	w.ws_ypixel = 0;
	ioctl(fd_master, TIOCSWINSZ, &w);

	reinit();

	fflush(stdin);

	system("cursor-off"); /* Might GPF */

	signal(SIGUSR2, sig_suspend_input);

	int pid = getpid();
	uint32_t f = fork();

	if (getpid() != pid) {
		setsid();
		dup2(fd_slave, 0);
		dup2(fd_slave, 1);
		dup2(fd_slave, 2);

		if (argv[optind] != NULL) {
			char * tokens[] = {argv[optind], NULL};
			execvp(tokens[0], tokens);
			fprintf(stderr, "Failed to launch requested startup application.\n");
		} else {
			if (_login_shell) {
				char * tokens[] = {"/bin/login-loop",NULL};
				execvp(tokens[0], tokens);
				exit(1);
			} else {
				char * shell = getenv("SHELL");
				if (!shell) shell = "/bin/sh"; /* fallback */
				char * tokens[] = {shell,NULL};
				execvp(tokens[0], tokens);
				exit(1);
			}
		}

		exit_application = 1;

		return 1;
	} else {

		child_pid = f;

		int kfd = open("/dev/kbd", O_RDONLY);
		key_event_t event;
		char c;
		int vmmouse = 0;
		mouse_device_packet_t packet;

		int mfd = open("/dev/mouse", O_RDONLY);
		int amfd = open("/dev/absmouse", O_RDONLY);
		if (amfd == -1) {
			amfd = open("/dev/vmmouse", O_RDONLY);
			vmmouse = 1;
		}

		key_event_state_t kbd_state = {0};

		/* Prune any keyboard input we got before the terminal started. */
		struct stat s;
		fstat(kfd, &s);
		for (int i = 0; i < s.st_size; i++) {
			char tmp[1];
			read(kfd, tmp, 1);
		}

		int fds[] = {fd_master, kfd, mfd, amfd};

		unsigned char buf[1024];
		while (!exit_application) {

			int index = fswait2(amfd == -1 ? 3 : 4,fds,200);

			check_for_exit();

			if (input_stopped) continue;

			if (index == 0) {
				maybe_flip_cursor();
				int r = read(fd_master, buf, 1024);
				for (int i = 0; i < r; ++i) {
					ansi_put(ansi_state, buf[i]);
				}
			} else if (index == 1) {
				maybe_flip_cursor();
				int r = read(kfd, &c, 1);
				if (r > 0) {
					int ret = kbd_scancode(&kbd_state, c, &event);
					key_event(ret, &event);
				}
			} else if (index == 2) {
				/* mouse event */
				int r = read(mfd, (char *)&packet, sizeof(mouse_device_packet_t));
				if (r > 0) {
					last_mouse_buttons = packet.buttons;
					handle_mouse(&packet);
				}
			} else if (amfd != -1 && index == 3) {
				int r = read(amfd, (char *)&packet, sizeof(mouse_device_packet_t));
				if (r > 0) {
					if (!vmmouse) {
						packet.buttons = last_mouse_buttons & 0xF;
					} else {
						last_mouse_buttons = packet.buttons;
					}
					handle_mouse_abs(&packet);
				}
				continue;
				
			} else {
				maybe_flip_cursor();
			}
		}

	}

	return 0;
}
