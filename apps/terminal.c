/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2018 K. Lange
 *
 * Terminal Emulator
 *
 * Graphical terminal emulator.
 *
 * Provides a number of features:
 *  - Windowed and full screen modes
 *  - Antialiased fonts
 *  - Built-in fallback bitmap font
 *  - ANSI escape support
 *  - 256 colors
 */

#include <stdio.h>
#include <stdint.h>
#include <syscall.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <getopt.h>
#include <errno.h>

#include <wchar.h>

#define TRACE_APP_NAME "terminal"
#include <toaru/trace.h>
#include <toaru/utf8decode.h>
#include <toaru/yutani.h>
#include <toaru/decorations.h>
#include <toaru/graphics.h>
#include <toaru/kbd.h>
#include <toaru/termemu.h>
#include <toaru/spinlock.h>
#include <toaru/list.h>
#include <toaru/menu.h>
#include <toaru/menubar.h>
#include <toaru/sdf.h>

#include "terminal-palette.h"
#include "terminal-font.h"


#define USE_BELL 0

/* master and slave pty descriptors */
static int fd_master, fd_slave;
static FILE * terminal;

int      scale_fonts    = 0;    /* Whether fonts should be scaled */
float    font_scaling   = 1.0;  /* How much they should be scaled by */
float    font_gamma     = 1.7;  /* Gamma to use for SDF library */
uint16_t term_width     = 0;    /* Width of the terminal (in cells) */
uint16_t term_height    = 0;    /* Height of the terminal (in cells) */
uint16_t font_size      = 16;   /* Font size according to SDF library */
uint16_t char_width     = 9;   /* Width of a cell in pixels */
uint16_t char_height    = 17;   /* Height of a cell in pixels */
int      csr_x          = 0;    /* Cursor X */
int      csr_y          = 0;    /* Cursor Y */
term_cell_t * term_buffer    = NULL; /* The terminal cell buffer */
uint32_t current_fg     = 7;    /* Current foreground color */
uint32_t current_bg     = 0;    /* Current background color */
uint8_t  cursor_on      = 1;    /* Whether or not the cursor should be rendered */
uint8_t  _fullscreen    = 0;    /* Whether or not we are running in fullscreen mode (GUI only) */
uint8_t  _no_frame      = 0;    /* Whether to disable decorations or not */
uint8_t  _login_shell   = 0;    /* Whether we're going to display a login shell or not */
uint8_t  _use_sdf       = 1;    /* Whether or not to use SDF text rendering */
uint8_t  _force_kernel  = 0;
uint8_t  _hold_out      = 0;    /* state indicator on last cell ignore \n */
uint8_t  _free_size     = 1;    /* Disable rounding when resized */

int menu_bar_height = 24;

int selection = 0;
int selection_start_x = 0;
int selection_start_y = 0;
int selection_end_x = 0;
int selection_end_y = 0;
char * selection_text = NULL;

int      last_mouse_x   = -1;
int      last_mouse_y   = -1;
int      button_state   = 0;

uint64_t mouse_ticks = 0;

struct MenuList * menu_right_click = NULL;

yutani_window_t * window       = NULL; /* GUI window */
yutani_t * yctx = NULL;

term_state_t * ansi_state = NULL;

int32_t l_x = INT32_MAX;
int32_t l_y = INT32_MAX;
int32_t r_x = -1;
int32_t r_y = -1;

void reinit(); /* Defined way further down */
void term_redraw_cursor();

/* Some GUI-only options */
uint32_t window_width  = 640;
uint32_t window_height = 480;
#define TERMINAL_TITLE_SIZE 512
char   terminal_title[TERMINAL_TITLE_SIZE];
size_t terminal_title_length = 0;
gfx_context_t * ctx;
static void render_decors(void);
void term_clear();
void flush_unused_images(void);

void dump_buffer();

/* Trigger to exit the terminal when the child process dies or
 * we otherwise receive an exit signal */
volatile int exit_application = 0;

static void cell_redraw(uint16_t x, uint16_t y);
static void cell_redraw_inverted(uint16_t x, uint16_t y);

static uint64_t get_ticks(void) {
	struct timeval now;
	gettimeofday(&now, NULL);

	return (uint64_t)now.tv_sec * 1000000LL + (uint64_t)now.tv_usec;
}

static void display_flip(void) {
	if (l_x != INT32_MAX && l_y != INT32_MAX) {
		flip(ctx);
		yutani_flip_region(yctx, window, l_x, l_y, r_x - l_x, r_y - l_y);
		l_x = INT32_MAX;
		l_y = INT32_MAX;
		r_x = -1;
		r_y = -1;
	}
}

static void set_term_font_size(float s) {
	scale_fonts  = 1;
	font_scaling = s;
	reinit(1);
}

static void set_term_font_gamma(float s) {
	font_gamma = s;
	reinit(1);
}

static void set_term_font_mode(int i) {
	_use_sdf = i;
	reinit(1);
}

/* Returns the lower of two shorts */
int32_t min(int32_t a, int32_t b) {
	return (a < b) ? a : b;
}

/* Returns the higher of two shorts */
int32_t max(int32_t a, int32_t b) {
	return (a > b) ? a : b;
}

void set_title(char * c) {
	int len = min(TERMINAL_TITLE_SIZE, strlen(c)+1);
	memcpy(terminal_title, c, len);
	terminal_title[len-1] = '\0';
	terminal_title_length = len - 1;
	render_decors();
}

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

void unredraw_selection(void) {
	iterate_selection(cell_redraw);
}

void redraw_selection(void) {
	iterate_selection(cell_redraw_inverted);
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
	if (!(cell->flags & ANSI_EXT_IMG)) {
		if (((uint32_t *)cell)[0] != 0x00000000) {
			char tmp[7];
			_selection_count += to_eight(cell->c, tmp);
		}
	}
	if (x == term_width - 1) {
		_selection_count++;
	}
}

