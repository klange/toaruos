/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2014 Kevin Lange
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
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <getopt.h>
#include <errno.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_CACHE_H

#include <wchar.h>

#include "lib/utf8decode.h"

#include "lib/graphics.h"
#include "lib/yutani.h"
#include "lib/decorations.h"
#include "lib/pthread.h"
#include "lib/kbd.h"
#include "lib/spinlock.h"

#include "terminal-palette.h"
#include "terminal-font.h"

#include "gui/terminal/lib/termemu.h"

#define USE_BELL 0

/* master and slave pty descriptors */
static int fd_master, fd_slave;
static FILE * terminal;

int      scale_fonts    = 0;    /* Whether fonts should be scaled */
float    font_scaling   = 1.0;  /* How much they should be scaled by */
uint16_t term_width     = 0;    /* Width of the terminal (in cells) */
uint16_t term_height    = 0;    /* Height of the terminal (in cells) */
uint16_t font_size      = 13;   /* Font size according to Freetype */
uint16_t char_width     = 8;    /* Width of a cell in pixels */
uint16_t char_height    = 12;   /* Height of a cell in pixels */
uint16_t char_offset    = 0;    /* Offset of the font within the cell */
uint16_t csr_x          = 0;    /* Cursor X */
uint16_t csr_y          = 0;    /* Cursor Y */
term_cell_t * term_buffer    = NULL; /* The terminal cell buffer */
uint32_t current_fg     = 7;    /* Current foreground color */
uint32_t current_bg     = 0;    /* Current background color */
uint8_t  cursor_on      = 1;    /* Whether or not the cursor should be rendered */
uint8_t  _fullscreen    = 0;    /* Whether or not we are running in fullscreen mode (GUI only) */
uint8_t  _no_frame      = 0;    /* Whether to disable decorations or not */
uint8_t  _login_shell   = 0;    /* Whether we're going to display a login shell or not */
uint8_t  _use_freetype  = 0;    /* Whether we should use freetype or not XXX seriously, how about some flags */
uint8_t  _force_kernel  = 0;
uint8_t  _hold_out      = 0;    /* state indicator on last cell ignore \n */
uint8_t  _free_size     = 1;    /* Disable rounding when resized */

int      last_mouse_x   = -1;
int      last_mouse_y   = -1;
int      button_state   = 0;

static volatile int display_lock = 0;

yutani_window_t * window       = NULL; /* GUI window */
yutani_t * yctx = NULL;

term_state_t * ansi_state = NULL;

int32_t l_x = INT32_MAX;
int32_t l_y = INT32_MAX;
int32_t r_x = -1;
int32_t r_y = -1;

void reinit(); /* Defined way further down */
void term_redraw_cursor();

/* Cursor bink timer */
static unsigned int timer_tick = 0;

/* Some GUI-only options */
uint32_t window_width  = 640;
uint32_t window_height = 408;
#define TERMINAL_TITLE_SIZE 512
char   terminal_title[TERMINAL_TITLE_SIZE];
size_t terminal_title_length = 0;
gfx_context_t * ctx;
static void render_decors();
void term_clear();

void dump_buffer();

/* Trigger to exit the terminal when the child process dies or
 * we otherwise receive an exit signal */
volatile int exit_application = 0;