void write_selection(uint16_t x, uint16_t y) {
	term_cell_t * cell = (term_cell_t *)((uintptr_t)term_buffer + (y * term_width + x) * sizeof(term_cell_t));
	if (!(cell->flags & ANSI_EXT_IMG)) {
		if (((uint32_t *)cell)[0] != 0x00000000) {
			char tmp[7];
			int count = to_eight(cell->c, tmp);
			for (int i = 0; i < count; ++i) {
				selection_text[_selection_i] = tmp[i];
				_selection_i++;
			}
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

	fprintf(stderr, "Selection length is %d\n", _selection_count);

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

	yutani_set_clipboard(yctx, selection_text);

	return selection_text;
}

/* Stuffs a string into the stdin of the terminal's child process
 * Useful for things like the ANSI DSR command. */
void input_buffer_stuff(char * str) {
	size_t s = strlen(str) + 1;
	write(fd_master, str, s);
}

struct menu_bar terminal_menu_bar = {0};
struct menu_bar_entries terminal_menu_entries[] = {
	{"File", "file"},
	{"Edit", "edit"},
	{"View", "view"},
	{"Help", "help"},
	{NULL, NULL},
};


static void render_decors(void) {
	/* XXX Make the decorations library support Yutani windows */
	if (_fullscreen) return;
	if (!_no_frame) {
		render_decorations(window, ctx, terminal_title_length ? terminal_title : "Terminal");
		terminal_menu_bar.x = decor_left_width;
		terminal_menu_bar.y = decor_top_height;
		terminal_menu_bar.width = window_width;
		menu_bar_render(&terminal_menu_bar, ctx);
	}
	yutani_window_advertise_icon(yctx, window, terminal_title_length ? terminal_title : "Terminal", "utilities-terminal");
	l_x = 0; l_y = 0;
	r_x = window->width;
	r_y = window->height;
	display_flip();
}

static inline void term_set_point(uint16_t x, uint16_t y, uint32_t color ) {
	if (_fullscreen) {
		color = alpha_blend_rgba(premultiply(rgba(0,0,0,0xFF)), color);
	}
	if (!_no_frame) {
		GFX(ctx, (x+decor_left_width),(y+decor_top_height+menu_bar_height)) = color;
	} else {
		GFX(ctx, x,y) = color;
	}
}

void draw_semi_block(int c, int x, int y, uint32_t fg, uint32_t bg) {
	int height;
	bg = premultiply(bg);
	fg = premultiply(fg);
	if (c == 0x2580) {
		uint32_t t = bg;
		bg = fg;
		fg = t;
		c = 0x2584;
		for (uint8_t i = 0; i < char_height; ++i) {
			for (uint8_t j = 0; j < char_width; ++j) {
				term_set_point(x+j,y+i,bg);
			}
		}
	}
	c -= 0x2580;
	height = char_height - ((c * char_height) / 8);
	for (uint8_t i = height; i < char_height; ++i) {
		for (uint8_t j = 0; j < char_width; ++j) {
			term_set_point(x+j, y+i,fg);
		}
	}
}

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

	uint32_t _fg, _bg;

	if (fg < PALETTE_COLORS) {
		_fg = term_colors[fg];
		_fg |= 0xFF << 24;
	} else {
		_fg = fg;
	}
	if (bg < PALETTE_COLORS) {
		_bg = term_colors[bg];
		if (flags & ANSI_SPECBG) {
			_bg |= 0xFF << 24;
		} else {
			_bg |= TERM_DEFAULT_OPAC << 24;
		}
	} else {
		_bg = bg;
	}
	if (val >= 0x2580 && val <= 0x2588) {
		for (uint8_t i = 0; i < char_height; ++i) {
			for (uint8_t j = 0; j < char_width; ++j) {
				term_set_point(x+j,y+i,premultiply(_bg));
			}
		}
		draw_semi_block(val, x, y, _fg, _bg);
		goto _extra_stuff;
	}
	if (val > 128) {
		val = ununicode(val);
	}
	if (_use_sdf) {
		char tmp[2] = {val,0};
		for (uint8_t i = 0; i < char_height; ++i) {
			for (uint8_t j = 0; j < char_width; ++j) {
				term_set_point(x+j,y+i,_bg);
			}
		}
		if (val != 0 && val != ' ' && _fg != _bg) {
			int _font = SDF_FONT_MONO;
			if (flags & ANSI_BOLD && flags & ANSI_ITALIC) {
				_font = SDF_FONT_MONO_BOLD_OBLIQUE;
			} else if (flags & ANSI_BOLD) {
				_font = SDF_FONT_MONO_BOLD;
			} else if (flags & ANSI_ITALIC) {
				_font = SDF_FONT_MONO_OBLIQUE;
			}
			if (_no_frame) {
				draw_sdf_string_gamma(ctx, x-1, y, tmp, font_size, _fg, _font, font_gamma);
			} else {
				draw_sdf_string_gamma(ctx, x+decor_left_width-1, y+decor_top_height+menu_bar_height, tmp, font_size, _fg, _font, font_gamma);
			}
		}
	} else {
#ifdef number_font
		uint8_t * c = number_font[val];
		for (uint8_t i = 0; i < char_height; ++i) {
			for (uint8_t j = 0; j < char_width; ++j) {
				if (c[i] & (1 << (8-j))) {
					term_set_point(x+j,y+i,_fg);
				} else {
					term_set_point(x+j,y+i,_bg);
				}
			}
		}
#else
		uint16_t * c = large_font[val];
		for (uint8_t i = 0; i < char_height; ++i) {
			for (uint8_t j = 0; j < char_width; ++j) {
				if (c[i] & (1 << (15-j))) {
					term_set_point(x+j,y+i,_fg);
				} else {
					term_set_point(x+j,y+i,_bg);
				}
			}
		}
#endif
	}
_extra_stuff:
	if (flags & ANSI_UNDERLINE) {
		for (uint8_t i = 0; i < char_width; ++i) {
			term_set_point(x + i, y + char_height - 1, _fg);
		}
	}
	if (flags & ANSI_CROSS) {
		for (uint8_t i = 0; i < char_width; ++i) {
			term_set_point(x + i, y + char_height - 7, _fg);
		}
	}
	if (flags & ANSI_BORDER) {
		for (uint8_t i = 0; i < char_height; ++i) {
			term_set_point(x , y + i, _fg);
			term_set_point(x + (char_width - 1), y + i, _fg);
		}
		for (uint8_t j = 0; j < char_width; ++j) {
			term_set_point(x + j, y, _fg);
			term_set_point(x + j, y + (char_height - 1), _fg);
		}
	}

	if (!_no_frame) {
		l_x = min(l_x, decor_left_width + x);
		l_y = min(l_y, decor_top_height+menu_bar_height + y);

		if (flags & ANSI_WIDE) {
			r_x = max(r_x, decor_left_width + x + char_width * 2);
			r_y = max(r_y, decor_top_height+menu_bar_height + y + char_height * 2);
		} else {
			r_x = max(r_x, decor_left_width + x + char_width);
			r_y = max(r_y, decor_top_height+menu_bar_height + y + char_height);
		}
	} else {
		l_x = min(l_x, x);
		l_y = min(l_y, y);

		if (flags & ANSI_WIDE) {
			r_x = max(r_x, x + char_width * 2);
			r_y = max(r_y, y + char_height * 2);
		} else {
			r_x = max(r_x, x + char_width);
			r_y = max(r_y, y + char_height);
		}
	}
}

static void cell_set(uint16_t x, uint16_t y, uint32_t c, uint32_t fg, uint32_t bg, uint32_t flags) {
	if (x >= term_width || y >= term_height) return;
	term_cell_t * cell = (term_cell_t *)((uintptr_t)term_buffer + (y * term_width + x) * sizeof(term_cell_t));
	cell->c     = c;
	cell->fg    = fg;
	cell->bg    = bg;
	cell->flags = flags;
}

static void redraw_cell_image(uint16_t x, uint16_t y, term_cell_t * cell) {
	if (x >= term_width || y >= term_height) return;
	uint32_t * data = (uint32_t *)cell->fg;
	for (uint32_t yy = 0; yy < char_height; ++yy) {
		for (uint32_t xx = 0; xx < char_width; ++xx) {
			term_set_point(x * char_width + xx, y * char_height + yy, *data);
			data++;
		}
	}
	if (!_no_frame) {
		l_x = min(l_x, decor_left_width + x * char_width);
		l_y = min(l_y, decor_top_height+menu_bar_height + y * char_height);
		r_x = max(r_x, decor_left_width + x * char_width + char_width);
		r_y = max(r_y, decor_top_height+menu_bar_height + y * char_height + char_height);
	} else {
		l_x = min(l_x, x * char_width);
		l_y = min(l_y, y * char_height);
		r_x = max(r_x, x * char_width + char_width);
		r_y = max(r_y, y * char_height + char_height);
	}
}

static void cell_redraw(uint16_t x, uint16_t y) {
	if (x >= term_width || y >= term_height) return;
	term_cell_t * cell = (term_cell_t *)((uintptr_t)term_buffer + (y * term_width + x) * sizeof(term_cell_t));
	if (cell->flags & ANSI_EXT_IMG) { redraw_cell_image(x,y,cell); return; }
	if (((uint32_t *)cell)[0] == 0x00000000) {
		term_write_char(' ', x * char_width, y * char_height, TERM_DEFAULT_FG, TERM_DEFAULT_BG, TERM_DEFAULT_FLAGS);
	} else {
		term_write_char(cell->c, x * char_width, y * char_height, cell->fg, cell->bg, cell->flags);
	}
}

static void cell_redraw_inverted(uint16_t x, uint16_t y) {
	if (x >= term_width || y >= term_height) return;
	term_cell_t * cell = (term_cell_t *)((uintptr_t)term_buffer + (y * term_width + x) * sizeof(term_cell_t));
	if (cell->flags & ANSI_EXT_IMG) { redraw_cell_image(x,y,cell); return; }
	if (((uint32_t *)cell)[0] == 0x00000000) {
		term_write_char(' ', x * char_width, y * char_height, TERM_DEFAULT_BG, TERM_DEFAULT_FG, TERM_DEFAULT_FLAGS | ANSI_SPECBG);
	} else {
		term_write_char(cell->c, x * char_width, y * char_height, cell->bg, cell->fg, cell->flags | ANSI_SPECBG);
	}
}

static void cell_redraw_box(uint16_t x, uint16_t y) {
	if (x >= term_width || y >= term_height) return;
	term_cell_t * cell = (term_cell_t *)((uintptr_t)term_buffer + (y * term_width + x) * sizeof(term_cell_t));
	if (cell->flags & ANSI_EXT_IMG) { redraw_cell_image(x,y,cell); return; }
	if (((uint32_t *)cell)[0] == 0x00000000) {
		term_write_char(' ', x * char_width, y * char_height, TERM_DEFAULT_FG, TERM_DEFAULT_BG, TERM_DEFAULT_FLAGS | ANSI_BORDER);
	} else {
		term_write_char(cell->c, x * char_width, y * char_height, cell->fg, cell->bg, cell->flags | ANSI_BORDER);
	}
}

void render_cursor() {
	if (!window->focused) {
		cell_redraw_box(csr_x, csr_y);
	} else {
		cell_redraw_inverted(csr_x, csr_y);
	}
}

void draw_cursor() {
	if (!cursor_on) return;
	mouse_ticks = get_ticks();
	render_cursor();
}

void term_redraw_all() { 
	for (int i = 0; i < term_height; i++) {
		for (int x = 0; x < term_width; ++x) {
			term_cell_t * cell = (term_cell_t *)((uintptr_t)term_buffer + (i * term_width + x) * sizeof(term_cell_t));
			if (cell->flags & ANSI_EXT_IMG) { redraw_cell_image(x,i,cell); continue; }
			if (((uint32_t *)cell)[0] == 0x00000000) {
				term_write_char(' ', x * char_width, i * char_height, TERM_DEFAULT_FG, TERM_DEFAULT_BG, TERM_DEFAULT_FLAGS);
			} else {
				term_write_char(cell->c, x * char_width, i * char_height, cell->fg, cell->bg, cell->flags);
			}
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

	cell_redraw(csr_x, csr_y);
	if (how_much > 0) {
		/* Shift terminal cells one row up */
		memmove(term_buffer, (void *)((uintptr_t)term_buffer + sizeof(term_cell_t) * term_width), sizeof(term_cell_t) * term_width * (term_height - how_much));
		/* Reset the "new" row to clean cells */
		memset((void *)((uintptr_t)term_buffer + sizeof(term_cell_t) * term_width * (term_height - how_much)), 0x0, sizeof(term_cell_t) * term_width * how_much);
		/* In graphical modes, we will shift the graphics buffer up as necessary */
		uintptr_t dst, src;
		size_t    siz = char_height * (term_height - how_much) * GFX_W(ctx) * GFX_B(ctx);
		if (!_no_frame) {
			/* Must include decorations */
			dst = (uintptr_t)ctx->backbuffer + (GFX_W(ctx) * (decor_top_height+menu_bar_height)) * GFX_B(ctx);
			src = (uintptr_t)ctx->backbuffer + (GFX_W(ctx) * (decor_top_height+menu_bar_height + char_height * how_much)) * GFX_B(ctx);
		} else {
			/* Can skip decorations */
			dst = (uintptr_t)ctx->backbuffer;
			src = (uintptr_t)ctx->backbuffer + (GFX_W(ctx) *  char_height * how_much) * GFX_B(ctx);
		}
		/* Perform the shift */
		memmove((void *)dst, (void *)src, siz);
		/* And redraw the new rows */
		for (int i = 0; i < how_much; ++i) {
			for (uint16_t x = 0; x < term_width; ++x) {
				cell_set(x,term_height - how_much,' ', current_fg, current_bg, ansi_state->flags);
				cell_redraw(x, term_height - how_much);
			}
		}
	} else {
		how_much = -how_much;
		/* Shift terminal cells one row up */
		memmove((void *)((uintptr_t)term_buffer + sizeof(term_cell_t) * term_width), term_buffer, sizeof(term_cell_t) * term_width * (term_height - how_much));
		/* Reset the "new" row to clean cells */
		memset(term_buffer, 0x0, sizeof(term_cell_t) * term_width * how_much);
		uintptr_t dst, src;
		size_t    siz = char_height * (term_height - how_much) * GFX_W(ctx) * GFX_B(ctx);
		if (!_no_frame) {
			src = (uintptr_t)ctx->backbuffer + (GFX_W(ctx) * (decor_top_height+menu_bar_height)) * GFX_B(ctx);
			dst = (uintptr_t)ctx->backbuffer + (GFX_W(ctx) * (decor_top_height+menu_bar_height + char_height * how_much)) * GFX_B(ctx);
		} else {
			src = (uintptr_t)ctx->backbuffer;
			dst = (uintptr_t)ctx->backbuffer + (GFX_W(ctx) *  char_height * how_much) * GFX_B(ctx);
		}
		/* Perform the shift */
		memmove((void *)dst, (void *)src, siz);
		/* And redraw the new rows */
		for (int i = 0; i < how_much; ++i) {
			for (uint16_t x = 0; x < term_width; ++x) {
				cell_redraw(x, i);
			}
		}
	}
	flush_unused_images();
	yutani_flip(yctx, window);
}

int is_wide(uint32_t codepoint) {
	if (codepoint < 256) return 0;
	return wcwidth(codepoint) == 2;
}

struct scrollback_row {
	unsigned short width;
	term_cell_t cells[];
};

#define MAX_SCROLLBACK 10240

list_t * scrollback_list = NULL;

int scrollback_offset = 0;

void save_scrollback() {
	/* Save the current top row for scrollback */
	if (!scrollback_list) {
		scrollback_list = list_create();
	}
	if (scrollback_list->length == MAX_SCROLLBACK) {
		free(list_dequeue(scrollback_list));
	}

	struct scrollback_row * row = malloc(sizeof(struct scrollback_row) + sizeof(term_cell_t) * term_width + 20);
	row->width = term_width;
	for (int i = 0; i < term_width; ++i) {
		term_cell_t * cell = (term_cell_t *)((uintptr_t)term_buffer + (i) * sizeof(term_cell_t));
		memcpy(&row->cells[i], cell, sizeof(term_cell_t));
	}

	list_insert(scrollback_list, row);
}

void redraw_scrollback() {
	if (!scrollback_offset) {
		term_redraw_all();
		display_flip();
		return;
	}
	if (scrollback_offset < term_height) {
		for (int i = scrollback_offset; i < term_height; i++) {
			int y = i - scrollback_offset;
			for (int x = 0; x < term_width; ++x) {
				term_cell_t * cell = (term_cell_t *)((uintptr_t)term_buffer + (y * term_width + x) * sizeof(term_cell_t));
				if (cell->flags & ANSI_EXT_IMG) { redraw_cell_image(x,i,cell); continue; }
				if (((uint32_t *)cell)[0] == 0x00000000) {
					term_write_char(' ', x * char_width, i * char_height, TERM_DEFAULT_FG, TERM_DEFAULT_BG, TERM_DEFAULT_FLAGS);
				} else {
					term_write_char(cell->c, x * char_width, i * char_height, cell->fg, cell->bg, cell->flags);
				}
			}
		}

		node_t * node = scrollback_list->tail;
		for (int i = 0; i < scrollback_offset; ++i) {
			struct scrollback_row * row = (struct scrollback_row *)node->value;

			int y = scrollback_offset - 1 - i;
			int width = row->width;
			if (width > term_width) {
				width = term_width;
			} else {
				for (int x = row->width; x < term_width; ++x) {
					term_write_char(' ', x * char_width, y * char_height, TERM_DEFAULT_FG, TERM_DEFAULT_BG, TERM_DEFAULT_FLAGS);
				}
			}
			for (int x = 0; x < width; ++x) {
				term_cell_t * cell = &row->cells[x];
				if (((uint32_t *)cell)[0] == 0x00000000) {
					term_write_char(' ', x * char_width, y * char_height, TERM_DEFAULT_FG, TERM_DEFAULT_BG, TERM_DEFAULT_FLAGS);
				} else {
					term_write_char(cell->c, x * char_width, y * char_height, cell->fg, cell->bg, cell->flags);
				}
			}

			node = node->prev;
		}
	} else {
		node_t * node = scrollback_list->tail;
		for (int i = 0; i < scrollback_offset - term_height; ++i) {
			node = node->prev;
		}
		for (int i = scrollback_offset - term_height; i < scrollback_offset; ++i) {
			struct scrollback_row * row = (struct scrollback_row *)node->value;

			int y = scrollback_offset - 1 - i;
			int width = row->width;
			if (width > term_width) {
				width = term_width;
			} else {
				for (int x = row->width; x < term_width; ++x) {
					term_write_char(' ', x * char_width, y * char_height, TERM_DEFAULT_FG, TERM_DEFAULT_BG, TERM_DEFAULT_FLAGS);
				}
			}
			for (int x = 0; x < width; ++x) {
				term_cell_t * cell = &row->cells[x];
				if (((uint32_t *)cell)[0] == 0x00000000) {
					term_write_char(' ', x * char_width, y * char_height, TERM_DEFAULT_FG, TERM_DEFAULT_BG, TERM_DEFAULT_FLAGS);
				} else {
					term_write_char(cell->c, x * char_width, y * char_height, cell->fg, cell->bg, cell->flags);
				}
			}

			node = node->prev;
		}
	}
	display_flip();
}

void term_write(char c) {
	static uint32_t unicode_state = 0;
	static uint32_t codepoint = 0;

	cell_redraw(csr_x, csr_y);

	if (!decode(&unicode_state, &codepoint, (uint8_t)c)) {
		uint32_t o = codepoint;
		codepoint = 0;
		if (c == '\r') {
			csr_x = 0;
			return;
		}
		if (csr_x < 0) csr_x = 0;
		if (csr_y < 0) csr_y = 0;
		if (csr_x == term_width) {
			csr_x = 0;
			++csr_y;
		}
		if (csr_y == term_height) {
			save_scrollback();
			term_scroll(1);
			csr_y = term_height - 1;
		}
		if (c == '\n') {
			if (csr_x == 0 && _hold_out) {
				_hold_out = 0;
				return;
			}
			++csr_y;
			if (csr_y == term_height) {
				save_scrollback();
				term_scroll(1);
				csr_y = term_height - 1;
			}
			draw_cursor();
		} else if (c == '\007') {
			/* bell */
#if USE_BELL
			for (int i = 0; i < term_height; ++i) {
				for (int j = 0; j < term_width; ++j) {
					cell_redraw_inverted(j, i);
				}
			}
			syscall_nanosleep(0,10);
			term_redraw_all();
#endif
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
			int wide = is_wide(o);
			uint8_t flags = ansi_state->flags;
			if (wide && csr_x == term_width - 1) {
				csr_x = 0;
				++csr_y;
			}
			if (wide) {
				flags = flags | ANSI_WIDE;
			}
			cell_set(csr_x,csr_y, o, current_fg, current_bg, flags);
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
		codepoint = 0;
	}
	draw_cursor();
}

void term_set_csr(int x, int y) {
	cell_redraw(csr_x,csr_y);
	csr_x = x;
	csr_y = y;
	draw_cursor();
}

int term_get_csr_x(void) {
	return csr_x;
}

int term_get_csr_y(void) {
	return csr_y;
}

static list_t * images_list = NULL;

void term_set_cell_contents(int x, int y, char * data) {
	if (!images_list) {
		images_list = list_create();
	}
	char * cell_data = malloc(char_width * char_height * sizeof(uint32_t));
	memcpy(cell_data, data, char_width * char_height * sizeof(uint32_t));
	list_insert(images_list, cell_data);
	cell_set(x, y, ' ', (uint32_t)cell_data, 0, ANSI_EXT_IMG);
	return;
}

void flush_unused_images(void) {
	if (!images_list) return;

	list_t * tmp = list_create();
	for (int y = 0; y < term_height; ++y) {
		for (int x = 0; x < term_width; ++x) {
			term_cell_t * cell = (term_cell_t *)((uintptr_t)term_buffer + (y * term_width + x) * sizeof(term_cell_t));
			if (cell->flags & ANSI_EXT_IMG) {
				list_insert(tmp, (void *)cell->fg);
			}
		}
	}
	foreach(node, images_list) {
		if (!list_find(tmp, node->value)) {
			free(node->value);
		}
	}

	list_free(images_list);
	images_list = tmp;
}

int term_get_cell_width(void) {
	return char_width;
}

int term_get_cell_height(void) {
	return char_height;
}

void term_set_csr_show(int on) {
	cursor_on = on;
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
	static uint8_t cursor_flipped = 0;
	if (scrollback_offset != 0) {
		return; /* Don't flip cursor while drawing scrollback */
	}
	if (window->focused && cursor_flipped) {
		cell_redraw(csr_x, csr_y);
	} else {
		render_cursor();
	}
	display_flip();
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
		if (!_no_frame) {
			render_decors();
		}
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
	flush_unused_images();
}

#if 0
char * loadMemFont(char * name, char * ident, size_t * size) {
	size_t s = 0;
	int error;
	char tmp[100];
	snprintf(tmp, 100, "sys.%s.fonts.%s", yctx->server_ident, ident);

	char * font = (char *)syscall_shm_obtain(tmp, &s);
	*size = s;
	return font;
}
#endif

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
	display_flip();
}

void handle_input_s(char * c) {
	write(fd_master, c, strlen(c));
	display_flip();
}

void scroll_up(int amount) {
	int i = 0;
	while (i < amount && scrollback_list && scrollback_offset < (int)scrollback_list->length) {
		scrollback_offset ++;
		i++;
	}
	redraw_scrollback();
}

void scroll_down(int amount) {
	int i = 0;
	while (i < amount && scrollback_list && scrollback_offset != 0) {
		scrollback_offset -= 1;
		i++;
	}
	redraw_scrollback();
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
			yutani_special_request(yctx, NULL, YUTANI_SPECIAL_REQUEST_CLIPBOARD);
#if 0
			if (selection_text) {
				handle_input_s(selection_text);
			}
#endif
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
				/* Toggle decorations */
				if (!_fullscreen) {
					_no_frame = !_no_frame;
					window_width = window->width - decor_width() * (!_no_frame);
					window_height = window->height - (decor_height() + menu_bar_height) * (!_no_frame);
					reinit(1);
				}
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
				if (event->modifiers & KEY_MOD_LEFT_SHIFT) {
					scroll_up(term_height/2);
				} else {
					handle_input_s("\033[5~");
				}
				break;
			case KEY_PAGE_DOWN:
				if (event->modifiers & KEY_MOD_LEFT_SHIFT) {
					scroll_down(term_height/2);
				} else {
					handle_input_s("\033[6~");
				}
				break;
			case KEY_HOME:
				if (event->modifiers & KEY_MOD_LEFT_SHIFT) {
					if (scrollback_list) {
						scrollback_offset = scrollback_list->length;
						redraw_scrollback();
					}
				} else {
					handle_input_s("\033OH");
				}
				break;
			case KEY_END:
				if (event->modifiers & KEY_MOD_LEFT_SHIFT) {
					scrollback_offset = 0;
					redraw_scrollback();
				} else {
					handle_input_s("\033OF");
				}
				break;
			case KEY_DEL:
				handle_input_s("\033[3~");
				break;
			case KEY_INSERT:
				handle_input_s("\033[2~");
				break;
		}
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

void usage(char * argv[]) {
	printf(
			"Terminal Emulator\n"
			"\n"
			"usage: %s [-b] [-F] [-h]\n"
			"\n"
			" -F --fullscreen \033[3mRun in fullscreen (background) mode.\033[0m\n"
			" -b --bitmap     \033[3mUse the integrated bitmap font.\033[0m\n"
			" -s --scale      \033[3mScale the font in SDF mode by a given amount.\033[0m\n"
			" -h --help       \033[3mShow this help message.\033[0m\n"
			" -x --grid       \033[3mMake resizes round to nearest match for character cell size.\033[0m\n"
			" -n --no-frame   \033[3mDisable decorations.\033[0m\n"
			"\n"
			" This terminal emulator provides basic support for VT220 escapes and\n"
			" XTerm extensions, including 256 color support and font effects.\n",
			argv[0]);
}

term_callbacks_t term_callbacks = {
	&term_write,
	term_set_colors,
	term_set_csr,
	term_get_csr_x,
	term_get_csr_y,
	term_set_cell,
	term_clear,
	term_scroll,
	term_redraw_cursor,
	input_buffer_stuff,
	set_term_font_size,
	set_title,
	term_set_cell_contents,
	term_get_cell_width,
	term_get_cell_height,
	term_set_csr_show,
	set_term_font_gamma,
	set_term_font_mode,
};

void reinit(int send_sig) {
	if (_use_sdf) {
		char_width = 9;
		char_height = 17;
		font_size = 16;
		if (scale_fonts) {
			font_size   *= font_scaling;
			char_height *= font_scaling;
			char_width  *= font_scaling;
		}
	} else {
		char_width = 9;
		char_height = 20;
	}

	int old_width  = term_width;
	int old_height = term_height;

	term_width  = window_width  / char_width;
	term_height = window_height / char_height;
	if (term_buffer) {
		term_cell_t * new_term_buffer = malloc(sizeof(term_cell_t) * term_width * term_height);

		memset(new_term_buffer, 0x0, sizeof(term_cell_t) * term_width * term_height);

		int offset = 0;
		if (term_height < old_height) {
			while (csr_y >= term_height) {
				offset++;
				old_height--;
				csr_y--;
			}
		}
		for (int row = 0; row < min(old_height, term_height); ++row) {
			for (int col = 0; col < min(old_width, term_width); ++col) {
				term_cell_t * old_cell = (term_cell_t *)((uintptr_t)term_buffer + ((row + offset) * old_width + col) * sizeof(term_cell_t));
				term_cell_t * new_cell = (term_cell_t *)((uintptr_t)new_term_buffer + (row * term_width + col) * sizeof(term_cell_t));
				*new_cell = *old_cell;
			}
		}
		if (csr_x >= term_width) {
			csr_x = term_width-1;
		}
		free(term_buffer);

		term_buffer = new_term_buffer;
	} else {
		term_buffer = malloc(sizeof(term_cell_t) * term_width * term_height);
		memset(term_buffer, 0x0, sizeof(term_cell_t) * term_width * term_height);
	}

	int old_mouse_state = 0;
	if (ansi_state) old_mouse_state = ansi_state->mouse_on;
	ansi_state = ansi_init(ansi_state, term_width, term_height, &term_callbacks);
	ansi_state->mouse_on = old_mouse_state;

	draw_fill(ctx, rgba(0,0,0, TERM_DEFAULT_OPAC));
	render_decors();
	term_redraw_all();
	display_flip();

	struct winsize w;
	w.ws_row = term_height;
	w.ws_col = term_width;
	w.ws_xpixel = term_width * char_width;
	w.ws_ypixel = term_height * char_height;
	ioctl(fd_master, TIOCSWINSZ, &w);

	if (send_sig) {
		kill(child_pid, SIGWINCH);
	}
}

static void resize_finish(int width, int height) {
	static int resize_attempts = 0;

	int extra_x = 0;
	int extra_y = 0;

	if (!_no_frame) {
		extra_x = decor_width();
		extra_y = decor_height() + menu_bar_height;
	}

	int t_window_width  = width  - extra_x;
	int t_window_height = height - extra_y;

	if (t_window_width < char_width * 20 || t_window_height < char_height * 10) {
		resize_attempts++;
		int n_width  = extra_x + max(char_width * 20, t_window_width);
		int n_height = extra_y + max(char_height * 10, t_window_height);
		yutani_window_resize_offer(yctx, window, n_width, n_height);
		return;
	}

	if (!_free_size && ((t_window_width % char_width != 0 || t_window_height % char_height != 0) && resize_attempts < 3)) {
		resize_attempts++;
		int n_width  = extra_x + t_window_width  - (t_window_width  % char_width);
		int n_height = extra_y + t_window_height - (t_window_height % char_height);
		yutani_window_resize_offer(yctx, window, n_width, n_height);
		return;
	}

	resize_attempts = 0;

	yutani_window_resize_accept(yctx, window, width, height);
	window_width  = window->width  - extra_x;
	window_height = window->height - extra_y;

	reinit_graphics_yutani(ctx, window);
	reinit(1);

	yutani_window_resize_done(yctx, window);
	yutani_flip(yctx, window);
}

void mouse_event(int button, int x, int y) {
	char buf[7];
	sprintf(buf, "\033[M%c%c%c", button + 32, x + 33, y + 33);
	handle_input_s(buf);
}

void * handle_incoming(void) {

	yutani_msg_t * m = yutani_poll(yctx);
	while (m) {
		menu_process_event(yctx, m);
		switch (m->type) {
			case YUTANI_MSG_KEY_EVENT:
				{
					struct yutani_msg_key_event * ke = (void*)m->data;
					int ret = (ke->event.action == KEY_ACTION_DOWN) && (ke->event.key);
					key_event(ret, &ke->event);
				}
				break;
			case YUTANI_MSG_WINDOW_FOCUS_CHANGE:
				{
					struct yutani_msg_window_focus_change * wf = (void*)m->data;
					yutani_window_t * win = hashmap_get(yctx->windows, (void*)wf->wid);
					if (win) {
						win->focused = wf->focused;
						render_decors();
					}
				}
				break;
			case YUTANI_MSG_WINDOW_CLOSE:
				{
					struct yutani_msg_window_close * wc = (void*)m->data;
					if (wc->wid == window->wid) {
						kill(child_pid, SIGKILL);
						exit_application = 1;
					}
				}
				break;
			case YUTANI_MSG_SESSION_END:
				{
					kill(child_pid, SIGKILL);
					exit_application = 1;
				}
				break;
			case YUTANI_MSG_RESIZE_OFFER:
				{
					struct yutani_msg_window_resize * wr = (void*)m->data;
					resize_finish(wr->width, wr->height);
				}
				break;
			case YUTANI_MSG_CLIPBOARD:
				{
					struct yutani_msg_clipboard * cb = (void *)m->data;
					if (selection_text) {
						free(selection_text);
					}
					if (*cb->content == '\002') {
						int size = atoi(&cb->content[2]);
						FILE * clipboard = yutani_open_clipboard(yctx);
						selection_text = malloc(size + 1);
						fread(selection_text, 1, size, clipboard);
						selection_text[size] = '\0';
						fclose(clipboard);
					} else {
						selection_text = malloc(cb->size+1);
						memcpy(selection_text, cb->content, cb->size);
						selection_text[cb->size] = '\0';
					}
					handle_input_s(selection_text);
				}
				break;
			case YUTANI_MSG_WINDOW_MOUSE_EVENT:
				{
					struct yutani_msg_window_mouse_event * me = (void*)m->data;
					if (me->wid != window->wid) break;
					if (!_no_frame) {
						int decor_response = decor_handle_event(yctx, m);

						switch (decor_response) {
							case DECOR_CLOSE:
								kill(child_pid, SIGKILL);
								exit_application = 1;
								break;
							case DECOR_RIGHT:
								/* right click in decoration, show appropriate menu */
								decor_show_default_menu(window, window->x + me->new_x, window->y + me->new_y);
								break;
							default:
								break;
						}

						menu_bar_mouse_event(yctx, window, &terminal_menu_bar, me, me->new_x, me->new_y);
					}

					if (!_no_frame) {
						if (me->new_x < 0 || me->new_x >= (int)window_width + (int)decor_width() || me->new_y < 0 || me->new_y >= (int)window_height + (int)decor_height()) {
							break;
						}
						if (me->new_y < (int)decor_top_height+menu_bar_height || me->new_y >= (int)(window_height + decor_top_height+menu_bar_height)) {
							break;
						}
						if (me->new_x < (int)decor_left_width || me->new_y >= (int)(window_width + decor_left_width)) {
							break;
						}
					} else {
						if (me->new_x < 0 || me->new_x >= (int)window_width || me->new_y < 0 || me->new_y >= (int)window_height) {
							break;
						}
					}

					int new_x = me->new_x;
					int new_y = me->new_y;
					if (!_no_frame) {
						new_x -= decor_left_width;
						new_y -= decor_top_height+menu_bar_height;
					}
					/* Convert from coordinate to cell positon */
					new_x /= char_width;
					new_y /= char_height;

					if (new_x < 0 || new_y < 0) break;
					if (new_x > term_width || new_y > term_height) break;

					/* Map Cursor Action */
					if (ansi_state->mouse_on) {

						if (me->buttons & YUTANI_MOUSE_SCROLL_UP) {
							mouse_event(32+32, new_x, new_y);
						} else if (me->buttons & YUTANI_MOUSE_SCROLL_DOWN) {
							mouse_event(32+32+1, new_x, new_y);
						}

						if (me->buttons != button_state) {
							/* Figure out what changed */
							if (me->buttons & YUTANI_MOUSE_BUTTON_LEFT && !(button_state & YUTANI_MOUSE_BUTTON_LEFT)) mouse_event(0, new_x, new_y);
							if (me->buttons & YUTANI_MOUSE_BUTTON_MIDDLE && !(button_state & YUTANI_MOUSE_BUTTON_MIDDLE)) mouse_event(1, new_x, new_y);
							if (me->buttons & YUTANI_MOUSE_BUTTON_RIGHT && !(button_state & YUTANI_MOUSE_BUTTON_RIGHT)) mouse_event(2, new_x, new_y);
							if (!(me->buttons & YUTANI_MOUSE_BUTTON_LEFT) && button_state & YUTANI_MOUSE_BUTTON_LEFT) mouse_event(3, new_x, new_y);
							if (!(me->buttons & YUTANI_MOUSE_BUTTON_MIDDLE) && button_state & YUTANI_MOUSE_BUTTON_MIDDLE) mouse_event(3, new_x, new_y);
							if (!(me->buttons & YUTANI_MOUSE_BUTTON_RIGHT) && button_state & YUTANI_MOUSE_BUTTON_RIGHT) mouse_event(3, new_x, new_y);
							last_mouse_x = new_x;
							last_mouse_y = new_y;
							button_state = me->buttons;
						} else if (ansi_state->mouse_on == 2) {
							/* Report motion for pressed buttons */
							if (last_mouse_x == new_x && last_mouse_y == new_y) break;
							if (button_state & YUTANI_MOUSE_BUTTON_LEFT) mouse_event(32, new_x, new_y);
							if (button_state & YUTANI_MOUSE_BUTTON_MIDDLE) mouse_event(33, new_x, new_y);
							if (button_state & YUTANI_MOUSE_BUTTON_RIGHT) mouse_event(34, new_x, new_y);
							last_mouse_x = new_x;
							last_mouse_y = new_y;
						}
					} else {
						if (me->command == YUTANI_MOUSE_EVENT_DOWN && me->buttons & YUTANI_MOUSE_BUTTON_LEFT) {
							term_redraw_all();
							selection_start_x = new_x;
							selection_start_y = new_y;
							selection_end_x = new_x;
							selection_end_y = new_y;
							selection = 1;
							redraw_selection();
							display_flip();
						}
						if (me->command == YUTANI_MOUSE_EVENT_DRAG && me->buttons & YUTANI_MOUSE_BUTTON_LEFT ){
							unredraw_selection();
							selection_end_x = new_x;
							selection_end_y = new_y;
							redraw_selection();
							display_flip();
						}
						if (me->command == YUTANI_MOUSE_EVENT_RAISE) {
							if (me->new_x == me->old_x && me->new_y == me->old_y) {
								selection = 0;
								term_redraw_all();
								display_flip();
							} /* else selection */
						}
						if (me->buttons & YUTANI_MOUSE_SCROLL_UP) {
							scroll_up(5);
						} else if (me->buttons & YUTANI_MOUSE_SCROLL_DOWN) {
							scroll_down(5);
						} else if (me->buttons & YUTANI_MOUSE_BUTTON_RIGHT) {
							if (!menu_right_click->window) {
								menu_show(menu_right_click, yctx);
								yutani_window_move(yctx, menu_right_click->window, window->x + me->new_x, window->y + me->new_y);
							}
						}
					}
				}
				break;
			default:
				break;
		}
		free(m);
		m = yutani_poll_async(yctx);
	}

	return NULL;
}

void _menu_action_exit(struct MenuEntry * self) {
	kill(child_pid, SIGKILL);
	exit_application = 1;
}

void _menu_action_hide_borders(struct MenuEntry * self) {
	_no_frame = !(_no_frame);
	window_width = window->width - decor_width() * (!_no_frame);
	window_height = window->height - (decor_height() + menu_bar_height) * (!_no_frame);
	reinit(1);
}

void _menu_action_copy(struct MenuEntry * self) {
	copy_selection();
}

void _menu_action_paste(struct MenuEntry * self) {
	yutani_special_request(yctx, NULL, YUTANI_SPECIAL_REQUEST_CLIPBOARD);
}

void maybe_flip_cursor(void) {
	uint64_t ticks = get_ticks();
	if (ticks > mouse_ticks + 600000LL) {
		mouse_ticks = ticks;
		flip_cursor();
	}
}

int main(int argc, char ** argv) {

	_login_shell = 0;
	_fullscreen = 0;
	_no_frame = 0;

	window_width  = char_width * 80;
	window_height = char_height * 24;

	static struct option long_opts[] = {
		{"fullscreen", no_argument,       0, 'F'},
		{"bitmap",     no_argument,       0, 'b'},
		{"scale",      required_argument, 0, 's'},
		{"login",      no_argument,       0, 'l'},
		{"help",       no_argument,       0, 'h'},
		{"kernel",     no_argument,       0, 'k'},
		{"grid",       no_argument,       0, 'x'},
		{"no-frame",   no_argument,       0, 'n'},
		{"geometry",   required_argument, 0, 'g'},
		{0,0,0,0}
	};

	/* Read some arguments */
	int index, c;
	while ((c = getopt_long(argc, argv, "bhxnFlks:g:", long_opts, &index)) != -1) {
		if (!c) {
			if (long_opts[index].flag == 0) {
				c = long_opts[index].val;
			}
		}
		switch (c) {
			case 'k':
				_force_kernel = 1;
				break;
			case 'x':
				_free_size = 0;
				break;
			case 'l':
				_login_shell = 1;
				break;
			case 'n':
				_no_frame = 1;
				break;
			case 'F':
				_fullscreen = 1;
				_no_frame = 1;
				break;
			case 'b':
				_use_sdf = 0;
				break;
			case 'h':
				usage(argv);
				return 0;
				break;
			case 's':
				scale_fonts = 1;
				font_scaling = atof(optarg);
				break;
			case 'g':
				{
					char * c = strstr(optarg, "x");
					if (c) {
						*c = '\0';
						c++;
						window_width  = atoi(optarg);
						window_height = atoi(c);
					}
				}
				break;
			case '?':
				break;
			default:
				break;
		}
	}

	// XXX
	putenv("TERM=toaru");

	/* Initialize the windowing library */
	yctx = yutani_init();

	if (_fullscreen) {
		window_width = yctx->display_width;
		window_height = yctx->display_height;
	}

	if (_no_frame) {
		window = yutani_window_create(yctx, window_width, window_height);
	} else {
		init_decorations();
		window = yutani_window_create(yctx, window_width + decor_left_width + decor_right_width, window_height + decor_top_height+menu_bar_height + decor_bottom_height);

	}

	if (_fullscreen) {
		yutani_set_stack(yctx, window, YUTANI_ZORDER_BOTTOM);
		window->focused = 1;
	} else {
		window->focused = 0;
	}

	/* Set up menus */
	terminal_menu_bar.entries = terminal_menu_entries;
	terminal_menu_bar.redraw_callback = render_decors;

	struct MenuEntry * _menu_exit = menu_create_normal("exit","exit","Exit", _menu_action_exit);
	struct MenuEntry * _menu_copy = menu_create_normal(NULL, NULL, "Copy", _menu_action_copy);
	struct MenuEntry * _menu_paste = menu_create_normal(NULL, NULL, "Paste", _menu_action_paste);

	menu_right_click = menu_create();
	menu_insert(menu_right_click, _menu_copy);
	menu_insert(menu_right_click, _menu_paste);
	menu_insert(menu_right_click, menu_create_separator());
	menu_insert(menu_right_click, menu_create_normal(NULL, NULL, "Toggle borders", _menu_action_hide_borders));
	menu_insert(menu_right_click, menu_create_separator());
	menu_insert(menu_right_click, _menu_exit);

	/* Menu Bar menus */
	terminal_menu_bar.set = menu_set_create();
	struct MenuList * m;
	m = menu_create(); /* File */
	menu_insert(m, _menu_exit);
	menu_set_insert(terminal_menu_bar.set, "file", m);

	m = menu_create();
	menu_insert(m, _menu_copy);
	menu_insert(m, _menu_paste);
	menu_set_insert(terminal_menu_bar.set, "edit", m);

	m = menu_create();
	menu_insert(m, menu_create_normal(NULL, NULL, "Hide borders", _menu_action_hide_borders));
	menu_set_insert(terminal_menu_bar.set, "view", m);

	m = menu_create();
	menu_insert(m, menu_create_normal("star","star","About Terminal", NULL));
	menu_set_insert(terminal_menu_bar.set, "help", m);



	/* Initialize the graphics context */
	ctx = init_graphics_yutani_double_buffer(window);

	/* Clear to black */
	draw_fill(ctx, rgba(0,0,0,0));

	yutani_window_move(yctx, window, yctx->display_width / 2 - window->width / 2, yctx->display_height / 2 - window->height / 2);

	syscall_openpty(&fd_master, &fd_slave, NULL, NULL, NULL);

	terminal = fdopen(fd_slave, "w");

	reinit(0);

	fflush(stdin);

	pid_t pid = getpid();
	pid_t f = fork();

	if (getpid() != pid) {
		dup2(fd_slave, 0);
		dup2(fd_slave, 1);
		dup2(fd_slave, 2);

		if (argv[optind] != NULL) {
			char * tokens[] = {argv[optind], NULL};
			execvp(tokens[0], tokens);
			fprintf(stderr, "Failed to launch requested startup application.\n");
		} else {
			if (_login_shell) {
				char * tokens[] = {"/bin/login",NULL};
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

		if (_force_kernel) {
			/* Request kernel output to this terminal */
			//syscall_system_function(4, (char **)fd_slave);
		}

		child_pid = f;

		int fds[2] = {fileno(yctx->sock), fd_master};

		unsigned char buf[1024];
		while (!exit_application) {

			int index = syscall_fswait2(2,fds,200);

			check_for_exit();

			if (index == 1) {
				maybe_flip_cursor();
				int r = read(fd_master, buf, 1024);
				for (int i = 0; i < r; ++i) {
					ansi_put(ansi_state, buf[i]);
				}
				display_flip();
			} else if (index == 0) {
				maybe_flip_cursor();
				handle_incoming();
			} else if (index == 2) {
				maybe_flip_cursor();
			}
		}

	}

	//yutani_close(yctx, window);

	return 0;
}