static void display_flip(void) {
	if (l_x != INT32_MAX && l_y != INT32_MAX) {
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

/* Stuffs a string into the stdin of the terminal's child process
 * Useful for things like the ANSI DSR command. */
void input_buffer_stuff(char * str) {
	size_t s = strlen(str) + 1;
	write(fd_master, str, s);
}

static void render_decors() {
	/* XXX Make the decorations library support Yutani windows */
	if (_fullscreen) return;
	if (!_no_frame) {
		render_decorations(window, ctx, terminal_title_length ? terminal_title : "Terminal");
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
		GFX(ctx, (x+decor_left_width),(y+decor_top_height)) = color;
	} else {
		GFX(ctx, x,y) = color;
	}
}

/* FreeType text rendering */

FT_Library   library;
FT_Face      face;
FT_Face      face_bold;
FT_Face      face_italic;
FT_Face      face_bold_italic;
FT_Face      face_extra;
FT_Face      face_symbol;
FT_Face      face_variable;

FT_Face * fallbacks[] = {&face_variable, &face_symbol, &face_extra, &face_symbol, NULL};


void drawChar(FT_Bitmap * bitmap, int x, int y, uint32_t fg, uint32_t bg) {
	int i, j, p, q;
	int x_max = x + bitmap->width;
	int y_max = y + bitmap->rows;
	for (j = y, q = 0; j < y_max; j++, q++) {
		for ( i = x, p = 0; i < x_max; i++, p++) {
			uint32_t a = _ALP(fg);
			a = (a * bitmap->buffer[q * bitmap->width + p]) / 255;
			uint32_t tmp = rgba(_RED(fg),_GRE(fg),_BLU(fg),a);
			term_set_point(i,j, alpha_blend_rgba(premultiply(bg), premultiply(tmp)));
		}
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

void focus_callback(yutani_window_t * yutani_window) {
	render_decors();
	term_redraw_cursor();
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
	if (_use_freetype) {
		if (val == 0xFFFF) { return; } /* Unicode, do not redraw here */
		for (uint8_t i = 0; i < char_height; ++i) {
			for (uint8_t j = 0; j < char_width; ++j) {
				term_set_point(x+j,y+i,premultiply(_bg));
			}
		}
		if (flags & ANSI_WIDE) {
			for (uint8_t i = 0; i < char_height; ++i) {
				for (uint8_t j = char_width; j < 2 * char_width; ++j) {
					term_set_point(x+j,y+i,premultiply(_bg));
				}
			}
		}
		if (val < 32 || val == ' ') {
			goto _extra_stuff;
		}
		if (val >= 0x2580 && val <= 0x2588) {
			draw_semi_block(val, x, y, _fg, _bg);
			goto _extra_stuff;
		}

		int pen_x = x;
		int pen_y = y + char_offset;
		int error;

		FT_Face * _font = NULL;
		FT_GlyphSlot slot;
		FT_UInt      glyph_index;

		if (flags & ANSI_ALTFONT) {
			_font = &face_extra;
		} else if (flags & ANSI_BOLD && flags & ANSI_ITALIC) {
			_font = &face_bold_italic;
		} else if (flags & ANSI_ITALIC) {
			_font = &face_italic;
		} else if (flags & ANSI_BOLD) {
			_font = &face_bold;
		} else {
			_font = &face;
		}
		glyph_index = FT_Get_Char_Index(*_font, val);

		if (!glyph_index) {
			int i = 0;
			while (!glyph_index && fallbacks[i]) {
				_font = fallbacks[i];
				glyph_index = FT_Get_Char_Index(*_font, val);
				i++;
			}
		}
		error = FT_Load_Glyph(*_font, glyph_index,  FT_LOAD_DEFAULT);
		if (error) {
			fprintf(terminal, "Error loading glyph: %d\n", val);
			fprintf(stderr, "Error loading glyph: %d\n", val);
		};
		slot = (*_font)->glyph;
		if (slot->format == FT_GLYPH_FORMAT_OUTLINE) {
			error = FT_Render_Glyph((*_font)->glyph, FT_RENDER_MODE_NORMAL);
			if (error) {
				fprintf(stderr, "Error rendering glyph: %d\n", val);
				goto _extra_stuff;
			}
		}
		drawChar(&slot->bitmap, pen_x + slot->bitmap_left, pen_y - slot->bitmap_top, _fg, _bg);

	} else {
		if (val > 128) {
			val = 4;
		}
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
	}
_extra_stuff:
	if (flags & ANSI_UNDERLINE) {
		for (uint8_t i = 0; i < char_width; ++i) {
			term_set_point(x + i, y + char_offset + 2, _fg);
		}
	}
	if (flags & ANSI_CROSS) {
		for (uint8_t i = 0; i < char_width; ++i) {
			term_set_point(x + i, y + char_offset - 5, _fg);
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
		l_y = min(l_y, decor_top_height + y);

		if (flags & ANSI_WIDE) {
			r_x = max(r_x, decor_left_width + x + char_width * 2);
			r_y = max(r_y, decor_top_height + y + char_height * 2);
		} else {
			r_x = max(r_x, decor_left_width + x + char_width);
			r_y = max(r_y, decor_top_height + y + char_height);
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

static void cell_redraw_box(uint16_t x, uint16_t y) {
	if (x >= term_width || y >= term_height) return;
	term_cell_t * cell = (term_cell_t *)((uintptr_t)term_buffer + (y * term_width + x) * sizeof(term_cell_t));
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
	timer_tick = 0;
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
		/* In graphical modes, we will shift the graphics buffer up as necessary */
		uintptr_t dst, src;
		size_t    siz = char_height * (term_height - how_much) * GFX_W(ctx) * GFX_B(ctx);
		if (!_no_frame) {
			/* Must include decorations */
			dst = (uintptr_t)ctx->backbuffer + (GFX_W(ctx) * decor_top_height) * GFX_B(ctx);
			src = (uintptr_t)ctx->backbuffer + (GFX_W(ctx) * (decor_top_height + char_height * how_much)) * GFX_B(ctx);
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
			src = (uintptr_t)ctx->backbuffer + (GFX_W(ctx) * decor_top_height) * GFX_B(ctx);
			dst = (uintptr_t)ctx->backbuffer + (GFX_W(ctx) * (decor_top_height + char_height * how_much)) * GFX_B(ctx);
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

uint32_t scrollback_offset = 0;

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
		return;
	}
	if (scrollback_offset < term_height) {
		for (int i = scrollback_offset; i < term_height; i++) {
			int y = i - scrollback_offset;
			for (int x = 0; x < term_width; ++x) {
				term_cell_t * cell = (term_cell_t *)((uintptr_t)term_buffer + (y * term_width + x) * sizeof(term_cell_t));
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

void
term_set_csr(int x, int y) {
	cell_redraw(csr_x,csr_y);
	csr_x = x;
	csr_y = y;
	draw_cursor();
}

int
term_get_csr_x() {
	return csr_x;
}

int
term_get_csr_y() {
	return csr_y;
}

void
term_set_csr_show(uint8_t on) {
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
}

char * loadMemFont(char * name, char * ident, size_t * size) {
	size_t s = 0;
	int error;
	char tmp[100];
	snprintf(tmp, 100, "sys.%s.fonts.%s", yctx->server_ident, ident);

	char * font = (char *)syscall_shm_obtain(tmp, &s);
	*size = s;
	return font;
}

#define INPUT_SIZE 1024
char input_buffer[INPUT_SIZE];
int  input_collected = 0;

void clear_input() {
	memset(input_buffer, 0x0, INPUT_SIZE);
	input_collected = 0;
}

uint32_t child_pid = 0;

void handle_input(char c) {
	spin_lock(&display_lock);
	write(fd_master, &c, 1);
	display_flip();
	spin_unlock(&display_lock);
}

void handle_input_s(char * c) {
	spin_lock(&display_lock);
	write(fd_master, c, strlen(c));
	display_flip();
	spin_unlock(&display_lock);
}

void key_event(int ret, key_event_t * event) {
	if (ret) {
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
					spin_lock(&display_lock);
					_no_frame = !_no_frame;
					window_width = window->width - decor_width() * (!_no_frame);
					window_height = window->height - decor_height() * (!_no_frame);
					reinit(1);
					spin_unlock(&display_lock);
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
					int i = 0;
					while (i < 5 && scrollback_list && scrollback_offset < scrollback_list->length) {
						scrollback_offset ++;
						i++;
					}
					redraw_scrollback();
				} else {
					handle_input_s("\033[5~");
				}
				break;
			case KEY_PAGE_DOWN:
				if (event->modifiers & KEY_MOD_LEFT_SHIFT) {
					int i = 0;
					while (i < 5 && scrollback_list && scrollback_offset != 0) {
						scrollback_offset -= 1;
						i++;
					}
					redraw_scrollback();
				} else {
					handle_input_s("\033[6~");
				}
				break;
			case KEY_HOME:
				handle_input_s("\033OH");
				break;
			case KEY_END:
				handle_input_s("\033OF");
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

void * wait_for_exit(void * garbage) {
	int pid;
	do {
		pid = waitpid(-1, NULL, 0);
	} while (pid == -1 && errno == EINTR);
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
			" -h --help       \033[3mShow this help message.\033[0m\n"
			" -s --scale      \033[3mScale the font in FreeType mode by a given amount.\033[0m\n"
			" -x --grid       \033[3mMake resizes round to nearest match for character cell size.\033[0m\n"
			" -n --no-frame   \033[3mDisable decorations.\033[0m\n"
			"\n"
			" This terminal emulator provides basic support for VT220 escapes and\n"
			" XTerm extensions, including 256 color support and font effects.\n",
			argv[0]);
}

term_callbacks_t term_callbacks = {
	/* writer*/
	&term_write,
	/* set_color*/
	term_set_colors,
	/* set_csr*/
	term_set_csr,
	/* get_csr_x*/
	term_get_csr_x,
	/* get_csr_y*/
	term_get_csr_y,
	/* set_cell*/
	term_set_cell,
	/* cls*/
	term_clear,
	/* scroll*/
	term_scroll,
	/* redraw_cursor*/
	term_redraw_cursor,
	/* input_buffer_stuff*/
	input_buffer_stuff,
	/* set_font_size*/
	set_term_font_size,
	/* set_title*/
	set_title,
};

void reinit(int send_sig) {
	if (_use_freetype) {
		/* Reset font sizes */

		font_size   = 13;
		char_height = 17;
		char_width  = 8;
		char_offset = 13;

		if (scale_fonts) {
			/* Recalculate scaling */
			font_size   *= font_scaling;
			char_height *= font_scaling;
			char_width  *= font_scaling;
			char_offset *= font_scaling;
		}

		/* Initialize the freetype font pixel sizes */
		FT_Set_Pixel_Sizes(face, font_size, font_size);
		FT_Set_Pixel_Sizes(face_bold, font_size, font_size);
		FT_Set_Pixel_Sizes(face_italic, font_size, font_size);
		FT_Set_Pixel_Sizes(face_bold_italic, font_size, font_size);
		FT_Set_Pixel_Sizes(face_extra, font_size, font_size);
		FT_Set_Pixel_Sizes(face_symbol, font_size, font_size);
		FT_Set_Pixel_Sizes(face_variable, font_size, font_size);
	}
	int i = 0;

	int old_width  = term_width;
	int old_height = term_height;

	term_width  = window_width  / char_width;
	term_height = window_height / char_height;
	if (term_buffer) {
		term_cell_t * new_term_buffer = malloc(sizeof(term_cell_t) * term_width * term_height);

		memset(new_term_buffer, 0x0, sizeof(term_cell_t) * term_width * term_height);
		for (int row = 0; row < min(old_height, term_height); ++row) {
			for (int col = 0; col < min(old_width, term_width); ++col) {
				term_cell_t * old_cell = (term_cell_t *)((uintptr_t)term_buffer + (row * old_width + col) * sizeof(term_cell_t));
				term_cell_t * new_cell = (term_cell_t *)((uintptr_t)new_term_buffer + (row * term_width + col) * sizeof(term_cell_t));
				*new_cell = *old_cell;
			}
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

	struct winsize w;
	w.ws_row = term_height;
	w.ws_col = term_width;
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
		extra_y = decor_height();
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

	if (!_free_size && (t_window_width % char_width != 0 || t_window_height % char_height != 0 && resize_attempts < 3)) {
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

	spin_lock(&display_lock);
	reinit_graphics_yutani(ctx, window);
	reinit(1);
	spin_unlock(&display_lock);

	yutani_window_resize_done(yctx, window);
	yutani_flip(yctx, window);
}

void mouse_event(int button, int x, int y) {
	char buf[7];
	sprintf(buf, "\033[M%c%c%c", button + 32, x + 33, y + 33);
	handle_input_s(buf);
}

void * handle_incoming(void * garbage) {
	while (!exit_application) {
		yutani_msg_t * m = yutani_poll(yctx);
		if (m) {
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
				case YUTANI_MSG_WINDOW_MOUSE_EVENT:
					{
						struct yutani_msg_window_mouse_event * me = (void*)m->data;
						if (!_no_frame) {
							if (decor_handle_event(yctx, m) == DECOR_CLOSE) {
								kill(child_pid, SIGKILL);
								exit_application = 1;
								break;
							}
						}
						/* Map Cursor Action */
						if (ansi_state->mouse_on) {
							int new_x = me->new_x;
							int new_y = me->new_y;
							if (!_no_frame) {
								new_x -= decor_left_width;
								new_y -= decor_top_height;
							}
							/* Convert from coordinate to cell positon */
							new_x /= char_width;
							new_y /= char_height;

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
						}
					}
					break;
				default:
					break;
			}
			free(m);
		}
	}
	pthread_exit(0);
}

void * blink_cursor(void * garbage) {
	while (!exit_application) {
		timer_tick++;
		if (timer_tick == 3) {
			timer_tick = 0;
			spin_lock(&display_lock);
			flip_cursor();
			spin_unlock(&display_lock);
		}
		usleep(90000);
	}
	pthread_exit(0);
}

int main(int argc, char ** argv) {

	_use_freetype = 1;
	_login_shell = 0;

	static struct option long_opts[] = {
		{"fullscreen", no_argument,       0, 'F'},
		{"bitmap",     no_argument,       0, 'b'},
		{"login",      no_argument,       0, 'l'},
		{"help",       no_argument,       0, 'h'},
		{"kernel",     no_argument,       0, 'k'},
		{"grid",       no_argument,       0, 'x'},
		{"no-frame",   no_argument,       0, 'n'},
		{"scale",      required_argument, 0, 's'},
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
				_use_freetype = 0;
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
		window = yutani_window_create(yctx, window_width + decor_left_width + decor_right_width, window_height + decor_top_height + decor_bottom_height);
		init_decorations();
	}

	if (_fullscreen) {
		yutani_set_stack(yctx, window, YUTANI_ZORDER_BOTTOM);
		window->focused = 1;
	} else {
		window->focused = 0;
	}

	/* Initialize the graphics context */
	ctx = init_graphics_yutani(window);

	/* Clear to black */
	draw_fill(ctx, rgba(0,0,0,0));

	yutani_window_move(yctx, window, yctx->display_width / 2 - window->width / 2, yctx->display_height / 2 - window->height / 2);

	if (_use_freetype) {
		int error;
		error = FT_Init_FreeType(&library);
		if (error) return 1;

		char * font = NULL;
		size_t s;

		/* XXX Use shmemfont library */

		font = loadMemFont("/usr/share/fonts/DejaVuSansMono.ttf",  "monospace", &s);
		error = FT_New_Memory_Face(library, font, s, 0, &face); if (error) return 1;

		font = loadMemFont("/usr/share/fonts/DejaVuSansMono-Bold.ttf",  "monospace.bold", &s);
		error = FT_New_Memory_Face(library, font, s, 0, &face_bold); if (error) return 1;

		font = loadMemFont("/usr/share/fonts/DejaVuSansMono-Oblique.ttf",  "monospace.italic", &s);
		error = FT_New_Memory_Face(library, font, s, 0, &face_italic); if (error) return 1;

		font = loadMemFont("/usr/share/fonts/DejaVuSansMono-BoldOblique.ttf",  "monospace.bolditalic", &s);
		error = FT_New_Memory_Face(library, font, s, 0, &face_bold_italic); if (error) return 1;

		error = FT_New_Face(library, "/usr/share/fonts/VLGothic.ttf", 0, &face_extra);

		error = FT_New_Face(library, "/usr/share/fonts/Symbola.ttf", 0, &face_symbol);

		font = loadMemFont("/usr/share/fonts/DejaVuSans.ttf",  "sans-serif", &s);
		error = FT_New_Memory_Face(library, font, s, 0, &face_variable); if (error) return 1;

	}

	syscall_openpty(&fd_master, &fd_slave, NULL, NULL, NULL);

	terminal = fdopen(fd_slave, "w");

	reinit(0);

	fflush(stdin);

	int pid = getpid();
	uint32_t f = fork();

	if (getpid() != pid) {
		dup2(fd_slave, 0);
		dup2(fd_slave, 1);
		dup2(fd_slave, 2);

		if (argv[optind] != NULL) {
			char * tokens[] = {argv[optind], NULL};
			int i = execvp(tokens[0], tokens);
			fprintf(stderr, "Failed to launch requested startup application.\n");
		} else {
			if (_login_shell) {
				char * tokens[] = {"/bin/login",NULL};
				int i = execvp(tokens[0], tokens);
			} else {
				char * shell = getenv("SHELL");
				if (!shell) shell = "/bin/sh"; /* fallback */
				char * tokens[] = {shell,NULL};
				int i = execvp(tokens[0], tokens);
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

		pthread_t wait_for_exit_thread;
		pthread_create(&wait_for_exit_thread, NULL, wait_for_exit, NULL);

		pthread_t handle_incoming_thread;
		pthread_create(&handle_incoming_thread, NULL, handle_incoming, NULL);

		pthread_t cursor_blink_thread;
		pthread_create(&cursor_blink_thread, NULL, blink_cursor, NULL);

		unsigned char buf[1024];
		while (!exit_application) {
			int r = read(fd_master, buf, 1024);
			spin_lock(&display_lock);
			for (uint32_t i = 0; i < r; ++i) {
				ansi_put(ansi_state, buf[i]);
			}
			display_flip();
			spin_unlock(&display_lock);
		}

	}

	yutani_close(yctx, window);

	return 0;
}
