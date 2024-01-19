/**
 * @brief Virtual terminal emulator.
 *
 * Provides a graphical character cell terminal with support for
 * antialiased text, basic Unicode, bitmap fallbacks, nearly
 * complete ANSI escape sequence support, 256- and 24-bit color,
 * scrollback, selection, alternate screens, and various scroll
 * methods.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2022 K. Lange
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <getopt.h>
#include <errno.h>
#include <pty.h>
#include <wchar.h>
#include <dlfcn.h>
#include <pthread.h>

#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/fswait.h>

#define TRACE_APP_NAME "terminal"
#include <toaru/trace.h>
#include <toaru/decodeutf8.h>
#include <toaru/yutani.h>
#include <toaru/decorations.h>
#include <toaru/graphics.h>
#include <toaru/kbd.h>
#include <toaru/termemu.h>
#include <toaru/spinlock.h>
#include <toaru/list.h>
#include <toaru/menu.h>
#include <toaru/text.h>

/* 16- and 256-color palette */
#include "terminal-palette.h"
/* Bitmap font */
#include "terminal-font.h"

/* Show help text */
static void usage(char * argv[]) {
	printf(
			"Terminal Emulator\n"
			"\n"
			"usage: %s [-Fbxn] [-s SCALE] [-g WIDTHxHEIGHT] [COMMAND...]\n"
			"\n"
			" -F --fullscreen \033[3mRun in fullscreen (background) mode.\033[0m\n"
			" -b --bitmap     \033[3mUse the integrated bitmap font.\033[0m\n"
			" -s --scale      \033[3mScale the font in antialiased mode by a given amount.\033[0m\n"
			" -h --help       \033[3mShow this help message.\033[0m\n"
			" -x --grid       \033[3mMake resizes round to nearest match for character cell size.\033[0m\n"
			" -n --no-frame   \033[3mDisable decorations.\033[0m\n"
			" -g --geometry   \033[3mSet requested terminal size WIDTHxHEIGHT\033[0m\n"
			" -B --blurred    \033[3mBlur background behind terminal.\033[0m\n"
			" -S --scrollback \033[3mSet the scrollback buffer size, 0 for unlimited.\033[0m\n"
			"\n"
			" This terminal emulator provides basic support for VT220 escapes and\n"
			" XTerm extensions, including 256 color support and font effects.\n",
			argv[0]);
}

/* master and slave pty descriptors */
static int fd_master, fd_slave;
static FILE * terminal;
static pid_t child_pid = 0;

static int      scale_fonts    = 0;    /* Whether fonts should be scaled */
static float    font_scaling   = 1.0;  /* How much they should be scaled by */
static uint16_t term_width     = 0;    /* Width of the terminal (in cells) */
static uint16_t term_height    = 0;    /* Height of the terminal (in cells) */
static uint16_t font_size      = 16;   /* Font size according to tt library */
static uint16_t char_width     = 8;    /* Width of a cell in pixels */
static uint16_t char_height    = 17;   /* Height of a cell in pixels */
static uint16_t char_offset    = 0;    /* Offset of the font within the cell */
static int      csr_x          = 0;    /* Cursor X */
static int      csr_y          = 0;    /* Cursor Y */
static int      csr_h          = 0;    /* Cursor last column hold flag */
static uint32_t current_fg     = 7;    /* Current foreground color */
static uint32_t current_bg     = 0;    /* Current background color */

static term_cell_t * term_buffer = NULL; /* The active terminal cell buffer */
static term_cell_t * term_buffer_a = NULL; /* The main buffer */
static term_cell_t * term_buffer_b = NULL; /* The secondary buffer */

static term_cell_t * term_mirror = NULL;  /* What we want to draw */
static term_cell_t * term_display = NULL; /* What we think we've drawn already */

static term_state_t * ansi_state = NULL; /* ANSI parser library state */
static int active_buffer  = 0;
static int _orig_x = 0;
static int _orig_y = 0;
static uint32_t _orig_fg = 7;
static uint32_t _orig_bg = 0;

static bool cursor_on      = 1;    /* Whether or not the cursor should be rendered */
static bool _fullscreen    = 0;    /* Whether or not we are running in fullscreen mode (GUI only) */
static bool _no_frame      = 0;    /* Whether to disable decorations or not */
static bool _use_aa        = 1;    /* Whether or not to use best-available anti-aliased renderer */
static bool _free_size     = 1;    /* Disable rounding when resized */

static struct TT_Font * _tt_font_normal = NULL;
static struct TT_Font * _tt_font_bold = NULL;
static struct TT_Font * _tt_font_oblique = NULL;
static struct TT_Font * _tt_font_bold_oblique = NULL;

static struct TT_Font * _tt_font_fallback = NULL;
static struct TT_Font * _tt_font_japanese = NULL;

static list_t * images_list = NULL;

static int menu_bar_height = 24;

/* Text selection information */
static int selection = 0;
static int selection_start_x = 0;
static int selection_start_y = 0;
static int selection_end_x = 0;
static int selection_end_y = 0;
static char * selection_text = NULL;
static int _selection_count = 0;
static int _selection_i = 0;

/* Mouse state */
static int last_mouse_x   = -1;
static int last_mouse_y   = -1;
static int button_state   = 0;
static unsigned long long mouse_ticks = 0;

static yutani_window_t * window       = NULL; /* GUI window */
static yutani_t * yctx = NULL;

/* Window flip bounds */
static int32_t l_x = INT32_MAX;
static int32_t l_y = INT32_MAX;
static int32_t r_x = -1;
static int32_t r_y = -1;

static uint32_t window_width  = 640;
static uint32_t window_height = 480;
static bool     window_position_set = 0;
static int32_t  window_left   = 0;
static int32_t  window_top    = 0;
#define TERMINAL_TITLE_SIZE 512
static char   terminal_title[TERMINAL_TITLE_SIZE];
static size_t terminal_title_length = 0;
static gfx_context_t * ctx;
static struct MenuList * menu_right_click = NULL;

static void render_decors(void);
static void term_clear(int i);
static void reinit(void);
static void term_redraw_cursor();

static int decor_left_width = 0;
static int decor_top_height = 0;
static int decor_right_width = 0;
static int decor_bottom_height = 0;
static int decor_width = 0;
static int decor_height = 0;

struct scrollback_row {
	unsigned short width;
	term_cell_t cells[];
};

static size_t max_scrollback = 10000;
static list_t * scrollback_list = NULL;
static int scrollback_offset = 0;

/* Menu bar entries */
struct menu_bar terminal_menu_bar = {0};
struct menu_bar_entries terminal_menu_entries[] = {
	{"File", "file"},
	{"Edit", "edit"},
	{"View", "view"},
	{"Help", "help"},
	{NULL, NULL},
};

/* Trigger to exit the terminal when the child process dies or
 * we otherwise receive an exit signal */
static volatile int exit_application = 0;

static void cell_redraw(uint16_t x, uint16_t y);
static void cell_redraw_inverted(uint16_t x, uint16_t y);
static void cell_redraw_offset(uint16_t x, uint16_t y);
static void cell_redraw_offset_inverted(uint16_t x, uint16_t y);
static void update_bounds(void);

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

/* Returns the lower of two shorts */
static int32_t min(int32_t a, int32_t b) {
	return (a < b) ? a : b;
}

/* Returns the higher of two shorts */
static int32_t max(int32_t a, int32_t b) {
	return (a > b) ? a : b;
}

/*
 * Convert codepoint to UTF-8
 *
 * Returns length of byte sequence written.
 */
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

/* Set the terminal title string */
static void set_title(char * c) {
	int len = min(TERMINAL_TITLE_SIZE, strlen(c)+1);
	memcpy(terminal_title, c, len);
	terminal_title[len-1] = '\0';
	terminal_title_length = len - 1;
	render_decors();
}

/* Call a function for each selected cell */
static void iterate_selection(void (*func)(uint16_t x, uint16_t y)) {
	if (!selection) return;
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

/* Redraw the selection with the selection hint (inversion) */
static void redraw_selection(void) {
	iterate_selection(cell_redraw_offset_inverted);
}

static term_cell_t * cell_at(uint16_t x, uint16_t _y) {
	int y = _y;
	y -= scrollback_offset;
	if (y >= 0) {
		return &term_buffer[y * term_width + x];
	} else {
		node_t * node = scrollback_list->tail;
		for (; y < -1; y++) {
			if (!node) break;
			node = node->prev;
		}
		if (node) {
			struct scrollback_row * row = (struct scrollback_row *)node->value;
			if (row && x < row->width) {
				return &row->cells[x];
			}
		}
	}
	return NULL;
}

static void mark_cell(uint16_t x, uint16_t y) {
	term_cell_t * c = cell_at(x,y);
	if (c) {
		c->flags |= 0x200;
	}
}

static void mark_selection(void) {
	iterate_selection(mark_cell);
}

static void red_cell(uint16_t x, uint16_t y) {
	term_cell_t * c = cell_at(x,y);
	if (c) {
		if (c->flags & 0x200) {
			c->flags &= ~(0x200);
		} else {
			c->flags |= 0x400;
		}
	}
}

static void flip_selection(void) {
	iterate_selection(red_cell);
	for (int y = 0; y < term_height; ++y) {
		for (int x = 0; x < term_width; ++x) {
			term_cell_t * c = cell_at(x,y);
			if (c) {
				if (c->flags & 0x200) cell_redraw_offset(x,y);
				if (c->flags & 0x400) cell_redraw_offset_inverted(x,y);
				c->flags &= ~(0x600);
			}
		}
	}
}

/* Figure out how long the UTF-8 selection string should be. */
static void count_selection(uint16_t x, uint16_t _y) {
	int y = _y;
	y -= scrollback_offset;
	if (y >= 0) {
		term_cell_t * cell = &term_buffer[y * term_width + x];
		if (!(cell->flags & ANSI_EXT_IMG)) {
			if (((uint32_t *)cell)[0] != 0x00000000) {
				char tmp[7];
				_selection_count += to_eight(cell->c, tmp);
			}
		}
	} else {
		node_t * node = scrollback_list->tail;
		for (; y < -1; y++) {
			if (!node) break;
			node = node->prev;
		}
		if (node) {
			struct scrollback_row * row = (struct scrollback_row *)node->value;
			if (row && x < row->width) {
				term_cell_t * cell = &row->cells[x];
				if (cell && ((uint32_t *)cell)[0] != 0x00000000) {
					char tmp[7];
					_selection_count += to_eight(cell->c, tmp);
				}
			}
		}
	}
	if (x == term_width - 1) {
		_selection_count++;
	}
}

/* Fill the selection text buffer with the selected text. */
void write_selection(uint16_t x, uint16_t _y) {
	int y = _y;
	y -= scrollback_offset;
	if (y >= 0) {
		term_cell_t * cell = &term_buffer[y * term_width + x];
		if (!(cell->flags & ANSI_EXT_IMG)) {
			if (((uint32_t *)cell)[0] != 0x00000000 && cell->c != 0xFFFF) {
				char tmp[7];
				int count = to_eight(cell->c, tmp);
				for (int i = 0; i < count; ++i) {
					selection_text[_selection_i] = tmp[i];
					_selection_i++;
				}
			}
		}
	} else {
		node_t * node = scrollback_list->tail;
		for (; y < -1; y++) {
			if (!node) break;
			node = node->prev;
		}
		if (node) {
			struct scrollback_row * row = (struct scrollback_row *)node->value;
			if (row && x < row->width) {
				term_cell_t * cell = &row->cells[x];
				if (cell && ((uint32_t *)cell)[0] != 0x00000000 && cell->c != 0xFFFF) {
					char tmp[7];
					int count = to_eight(cell->c, tmp);
					for (int i = 0; i < count; ++i) {
						selection_text[_selection_i] = tmp[i];
						_selection_i++;
					}
				}
			}
		}
	}
	if (x == term_width - 1) {
		selection_text[_selection_i] = '\n';;
		_selection_i++;
	}
}

/* Copy the selection text to the clipboard. */
static char * copy_selection(void) {
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

	yutani_set_clipboard(yctx, selection_text);

	return selection_text;
}

static volatile int input_buffer_lock = 0;
static int input_buffer_semaphore[2];
static list_t * input_buffer_queue = NULL;
struct input_data {
	size_t len;
	char data[];
};

void * handle_input_writing(void * unused) {
	(void)unused;

	while (1) {

		/* Read one byte from semaphore; as long as semaphore has data,
		 * there is another input blob to write to the TTY */
		char tmp[1];
		int c = read(input_buffer_semaphore[0],tmp,1);
		if (c > 0) {
			/* Retrieve blob */
			spin_lock(&input_buffer_lock);
			node_t * blob = list_dequeue(input_buffer_queue);
			spin_unlock(&input_buffer_lock);
			/* No blobs? This shouldn't happen, but just in case, just continue */
			if (!blob) {
				continue;
			}
			/* Write blob data to the tty */
			struct input_data * value = blob->value;
			write(fd_master, value->data, value->len);
			free(blob->value);
			free(blob);
		} else {
			/* The pipe has closed, terminal is exiting */
			break;
		}
	}

	return NULL;
}

static void write_input_buffer(char * data, size_t len) {
	struct input_data * d = malloc(sizeof(struct input_data) + len);
	d->len = len;
	memcpy(&d->data, data, len);
	spin_lock(&input_buffer_lock);
	list_insert(input_buffer_queue, d);
	spin_unlock(&input_buffer_lock);
	write(input_buffer_semaphore[1], d, 1);
}

/* Stuffs a string into the stdin of the terminal's child process
 * Useful for things like the ANSI DSR command. */
static void input_buffer_stuff(char * str) {
	size_t len = strlen(str);
	write_input_buffer(str, len);
}


/* Redraw the decorations */
static void render_decors(void) {
	/* Don't draw decorations or bother advertising the window if in "fullscreen mode" */
	if (_fullscreen) return;

	if (!_no_frame) {
		/* Draw the decorations */
		render_decorations(window, ctx, terminal_title_length ? terminal_title : "Terminal");
		/* Update menu bar position and size */
		terminal_menu_bar.x = decor_left_width;
		terminal_menu_bar.y = decor_top_height;
		terminal_menu_bar.width = window_width;
		terminal_menu_bar.window = window;
		/* Redraw the menu bar */
		menu_bar_render(&terminal_menu_bar, ctx);
	}

	/* Advertise the window icon to the panel. */
	yutani_window_advertise_icon(yctx, window, terminal_title_length ? terminal_title : "Terminal", "utilities-terminal");

	/*
	 * Flip the whole window
	 * We do this regardless of whether we drew decorations to catch
	 * a case where decorations are toggled.
	 */
	l_x = 0; l_y = 0;
	r_x = window->width;
	r_y = window->height;
	display_flip();
}

/* Set a pixel in the terminal cell area */
static inline void term_set_point(uint16_t x, uint16_t y, uint32_t color ) {
	GFX(ctx, (x+decor_left_width),(y+decor_top_height+menu_bar_height)) = color;
}

static void _fill_region(uint32_t _bg, uint16_t x, uint16_t y, uint16_t width, uint16_t height) {
	for (uint8_t i = 0; i < height; ++i) {
		for (uint8_t j = 0; j < width; ++j) {
			term_set_point(x+j,y+i,_bg);
		}
	}
}

/* Draw a partial block character. */
static void draw_semi_block(int c, int x, int y, uint32_t fg, uint32_t bg) {
	bg = premultiply(bg);
	fg = alpha_blend_rgba(bg, premultiply(fg));
	_fill_region(bg, x, y, char_width, char_height);
	if (c == 0x2580) {
		for (uint8_t i = 0; i < char_height / 2; ++i) {
			for (uint8_t j = 0; j < char_width; ++j) {
				term_set_point(x+j,y+i,fg);
			}
		}
	} else if (c >= 0x2589) {
		c -= 0x2588;
		int width = char_width - ((c * char_width) / 8);
		for (uint8_t i = 0; i < char_height; ++i) {
			for (uint8_t j = 0; j < width; ++j) {
				term_set_point(x+j, y+i, fg);
			}
		}
	} else {
		c -= 0x2580;
		int height = char_height - ((c * char_height) / 8);
		for (uint8_t i = height; i < char_height; ++i) {
			for (uint8_t j = 0; j < char_width; ++j) {
				term_set_point(x+j, y+i,fg);
			}
		}
	}
}

static void draw_box_drawing(int c, int x, int y, uint32_t fg, uint32_t bg) {
	bg = premultiply(bg);
	fg = alpha_blend_rgba(bg, premultiply(fg));
	_fill_region(bg, x, y, char_width, char_height);

	int lineheight = char_height / 16;
	int linewidth = char_width / 8;

	lineheight = lineheight < 1 ? 1 : lineheight;
	linewidth = linewidth < 1 ? 1 : linewidth;

	int mid_x = char_width / 2 - linewidth / 2;
	int mid_y = char_height / 2 - lineheight / 2;
	int extra_x = (mid_x * 2 < char_width) ? char_width - mid_x * 2 : 0;
	int extra_y = (mid_y * 2 < char_height) ? char_height - mid_y * 2 : 0;

#define UP    _fill_region(fg, x + mid_x, y, linewidth, mid_y + lineheight)
#define DOWN  _fill_region(fg, x + mid_x, y + mid_y, linewidth, mid_y + extra_y)
#define LEFT  _fill_region(fg, x, y + mid_y, mid_x + linewidth, lineheight)
#define RIGHT _fill_region(fg, x + mid_x, y + mid_y, mid_x + extra_x, lineheight)
#define VERT  _fill_region(fg, x + mid_x, y, linewidth, char_height)
#define HORI  _fill_region(fg, x, y + mid_y, char_width, lineheight)

	switch (c) {
		case 0x2500: HORI; break;
		case 0x2502: VERT; break;
		case 0x250c: RIGHT; DOWN; break;
		case 0x2510: LEFT; DOWN; break;
		case 0x2514: UP; RIGHT; break;
		case 0x2518: UP; LEFT; break;
		case 0x251c: VERT; RIGHT; break;
		case 0x2524: VERT; LEFT; break;
		case 0x252c: HORI; DOWN; break;
		case 0x2534: UP; HORI; break;
		case 0x253c: HORI; VERT; break;
		case 0x2574: LEFT; break;
		case 0x2575: UP; break;
		case 0x2576: RIGHT; break;
		case 0x2577: DOWN; break;
	}
}

#include "apps/ununicode.h"

struct GlyphCacheEntry {
	struct TT_Font * font;
	sprite_t * sprite;
	uint32_t size;
	uint32_t glyph;
	uint32_t color;
};

static struct GlyphCacheEntry glyph_cache[1024];
static unsigned long _hits = 0;
static unsigned long _misses = 0;
static unsigned long _wrongcolor = 0;

static void _menu_action_cache_stats(struct MenuEntry * self) {
	char msg[400];

	unsigned long count = 0;
	unsigned long size = 0;

	for (int i = 0; i < 1024; ++i) {
		if (glyph_cache[i].sprite) {
			count++;
			size += glyph_cache[i].sprite->width * glyph_cache[i].sprite->height * 4;
		}
	}

	snprintf(msg, 400,
		"Hits: %lu\n"
		"Misses: %lu\n"
		"Wrong color: %lu\n"
		"Populated cache entries: %lu\n"
		"Size of sprites: %lu\n",
		_hits, _misses, _wrongcolor, count, size);

	write(fd_slave, msg, strlen(msg));
}

static void _menu_action_clear_cache(struct MenuEntry * self) {
	for (int i = 0; i < 1024; ++i) {
		if (glyph_cache[i].sprite) {
			sprite_free(glyph_cache[i].sprite);
		}
	}
	memset(glyph_cache,0,sizeof(glyph_cache));
}

static void draw_cached_glyph(gfx_context_t * ctx, struct TT_Font * _font, uint32_t size, int x, int y, uint32_t glyph, uint32_t fg, int flags) {
	unsigned int hash = (((uintptr_t)_font >> 8) ^ (glyph * size)) & 1023;

	struct GlyphCacheEntry * entry = &glyph_cache[hash];

	if (entry->font != _font || entry->size != size || entry->glyph != glyph) {
		if (entry->sprite) sprite_free(entry->sprite);
		int wide = (flags & ANSI_WIDE) ? 2 : 1;
		tt_set_size(_font, size);

		entry->font = _font;
		entry->size = size;
		entry->glyph = glyph;
		entry->sprite = create_sprite(char_width * wide, char_height, ALPHA_EMBEDDED);
		entry->color = _ALP(fg) == 255 ? fg : 0xFFFFFFFF;

		gfx_context_t * _ctx = init_graphics_sprite(entry->sprite);
		draw_fill(_ctx, 0);
		tt_draw_glyph(_ctx, entry->font, 0, char_offset, glyph, entry->color);
		free(_ctx);
		_misses++;
	} else {
		_hits++;
	}

	if (entry->color != fg) {
		_wrongcolor++;
		draw_sprite_alpha_paint(ctx, entry->sprite, x, y, 1.0, fg);
	} else {
		draw_sprite(ctx, entry->sprite, x, y);
	}
}

/* Write a character to the window. */
static void term_write_char(uint32_t val, uint16_t x, uint16_t y, uint32_t fg, uint32_t bg, uint8_t flags) {
	uint32_t _fg, _bg;

	/* Select foreground color from palette. */
	if (fg < PALETTE_COLORS) {
		_fg = term_colors[fg];
		_fg |= 0xFF << 24;
	} else {
		_fg = fg;
	}

	/* Select background color from aplette. */
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

	if (_fullscreen) {
		_bg |= 0xFF << 24;
	}

	switch (val) {
		/* Line drawing */
		case 0x2500:
		case 0x2502:
		case 0x250c:
		case 0x2510:
		case 0x2514:
		case 0x2518:
		case 0x251c:
		case 0x2524:
		case 0x252c:
		case 0x2534:
		case 0x253c:
		case 0x2574:
		case 0x2575:
		case 0x2576:
		case 0x2577:
			draw_box_drawing(val, x, y, _fg, _bg);
			goto _extra_stuff;

		/* Semi-filled blocks */
		case 0x2580 ... 0x258f: {
			draw_semi_block(val, x, y, _fg, _bg);
			goto _extra_stuff;
		}

		/* Instead of checker, does 50% opacity fill */
		case 0x2591:
		case 0x2592:
		case 0x2593:
			_fill_region(alpha_blend_rgba(premultiply(_bg), interp_colors(rgb(0,0,0), premultiply(_fg), 255 * (val - 0x2590) / 4)), x, y, char_width, char_height);
			goto _extra_stuff;


		default:
			break;
	}

	/* Draw glyphs */
	if (_use_aa) {
		if (val == 0xFFFF) return;
		for (uint8_t i = 0; i < char_height; ++i) {
			for (uint8_t j = 0; j < char_width; ++j) {
				term_set_point(x+j,y+i,_bg);
			}
		}
		if (flags & ANSI_WIDE) {
			for (uint8_t i = 0; i < char_height; ++i) {
				for (uint8_t j = char_width; j < 2 * char_width; ++j) {
					term_set_point(x+j,y+i,_bg);
				}
			}
		}
		if (val < 32 || val == ' ') {
			goto _extra_stuff;
		}
		struct TT_Font * _font = _tt_font_normal;
		if (flags & ANSI_BOLD && flags & ANSI_ITALIC) {
			_font = _tt_font_bold_oblique;
		} else if (flags & ANSI_BOLD) {
			_font = _tt_font_bold;
		} else if (flags & ANSI_ITALIC) {
			_font = _tt_font_oblique;
		}
		unsigned int glyph = tt_glyph_for_codepoint(_font, val);

		/* Try the regular sans serif font as a fallback */
		if (!glyph) {
			int nglyph = tt_glyph_for_codepoint(_tt_font_fallback, val);
			if (nglyph) {
				_font = _tt_font_fallback;
				glyph = nglyph;
			}
		}

		/* Try the VL Gothic, if it's installed and this is a reasonably high codepoint */
		if (!glyph && _tt_font_japanese && val >= 0x2E80) {
			int nglyph = tt_glyph_for_codepoint(_tt_font_japanese, val);
			if (nglyph) {
				_font = _tt_font_japanese;
				glyph = nglyph;
			}
		}

		int _x = x + decor_left_width;
		int _y = y + decor_top_height + menu_bar_height;
		draw_cached_glyph(ctx, _font, font_size, _x,_y, glyph, _fg, flags);
	} else {
		/* Convert other unicode characters. */
		if (val > 128) {
			val = ununicode(val);
		}
		/* Draw using the bitmap font. */
		uint8_t * c = large_font[val];
		for (uint8_t i = 0; i < char_height; ++i) {
			for (uint8_t j = 0; j < char_width; ++j) {
				if (c[i] & (1 << (LARGE_FONT_MASK-j))) {
					term_set_point(x+j,y+i,_fg);
				} else {
					term_set_point(x+j,y+i,_bg);
				}
			}
		}
	}

	/* Draw additional text elements, like underlines and cross-outs. */
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

	/* Calculate the bounds of the updated region of the window */
	l_x = min(l_x, decor_left_width + x);
	l_y = min(l_y, decor_top_height+menu_bar_height + y);

	if (flags & ANSI_WIDE) {
		r_x = max(r_x, decor_left_width + x + char_width * 2);
		r_y = max(r_y, decor_top_height+menu_bar_height + y + char_height * 2);
	} else {
		r_x = max(r_x, decor_left_width + x + char_width);
		r_y = max(r_y, decor_top_height+menu_bar_height + y + char_height);
	}
}

static void term_mirror_set(uint16_t x, uint16_t y, uint32_t val, uint32_t fg, uint32_t bg, uint8_t flags) {
	if (x >= term_width || y >= term_height) return;
	term_cell_t * cell = &term_mirror[y * term_width + x];
	cell->c = val;
	cell->fg = fg;
	cell->bg = bg;
	cell->flags = flags;
}

static void term_mirror_copy(uint16_t x, uint16_t y, term_cell_t * from) {
	if (x >= term_width || y >= term_height) return;
	term_cell_t * cell = &term_mirror[y * term_width + x];
	if (!from->c && !from->fg && !from->bg) {
		cell->c = ' ';
		cell->fg = TERM_DEFAULT_FG;
		cell->bg = TERM_DEFAULT_BG;
		cell->flags = from->flags;
	} else {
		*cell = *from;
	}
}

static void term_mirror_copy_inverted(uint16_t x, uint16_t y, term_cell_t * from) {
	if (x >= term_width || y >= term_height) return;
	term_cell_t * cell = &term_mirror[y * term_width + x];
	if (!from->c && !from->fg && !from->bg) {
		cell->c = ' ';
		cell->fg = TERM_DEFAULT_BG;
		cell->bg = TERM_DEFAULT_FG;
		cell->flags = from->flags;
	} else if (from->flags & ANSI_EXT_IMG) {
		cell->c = ' ';
		cell->fg = from->fg;
		cell->bg = from->bg;
		cell->flags = from->flags | ANSI_SPECBG;
	} else {
		cell->c = from->c;
		cell->fg = from->bg;
		cell->bg = from->fg;
		cell->flags = from->flags | ANSI_SPECBG;
	}
}

/* Set a terminal cell */
static void cell_set(uint16_t x, uint16_t y, uint32_t c, uint32_t fg, uint32_t bg, uint32_t flags) {
	/* Avoid setting cells out of range. */
	if (x >= term_width || y >= term_height) return;

	/* Calculate the cell position in the terminal buffer */
	term_cell_t * cell = &term_buffer[y * term_width + x];

	/* Set cell attributes */
	cell->c     = c;
	cell->fg    = fg;
	cell->bg    = bg;
	cell->flags = flags;
}

/* Redraw an embedded image cell */
static void redraw_cell_image(uint16_t x, uint16_t y, term_cell_t * cell, int inverted) {
	/* Avoid setting cells out of range. */
	if (x >= term_width || y >= term_height) return;

	/* Draw the image data */
	uint32_t * data = (uint32_t *)((uintptr_t)cell->bg << 32 | cell->fg);
	if (inverted) {
		for (uint32_t yy = 0; yy < char_height; ++yy) {
			for (uint32_t xx = 0; xx < char_width; ++xx) {
				uint32_t alpha = 0xFF000000 & *data;
				uint32_t color = 0xFFFFFF - (*data & 0xFFFFFF);
				term_set_point(x * char_width + xx, y * char_height + yy, color | alpha);
				data++;
			}
		}
	} else {
		for (uint32_t yy = 0; yy < char_height; ++yy) {
			for (uint32_t xx = 0; xx < char_width; ++xx) {
				term_set_point(x * char_width + xx, y * char_height + yy, *data);
				data++;
			}
		}
	}

	/* Update bounds */
	l_x = min(l_x, decor_left_width + x * char_width);
	l_y = min(l_y, decor_top_height+menu_bar_height + y * char_height);
	r_x = max(r_x, decor_left_width + x * char_width + char_width);
	r_y = max(r_y, decor_top_height+menu_bar_height + y * char_height + char_height);
}

static void maybe_flip_display(int force) {
	static uint64_t last_refresh;
	uint64_t ticks = get_ticks();
	if (!force) {
		if (ticks < last_refresh + 33330L) {
			return;
		}
	}

	last_refresh = ticks;

	for (unsigned int y = 0; y < term_height; ++y) {
		for (unsigned int x = 0; x < term_width; ++x) {
			term_cell_t * cell_m = &term_mirror[y * term_width + x];
			term_cell_t * cell_d = &term_display[y * term_width + x];
			if (memcmp(cell_m, cell_d, sizeof(term_cell_t))) {
				*cell_d = *cell_m;
				if (cell_m->flags & ANSI_EXT_IMG) {
					redraw_cell_image(x,y,cell_m,cell_m->flags & ANSI_SPECBG);
				} else {
					term_write_char(cell_m->c, x * char_width, y * char_height, cell_m->fg, cell_m->bg, cell_m->flags);
				}
			}
		}
	}
	display_flip();
}

static void cell_redraw_offset(uint16_t x, uint16_t _y) {
	int y = _y;
	int i = y;

	y -= scrollback_offset;

	if (y >= 0) {
		term_mirror_copy(x,i,&term_buffer[y * term_width + x]);
	} else {
		node_t * node = scrollback_list->tail;
		for (; y < -1; y++) {
			if (!node) break;
			node = node->prev;
		}
		if (node) {
			struct scrollback_row * row = (struct scrollback_row *)node->value;
			if (row && x < row->width) {
				term_mirror_copy(x,i,&row->cells[x]);
			} else {
				term_mirror_set(x,i,' ',TERM_DEFAULT_FG, TERM_DEFAULT_BG, TERM_DEFAULT_FLAGS);
			}
		}
	}
}

static void cell_redraw_offset_inverted(uint16_t x, uint16_t _y) {
	int y = _y;
	int i = y;

	y -= scrollback_offset;

	if (y >= 0) {
		term_mirror_copy_inverted(x,i,&term_buffer[y * term_width + x]);
	} else {
		node_t * node = scrollback_list->tail;
		for (; y < -1; y++) {
			if (!node) break;
			node = node->prev;
		}
		if (node) {
			struct scrollback_row * row = (struct scrollback_row *)node->value;
			if (row && x < row->width) {
				term_mirror_copy_inverted(x,i,&row->cells[x]);
			} else {
				term_mirror_set(x, i, ' ', TERM_DEFAULT_BG, TERM_DEFAULT_FG, TERM_DEFAULT_FLAGS|ANSI_SPECBG);
			}
		}
	}
}

/* Redraw a text cell normally. */
static void cell_redraw(uint16_t x, uint16_t y) {
	if (x >= term_width || y >= term_height) return;
	term_mirror_copy(x,y,&term_buffer[y * term_width + x]);
}

/* Redraw text cell inverted. */
static void cell_redraw_inverted(uint16_t x, uint16_t y) {
	/* Avoid cells out of range. */
	if (x >= term_width || y >= term_height) return;
	term_mirror_copy_inverted(x,y,&term_buffer[y * term_width + x]);
}

/* Redraw text cell with a surrounding box (used by cursor) */
static void cell_redraw_box(uint16_t x, uint16_t y) {
	if (x >= term_width || y >= term_height) return;
	term_cell_t cell = term_buffer[y * term_width + x];
	cell.flags |= ANSI_BORDER;
	term_mirror_copy(x,y,&cell);
}

/* Draw the cursor cell */
static void render_cursor() {
	if (!cursor_on) return;
	if (!window->focused) {
		/* An unfocused terminal should draw an unfilled box. */
		cell_redraw_box(csr_x, csr_y);
	} else {
		/* A focused terminal draws a solid box. */
		cell_redraw_inverted(csr_x, csr_y);
	}
}

static uint8_t cursor_flipped = 0;
/* A soft request to draw the cursor. */
static void draw_cursor() {
	if (!cursor_on) return;
	cursor_flipped = 0;
	render_cursor();
}

/* Timer callback to flip (flash) the cursor */
static void maybe_flip_cursor(void) {
	uint64_t ticks = get_ticks();
	if (ticks > mouse_ticks + 600000LL) {
		mouse_ticks = ticks;
		if (scrollback_offset != 0) {
			return; /* Don't flip cursor while drawing scrollback */
		}
		if (window->focused && cursor_flipped) {
			cell_redraw(csr_x, csr_y);
		} else {
			render_cursor();
		}
		cursor_flipped = 1 - cursor_flipped;
	}
}

/* Draw all cells. Duplicates code from cell_redraw to avoid unecessary bounds checks. */
static void term_redraw_all() {
	for (int i = 0; i < term_height; i++) {
		for (int x = 0; x < term_width; ++x) {
			term_mirror_copy(x,i,&term_buffer[i * term_width + x]);
		}
	}
}

static void _menu_action_redraw(struct MenuEntry * self) {
	term_redraw_all();
}

/* Remove no-longer-visible image cell data. */
static void flush_unused_images(void) {
	if (!images_list->length) return;

	list_t * tmp = list_create();

	/* Go through scrollback, too */
	if (scrollback_list) {
		foreach(node, scrollback_list) {
			struct scrollback_row * row = (struct scrollback_row *)node->value;
			for (unsigned int x = 0; x < row->width; ++x) {
				term_cell_t * cell = &row->cells[x];
				if (cell->flags & ANSI_EXT_IMG) {
					uint32_t * data = (uint32_t *)((uintptr_t)cell->bg << 32 | cell->fg);
					list_insert(tmp, data);
				}
			}
		}
	}

	for (int y = 0; y < term_height; ++y) {
		for (int x = 0; x < term_width; ++x) {
			term_cell_t * cell = &term_buffer_a[y * term_width + x];
			if (cell->flags & ANSI_EXT_IMG) {
				uint32_t * data = (uint32_t *)((uintptr_t)cell->bg << 32 | cell->fg);
				list_insert(tmp, data);
			}
		}
	}

	for (int y = 0; y < term_height; ++y) {
		for (int x = 0; x < term_width; ++x) {
			term_cell_t * cell = &term_buffer_b[y * term_width + x];
			if (cell->flags & ANSI_EXT_IMG) {
				uint32_t * data = (uint32_t *)((uintptr_t)cell->bg << 32 | cell->fg);
				list_insert(tmp, data);
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

static void term_shift_region(int top, int height, int how_much) {
	if (how_much == 0) return;

	int destination, source;
	int count, new_top, new_bottom;
	if (how_much > height) {
		count = 0;
		new_top = top;
		new_bottom = top + height;
	} else if (how_much > 0) {
		destination = term_width * top;
		source = term_width * (top + how_much);
		count = height - how_much;
		new_top = top + height - how_much;
		new_bottom = top + height;
	} else if (how_much < 0) {
		destination = term_width * (top - how_much);
		source = term_width * top;
		count = height + how_much;
		new_top = top;
		new_bottom = top - how_much;
	}

	/* Move from top+how_much to top */
	if (count) {
		memmove(term_buffer + destination, term_buffer + source, count * term_width * sizeof(term_cell_t));
		memmove(term_mirror + destination, term_mirror + source, count * term_width * sizeof(term_cell_t));
	}

	l_x = 0; l_y = 0;
	r_x = window->width;
	r_y = window->height;

	/* Clear new lines at bottom */
	for (int i = new_top; i < new_bottom; ++i) {
		for (uint16_t x = 0; x < term_width; ++x) {
			cell_set(x, i, ' ', current_fg, current_bg, ansi_state->flags);
			cell_redraw(x, i);
		}
	}
}

/* Scroll the terminal up or down. */
static void term_scroll(int how_much) {
	term_shift_region(0, term_height, how_much);

	/* Remove image data for image cells that are no longer on screen. */
	flush_unused_images();
}

static void insert_delete_lines(int how_many) {
	if (how_many == 0) return;

	if (how_many > 0) {
		/* Insert lines is equivalent to scrolling from the current line */
		term_shift_region(csr_y,term_height-csr_y,-how_many);
	} else {
		term_shift_region(csr_y,term_height-csr_y,-how_many);
	}
}

/* Is this a wide character? (does wcwidth == 2) */
static int is_wide(uint32_t codepoint) {
	if (codepoint < 256) return 0;
	return wcwidth(codepoint) == 2;
}

/* Save the row that is about to be scrolled offscreen into the scrollback buffer. */
static void save_scrollback(void) {
	/* If the scrollback is already full, remove the oldest element. */
	struct scrollback_row * row = NULL;
	node_t * n = NULL;

	if (max_scrollback && scrollback_list->length == max_scrollback) {
		n = list_dequeue(scrollback_list);
		row = n->value;
		if (row->width < term_width) {
			free(row);
			row = NULL;
		}
	}

	if (!row) {
		row = malloc(sizeof(struct scrollback_row) + sizeof(term_cell_t) * term_width);
		row->width = term_width;
	}

	if (!n) {
		list_insert(scrollback_list, row);
	} else {
		n->value = row;
		list_append(scrollback_list, n);
	}

	for (int i = 0; i < term_width; ++i) {
		term_cell_t * cell = &term_buffer[i];
		memcpy(&row->cells[i], cell, sizeof(term_cell_t));
	}
}

/* Draw the scrollback. */
static void redraw_scrollback(void) {
	if (!scrollback_offset) {
		term_redraw_all();
		return;
	}
	if (scrollback_offset < term_height) {
		for (int i = scrollback_offset; i < term_height; i++) {
			int y = i - scrollback_offset;
			for (int x = 0; x < term_width; ++x) {
				term_mirror_copy(x,i,&term_buffer[y * term_width + x]);
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
					term_mirror_set(x, y, ' ', TERM_DEFAULT_FG, TERM_DEFAULT_BG, TERM_DEFAULT_FLAGS);
				}
			}
			for (int x = 0; x < width; ++x) {
				term_mirror_copy(x,y,&row->cells[x]);
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
					term_mirror_set(x, y, ' ', TERM_DEFAULT_FG, TERM_DEFAULT_BG, TERM_DEFAULT_FLAGS);
				}
			}
			for (int x = 0; x < width; ++x) {
				term_mirror_copy(x,y,&row->cells[x]);
			}

			node = node->prev;
		}
	}
}

static void undraw_cursor(void) {
	cell_redraw(csr_x, csr_y);
}

static void normalize_x(int setting_lcf) {
	if (csr_x >= term_width) {
		csr_x = term_width - 1;
		if (setting_lcf) {
			csr_h = 1;
		}
	}
}

static void normalize_y(void) {
	if (csr_y == term_height) {
		save_scrollback();
		term_scroll(1);
		csr_y = term_height - 1;
	}
}

/*
 * ANSI callback for writing characters.
 * Parses some things (\n\r, etc.) itself that should probably
 * be moved into the ANSI library.
 */
static void term_write(char c) {
	static uint32_t unicode_state = 0;
	static uint32_t codepoint = 0;

	if (!decode(&unicode_state, &codepoint, (uint8_t)c)) {
		uint32_t o = codepoint;
		codepoint = 0;

		switch (c) {
			case '\a':
				/* boop */
				return;

			case '\r':
				undraw_cursor();
				csr_x = csr_h = 0;
				draw_cursor();
				return;

			case '\t':
				undraw_cursor();
				csr_x += (8 - csr_x % 8);
				normalize_x(0);
				draw_cursor();
				return;

			case '\v':
			case '\f':
			case '\n':
				undraw_cursor();
				csr_h = 0;
				++csr_y;
				normalize_y();
				draw_cursor();
				return;

			case '\b':
				if (csr_x > 0) {
					undraw_cursor();
					--csr_x;
					draw_cursor();
				}
				csr_h = 0;
				return;

			default: {
				int wide = is_wide(o);
				uint8_t flags = ansi_state->flags;

				undraw_cursor();

				if (csr_h || (wide && csr_x == term_width - 1)) {
					csr_x = csr_h = 0;
					++csr_y;
					normalize_y();
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

				normalize_x(1);
				draw_cursor();
				return;
			}
		}

	} else if (unicode_state == UTF8_REJECT) {
		unicode_state = 0;
		codepoint = 0;
	}
}

/* ANSI callback to set cursor position */
static void term_set_csr(int x, int y) {
	cell_redraw(csr_x,csr_y);
	if (x < 0) x = 0;
	if (x >= term_width) x = term_width - 1;
	if (y < 0) y = 0;
	if (y >= term_height) y = term_height - 1;
	csr_x = x;
	csr_y = y;
	csr_h = 0;
	draw_cursor();
}

/* ANSI callback to get cursor x position */
static int term_get_csr_x(void) {
	return csr_x;
}

/* ANSI callback to get cursor y position */
static int term_get_csr_y(void) {
	return csr_y;
}

/* ANSI callback to set cell image data. */
static void term_set_cell_contents(int x, int y, char * data) {
	char * cell_data = malloc(char_width * char_height * sizeof(uint32_t));
	memcpy(cell_data, data, char_width * char_height * sizeof(uint32_t));
	list_insert(images_list, cell_data);
	cell_set(x, y, ' ',
		(uintptr_t)(cell_data) & 0xFFFFFFFF,
		(uintptr_t)(cell_data) >> 32,
		ANSI_EXT_IMG);
}

/* ANSI callback to get character cell width */
static int term_get_cell_width(void) {
	return char_width;
}

/* ANSI callback to get character cell height */
static int term_get_cell_height(void) {
	return char_height;
}

/* ANSI callback to set cursor visibility */
static void term_set_csr_show(int on) {
	cursor_on = on;
	if (on) {
		draw_cursor();
	}
}

/* ANSI callback to set the foreground/background colors. */
static void term_set_colors(uint32_t fg, uint32_t bg) {
	current_fg = fg;
	current_bg = bg;
}

/* ANSI callback to force the cursor to draw */
static void term_redraw_cursor() {
	if (term_buffer) {
		draw_cursor();
	}
}

/* ANSI callback to set a cell to a codepoint (only ever used to set spaces) */
static void term_set_cell(int x, int y, uint32_t c) {
	cell_set(x, y, c, current_fg, current_bg, ansi_state->flags);
	cell_redraw(x, y);
}

/* ANSI callback to clear the terminal. */
static void term_clear(int i) {
	if (i == 2) {
		/* Clear all */
		csr_x = 0;
		csr_y = 0;
		csr_h = 0;
		memset((void *)term_buffer, 0x00, term_width * term_height * sizeof(term_cell_t));
		if (!_no_frame) {
			render_decors();
		}
		term_redraw_all();
	} else if (i == 0) {
		/* Clear after cursor */
		for (int x = csr_x; x < term_width; ++x) {
			term_set_cell(x, csr_y, ' ');
		}
		for (int y = csr_y + 1; y < term_height; ++y) {
			for (int x = 0; x < term_width; ++x) {
				term_set_cell(x, y, ' ');
			}
		}
	} else if (i == 1) {
		/* Clear before cursor */
		for (int y = 0; y < csr_y; ++y) {
			for (int x = 0; x < term_width; ++x) {
				term_set_cell(x, y, ' ');
			}
		}
		for (int x = 0; x < csr_x; ++x) {
			term_set_cell(x, csr_y, ' ');
		}
	} else if (i == 3) {
		/* Clear scrollback */
		if (scrollback_list) {
			while (scrollback_list->length) {
				node_t * n = list_dequeue(scrollback_list);
				free(n->value);
				free(n);
			}
			scrollback_offset = 0;
		}

	}
	flush_unused_images();
}


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

/* ANSI callbacks */
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
	term_set_cell_contents,
	term_get_cell_width,
	term_get_cell_height,
	term_set_csr_show,
	term_switch_buffer,
	insert_delete_lines,
};

static void handle_input(char c) {
	write_input_buffer(&c, 1);
	if (scrollback_offset != 0) {
		scrollback_offset = 0;
		term_redraw_all();
	}
}

static void handle_input_s(char * c) {
	size_t len = strlen(c);
	write_input_buffer(c, len);
	if (scrollback_offset != 0) {
		scrollback_offset = 0;
		term_redraw_all();
	}
}


/* Scroll the view up (scrollback) */
static void scroll_up(int amount) {
	int i = 0;
	while (i < amount && scrollback_list && scrollback_offset < (int)scrollback_list->length) {
		scrollback_offset ++;
		i++;
	}
	redraw_scrollback();
}

/* Scroll the view down (scrollback) */
void scroll_down(int amount) {
	int i = 0;
	while (i < amount && scrollback_list && scrollback_offset != 0) {
		scrollback_offset -= 1;
		i++;
	}
	redraw_scrollback();
}

/* Handle a key press from Yutani */
static void key_event(int ret, key_event_t * event) {
	if (ret) {
		/* Ctrl-Shift-C - Copy selection */
		if ((event->modifiers & KEY_MOD_LEFT_SHIFT || event->modifiers & KEY_MOD_RIGHT_SHIFT) &&
			(event->modifiers & KEY_MOD_LEFT_CTRL || event->modifiers & KEY_MOD_RIGHT_CTRL) &&
			(event->keycode == 'c')) {
			if (selection) {
				/* Copy selection */
				copy_selection();
			}
			return;
		}

		/* Ctrl-Shift-V - Paste selection */
		if ((event->modifiers & KEY_MOD_LEFT_SHIFT || event->modifiers & KEY_MOD_RIGHT_SHIFT) &&
			(event->modifiers & KEY_MOD_LEFT_CTRL || event->modifiers & KEY_MOD_RIGHT_CTRL) &&
			(event->keycode == 'v')) {
			/* Paste selection */
			yutani_special_request(yctx, NULL, YUTANI_SPECIAL_REQUEST_CLIPBOARD);
			return;
		}

		if ((event->modifiers & KEY_MOD_LEFT_CTRL || event->modifiers & KEY_MOD_RIGHT_CTRL) &&
			(event->keycode == '0')) {
			scale_fonts  = 0;
			font_scaling = 1.0;
			reinit();
			return;
		}

		if ((event->modifiers & KEY_MOD_LEFT_SHIFT || event->modifiers & KEY_MOD_RIGHT_SHIFT) &&
			(event->modifiers & KEY_MOD_LEFT_CTRL || event->modifiers & KEY_MOD_RIGHT_CTRL) &&
			(event->keycode == '=')) {
			scale_fonts  = 1;
			font_scaling = font_scaling * 1.2;
			reinit();
			return;
		}

		if ((event->modifiers & KEY_MOD_LEFT_CTRL || event->modifiers & KEY_MOD_RIGHT_CTRL) &&
			(event->keycode == '-')) {
			scale_fonts  = 1;
			font_scaling = font_scaling * 0.8333333;
			reinit();
			return;
		}

		/* Left alt */
		if (event->modifiers & KEY_MOD_LEFT_ALT || event->modifiers & KEY_MOD_RIGHT_ALT) {
			handle_input('\033');
		}

		/* Shift-Tab */
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

		/* Pass key value to PTY */
		handle_input(event->key);
	} else {
		/* Special keys without ->key values */

		/* Only trigger on key down */
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
					update_bounds();
					window_width = window->width - decor_width;
					window_height = window->height - (decor_height + menu_bar_height);
					reinit();
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
					handle_input_s("\033[H");
				}
				break;
			case KEY_END:
				if (event->modifiers & KEY_MOD_LEFT_SHIFT) {
					scrollback_offset = 0;
					redraw_scrollback();
				} else {
					handle_input_s("\033[F");
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

/* Check if the Terminal should close. */
static void check_for_exit(void) {

	/* If something has set exit_application, we should exit. */
	if (exit_application) return;

	pid_t pid = waitpid(-1, NULL, WNOHANG);

	/* If the child has exited, we should exit. */
	if (pid != child_pid) return;

	/* Clean up */
	exit_application = 1;

	/* Write [Process terminated] */
	char exit_message[] = "[Process terminated]\n";
	write(fd_slave, exit_message, sizeof(exit_message));
	close(input_buffer_semaphore[1]);
}

static term_cell_t * copy_terminal(int old_width, int old_height, term_cell_t * term_buffer) {
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
			term_cell_t * old_cell = &term_buffer[(row+offset) * old_width + col];
			term_cell_t * new_cell = &new_term_buffer[row * term_width + col];
			*new_cell = *old_cell;
		}
	}
	if (csr_x >= term_width) {
		csr_x = term_width-1;
	}

	return new_term_buffer;
}

/* Reinitialize the terminal after a resize. */
static void reinit(void) {

	/* Figure out character sizes if fonts have changed. */
	if (_use_aa) {
		char_width = 8;
		char_height = 17;
		font_size = 13;
		char_offset = 13;
		if (scale_fonts) {
			font_size   *= font_scaling;
			char_height *= font_scaling;
			char_width  *= font_scaling;
			char_offset *= font_scaling;
		}
	} else {
		char_width = LARGE_FONT_CELL_WIDTH;
		char_height = LARGE_FONT_CELL_HEIGHT;
	}

	int old_width  = term_width;
	int old_height = term_height;

	/* Resize the terminal buffer */
	term_width  = window_width  / char_width;
	term_height = window_height / char_height;

	if (term_width == old_width && term_height == old_height) {
		memset(term_display, 0xFF, sizeof(term_cell_t) * term_width * term_height);
		draw_fill(ctx, rgba(0,0,0, TERM_DEFAULT_OPAC));
		render_decors();
		maybe_flip_display(1);
		return;
	}

	if (term_buffer) {
		term_cell_t * new_a = copy_terminal(old_width, old_height, term_buffer_a);
		term_cell_t * new_b = copy_terminal(old_width, old_height, term_buffer_b);
		free(term_buffer_a);
		term_buffer_a = new_a;
		free(term_buffer_b);
		term_buffer_b = new_b;
		if (active_buffer == 0) {
			term_buffer = new_a;
		} else {
			term_buffer = new_b;
		}
	} else {
		term_buffer_a = malloc(sizeof(term_cell_t) * term_width * term_height);
		memset(term_buffer_a, 0x0, sizeof(term_cell_t) * term_width * term_height);

		term_buffer_b = malloc(sizeof(term_cell_t) * term_width * term_height);
		memset(term_buffer_b, 0x0, sizeof(term_cell_t) * term_width * term_height);

		term_buffer = term_buffer_a;
	}

	term_mirror = realloc(term_mirror, sizeof(term_cell_t) * term_width * term_height);
	memcpy(term_mirror, term_buffer, sizeof(term_cell_t) * term_width * term_height);

	term_display = realloc(term_display, sizeof(term_cell_t) * term_width * term_height);
	memset(term_display, 0xFF, sizeof(term_cell_t) * term_width * term_height);

	/* Reset the ANSI library, ensuring we keep certain values */
	int old_mouse_state = 0;
	if (ansi_state) old_mouse_state = ansi_state->mouse_on;
	ansi_state = ansi_init(ansi_state, term_width, term_height, &term_callbacks);
	ansi_state->mouse_on = old_mouse_state;

	/* Send window size change ioctl */
	struct winsize w;
	w.ws_row = term_height;
	w.ws_col = term_width;
	w.ws_xpixel = term_width * char_width;
	w.ws_ypixel = term_height * char_height;
	ioctl(fd_master, TIOCSWINSZ, &w);

	/* Redraw the window */
	draw_fill(ctx, rgba(0,0,0, TERM_DEFAULT_OPAC));
	render_decors();
	term_redraw_all();
}

static void update_bounds(void) {
	if (!_no_frame) {
		struct decor_bounds bounds;
		decor_get_bounds(window, &bounds);

		decor_left_width = bounds.left_width;
		decor_top_height = bounds.top_height;
		decor_right_width = bounds.right_width;
		decor_bottom_height = bounds.bottom_height;
		decor_width = bounds.width;
		decor_height = bounds.height;
		menu_bar_height = 24;
	} else {
		decor_left_width = 0;
		decor_top_height = 0;
		decor_right_width = 0;
		decor_bottom_height = 0;
		decor_width = 0;
		decor_height = 0;
		menu_bar_height = 0;
	}
}

/* Handle window resize event. */
static void resize_finish(int width, int height) {
	static int resize_attempts = 0;

	/* Calculate window size */
	update_bounds();
	int extra_x = decor_width;
	int extra_y = decor_height + menu_bar_height;

	int t_window_width  = width  - extra_x;
	int t_window_height = height - extra_y;

	/* Prevent the terminal from becoming too small. */
	if (t_window_width < char_width * 20 || t_window_height < char_height * 10) {
		resize_attempts++;
		int n_width  = extra_x + max(char_width * 20, t_window_width);
		int n_height = extra_y + max(char_height * 10, t_window_height);
		yutani_window_resize_offer(yctx, window, n_width, n_height);
		return;
	}

	/* If requested, ensure the terminal resizes to a fixed size based on the cell size. */
	if (!_free_size && ((t_window_width % char_width != 0 || t_window_height % char_height != 0) && resize_attempts < 3)) {
		resize_attempts++;
		int n_width  = extra_x + t_window_width  - (t_window_width  % char_width);
		int n_height = extra_y + t_window_height - (t_window_height % char_height);
		yutani_window_resize_offer(yctx, window, n_width, n_height);
		return;
	}

	resize_attempts = 0;

	/* Accept new window size */
	yutani_window_resize_accept(yctx, window, width, height);
	window_width  = window->width  - extra_x;
	window_height = window->height - extra_y;

	/* Reinitialize the graphics library */
	reinit_graphics_yutani(ctx, window);

	/* Reinitialize the terminal buffer and ANSI library */
	reinit();
	maybe_flip_display(1);

	/* We are done resizing. */
	yutani_window_resize_done(yctx, window);
	yutani_flip(yctx, window);
}

/* Insert a mouse event sequence into the PTY */
static void mouse_event(int button, int x, int y) {
	if (ansi_state->mouse_on & TERMEMU_MOUSE_SGR) {
		char buf[100];
		sprintf(buf,"\033[<%d;%d;%d%c", button == 3 ? 0 : button, x+1, y+1, button == 3 ? 'm' : 'M');
		handle_input_s(buf);
	} else {
		char buf[7];
		sprintf(buf, "\033[M%c%c%c", button + 32, x + 33, y + 33);
		handle_input_s(buf);
	}
}

/* Handle Yutani messages */
static void * handle_incoming(void) {

	static uint64_t last_click = 0;

	yutani_msg_t * m = yutani_poll(yctx);
	while (m) {
		if (menu_process_event(yctx, m)) {
			render_decors();
		}
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
					yutani_window_t * win = hashmap_get(yctx->windows, (void*)(uintptr_t)wf->wid);
					if (win == window) {
						win->focused = wf->focused;
						render_decors();
						draw_cursor();
						maybe_flip_display(1);
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
					if (ansi_state->paste_mode) {
						handle_input_s("\033[200~");
						handle_input_s(selection_text);
						handle_input_s("\033[201~");
					} else {
						handle_input_s(selection_text);
					}
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

					if (me->new_x < 0 || me->new_y < 0 ||
						(!_no_frame && (me->new_x >= (int)window_width + (int)decor_width ||
							me->new_y < (int)decor_top_height+menu_bar_height ||
							me->new_y >= (int)(window_height + decor_top_height+menu_bar_height) ||
							me->new_x < (int)decor_left_width ||
							me->new_x >= (int)(window_width + decor_left_width))) ||
						(_no_frame && (me->new_x >= (int)window_width || me->new_y >= (int)window_height))) {
						if (window->mouse_state == YUTANI_CURSOR_TYPE_IBEAM) {
							yutani_window_show_mouse(yctx, window, YUTANI_CURSOR_TYPE_RESET);
						}
						break;
					}

					if (!(ansi_state->mouse_on & TERMEMU_MOUSE_ENABLE)) {
						if (window->mouse_state == YUTANI_CURSOR_TYPE_RESET) {
							yutani_window_show_mouse(yctx, window, YUTANI_CURSOR_TYPE_IBEAM);
						}
					} else {
						if (window->mouse_state == YUTANI_CURSOR_TYPE_IBEAM) {
							yutani_window_show_mouse(yctx, window, YUTANI_CURSOR_TYPE_RESET);
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
					if (new_x >= term_width || new_y >= term_height) break;

					/* Map Cursor Action */
					if ((ansi_state->mouse_on & TERMEMU_MOUSE_ENABLE) && !(me->modifiers & YUTANI_KEY_MODIFIER_SHIFT)) {

						if (me->buttons & YUTANI_MOUSE_SCROLL_UP) {
							mouse_event(32+32, new_x, new_y);
						} else if (me->buttons & YUTANI_MOUSE_SCROLL_DOWN) {
							mouse_event(32+32+1, new_x, new_y);
						}

						if (me->buttons != button_state) {
							/* Figure out what changed */
							if (me->buttons & YUTANI_MOUSE_BUTTON_LEFT &&
									!(button_state & YUTANI_MOUSE_BUTTON_LEFT))
								mouse_event(0, new_x, new_y);
							if (me->buttons & YUTANI_MOUSE_BUTTON_MIDDLE &&
									!(button_state & YUTANI_MOUSE_BUTTON_MIDDLE))
								mouse_event(1, new_x, new_y);
							if (me->buttons & YUTANI_MOUSE_BUTTON_RIGHT &&
									!(button_state & YUTANI_MOUSE_BUTTON_RIGHT))
								mouse_event(2, new_x, new_y);
							if (!(me->buttons & YUTANI_MOUSE_BUTTON_LEFT) &&
									button_state & YUTANI_MOUSE_BUTTON_LEFT)
								mouse_event(3, new_x, new_y);
							if (!(me->buttons & YUTANI_MOUSE_BUTTON_MIDDLE) &&
									button_state & YUTANI_MOUSE_BUTTON_MIDDLE)
								mouse_event(3, new_x, new_y);
							if (!(me->buttons & YUTANI_MOUSE_BUTTON_RIGHT) &&
									button_state & YUTANI_MOUSE_BUTTON_RIGHT)
								mouse_event(3, new_x, new_y);
							last_mouse_x = new_x;
							last_mouse_y = new_y;
							button_state = me->buttons;
						} else if (ansi_state->mouse_on & TERMEMU_MOUSE_DRAG) {
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
							redraw_scrollback();
							uint64_t now = get_ticks();
							if (now - last_click < 500000UL && (new_x == selection_start_x && new_y == selection_start_y)) {
								/* Double click */
								while (selection_start_x > 0) {
									term_cell_t * c = cell_at(selection_start_x-1, selection_start_y);
									if (!c || c->c == ' ' || !c->c) break;
									selection_start_x--;
								}
								while (selection_end_x < term_width - 1) {
									term_cell_t * c = cell_at(selection_end_x+1, selection_end_y);
									if (!c || c->c == ' ' || !c->c) break;
									selection_end_x++;
								}
								selection = 1;
							} else {
								last_click = get_ticks();
								selection_start_x = new_x;
								selection_start_y = new_y;
								selection_end_x = new_x;
								selection_end_y = new_y;
								selection = 0;
							}
							redraw_selection();
						}
						if (me->command == YUTANI_MOUSE_EVENT_DRAG && me->buttons & YUTANI_MOUSE_BUTTON_LEFT ){
							mark_selection();
							selection_end_x = new_x;
							selection_end_y = new_y;
							selection = 1;
							flip_selection();
						}
						if (me->command == YUTANI_MOUSE_EVENT_RAISE) {
							if (me->new_x == me->old_x && me->new_y == me->old_y) {
								selection = 0;
								term_redraw_all();
								redraw_scrollback();
							} /* else selection */
						}
						if (me->buttons & YUTANI_MOUSE_SCROLL_UP) {
							scroll_up(5);
						} else if (me->buttons & YUTANI_MOUSE_SCROLL_DOWN) {
							scroll_down(5);
						} else if (me->buttons & YUTANI_MOUSE_BUTTON_RIGHT) {
							if (!menu_right_click->window) {
								menu_prepare(menu_right_click, yctx);
								if (menu_right_click->window) {
									if (window->x + me->new_x + menu_right_click->window->width > yctx->display_width) {
										yutani_window_move(yctx, menu_right_click->window, window->x + me->new_x - menu_right_click->window->width, window->y + me->new_y);
									} else {
										yutani_window_move(yctx, menu_right_click->window, window->x + me->new_x, window->y + me->new_y);
									}
									yutani_flip(yctx, menu_right_click->window);
								}
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

/*
 * Menu Actions
 */

/* File > Exit */
static void _menu_action_exit(struct MenuEntry * self) {
	kill(child_pid, SIGKILL);
	exit_application = 1;
}

/* We need to track these so we can retitle both of them */
static struct MenuEntry * _menu_toggle_borders_context = NULL;
static struct MenuEntry * _menu_toggle_borders_bar = NULL;

static void _menu_action_hide_borders(struct MenuEntry * self) {
	_no_frame = !(_no_frame);
	update_bounds();
	window_width = window->width - decor_width;
	window_height = window->height - (decor_height + menu_bar_height);
	menu_update_icon(_menu_toggle_borders_context, _no_frame ? NULL : "check");
	menu_update_icon(_menu_toggle_borders_bar, _no_frame ? NULL : "check");
	reinit();
}

static struct MenuEntry * _menu_toggle_bitmap_context = NULL;
static struct MenuEntry * _menu_toggle_bitmap_bar = NULL;

static void _menu_action_toggle_tt(struct MenuEntry * self) {
	_use_aa = !(_use_aa);
	menu_update_icon(_menu_toggle_bitmap_context, _use_aa ? NULL : "check");
	menu_update_icon(_menu_toggle_bitmap_bar, _use_aa ? NULL : "check");
	reinit();
}

static void _menu_action_toggle_free_size(struct MenuEntry * self) {
	_free_size = !(_free_size);
	menu_update_icon(self, _free_size ? NULL : "check");
}

static void _menu_action_show_about(struct MenuEntry * self) {
	char about_cmd[1024] = "\0";
	strcat(about_cmd, "about \"About Terminal\" /usr/share/icons/48/utilities-terminal.png \"ToaruOS Terminal\" \"© 2013-2022 K. Lange\n-\nPart of ToaruOS, which is free software\nreleased under the NCSA/University of Illinois\nlicense.\n-\n%https://toaruos.org\n%https://github.com/klange/toaruos\" ");
	char coords[100];
	sprintf(coords, "%d %d &", (int)window->x + (int)window->width / 2, (int)window->y + (int)window->height / 2);
	strcat(about_cmd, coords);
	system(about_cmd);
	render_decors();
}

static void _menu_action_show_help(struct MenuEntry * self) {
	system("help-browser terminal.trt &");
	render_decors();
}

static void _menu_action_copy(struct MenuEntry * self) {
	copy_selection();
}

static void _menu_action_paste(struct MenuEntry * self) {
	yutani_special_request(yctx, NULL, YUTANI_SPECIAL_REQUEST_CLIPBOARD);
}

static void _menu_action_set_scale(struct MenuEntry * self) {
	struct MenuEntry_Normal * _self = (struct MenuEntry_Normal *)self;
	if (!_self->action) {
		scale_fonts  = 0;
		font_scaling = 1.0;
	} else {
		scale_fonts  = 1;
		font_scaling = atof(_self->action);
	}
	reinit();
}

static void render_decors_callback(struct menu_bar * self) {
	(void)self;
	render_decors();
}

/**
 * Geometry argument follows this format:
 *    [@]WxH[+X,Y]
 *
 * If @ is present, W and H are in characters.
 * If + is present, X and Y are the left and top offset.
 */
static void parse_geometry(char ** argv, char * str) {

	int in_chars = 0;
	if (*str == '@') {
		in_chars = 1;
		str++;
	}

	/* Split on 'x', which is required. */
	char * c = strstr(str, "x");
	if (!c) return; /* Ignore invalid arg */
	*c = '\0';
	c++;

	/* Find optional + that starts position */
	char * plus = strstr(c, "+");
	if (plus) {
		*plus = '\0';
		plus++;
	}

	/* Parse size */
	window_width = atoi(str) * (in_chars ? char_width : 1);
	window_height = atoi(c) * (in_chars ? char_height : 1);

	if (plus) {
		/* If there was a plus, let's look for a comma */
		char * comma = strstr(plus, ",");
		if (!comma) return; /* Skip invalid position */
		*comma = '\0';
		comma++;

		window_position_set = 1;
		window_left = atoi(plus);
		window_top  = atoi(comma);
	}
}

int main(int argc, char ** argv) {

	int _flags = 0;
	window_width  = char_width * 80;
	window_height = char_height * 24;

	static struct option long_opts[] = {
		{"fullscreen", no_argument,       0, 'F'},
		{"bitmap",     no_argument,       0, 'b'},
		{"scale",      required_argument, 0, 's'},
		{"help",       no_argument,       0, 'h'},
		{"grid",       no_argument,       0, 'x'},
		{"no-frame",   no_argument,       0, 'n'},
		{"geometry",   required_argument, 0, 'g'},
		{"blurred",    no_argument,       0, 'B'},
		{"scrollback", required_argument, 0, 'S'},
		{0,0,0,0}
	};

	/* Read some arguments */
	int index, c;
	while ((c = getopt_long(argc, argv, "bhxnFls:g:BS:", long_opts, &index)) != -1) {
		if (!c) {
			if (long_opts[index].flag == 0) {
				c = long_opts[index].val;
			}
		}
		switch (c) {
			case 'x':
				_free_size = 0;
				break;
			case 'n':
				_no_frame = 1;
				break;
			case 'F':
				_fullscreen = 1;
				_no_frame = 1;
				break;
			case 'b':
				_use_aa = 0;
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
				parse_geometry(argv,optarg);
				break;
			case 'B':
				_flags = YUTANI_WINDOW_FLAG_BLUR_BEHIND;
				break;
			case 'S':
				max_scrollback = strtoull(optarg,NULL,10);
				break;
			case '?':
				break;
			default:
				break;
		}
	}

	/* Initialize the windowing library */
	yctx = yutani_init();

	if (!yctx) {
		fprintf(stderr, "%s: failed to connect to compositor\n", argv[0]);
		return 1;
	}

	_tt_font_normal       = tt_font_from_shm("monospace");
	_tt_font_bold         = tt_font_from_shm("monospace.bold");
	_tt_font_oblique      = tt_font_from_shm("monospace.italic");
	_tt_font_bold_oblique = tt_font_from_shm("monospace.bolditalic");
	_tt_font_fallback     = _tt_font_normal;
	_tt_font_japanese     = tt_font_from_file("/usr/share/fonts/truetype/vlgothic/VL-Gothic-Regular.ttf"); /* Might not be present */

	/* Full screen mode forces window size to be that the display server */
	if (_fullscreen) {
		window_width = yctx->display_width;
		window_height = yctx->display_height;
	}

	if (_no_frame) {
		window = yutani_window_create_flags(yctx, window_width, window_height, YUTANI_WINDOW_FLAG_NO_ANIMATION);
	} else {
		init_decorations();
		struct decor_bounds bounds;
		decor_get_bounds(NULL, &bounds);
		window = yutani_window_create_flags(yctx, window_width + bounds.width, window_height + bounds.height + menu_bar_height, _flags);
		yutani_window_update_shape(yctx, window, 20);
	}

	if (_fullscreen) {
		/* If fullscreen, assume we're always focused and put us on the bottom. */
		yutani_set_stack(yctx, window, YUTANI_ZORDER_BOTTOM);
		window->focused = 1;
	} else {
		window->focused = 0;
	}

	update_bounds();

	/* Set up menus */
	terminal_menu_bar.entries = terminal_menu_entries;
	terminal_menu_bar.redraw_callback = render_decors_callback;

	struct MenuEntry * _menu_exit = menu_create_normal("exit","exit","Exit", _menu_action_exit);
	struct MenuEntry * _menu_copy = menu_create_normal(NULL, NULL, "Copy", _menu_action_copy);
	struct MenuEntry * _menu_paste = menu_create_normal(NULL, NULL, "Paste", _menu_action_paste);

	menu_right_click = menu_create();
	menu_insert(menu_right_click, _menu_copy);
	menu_insert(menu_right_click, _menu_paste);
	menu_insert(menu_right_click, menu_create_separator());
	if (!_fullscreen) {
		_menu_toggle_borders_context = menu_create_normal(_no_frame ? NULL : "check", NULL, "Show borders", _menu_action_hide_borders);
		menu_insert(menu_right_click, _menu_toggle_borders_context);
	}
	_menu_toggle_bitmap_context = menu_create_normal(_use_aa ? NULL : "check", NULL, "Bitmap font", _menu_action_toggle_tt);
	menu_insert(menu_right_click, _menu_toggle_bitmap_context);
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
	menu_insert(m, menu_create_normal(NULL, "0.75", "75%", _menu_action_set_scale));
	menu_insert(m, menu_create_normal(NULL, NULL, "100%", _menu_action_set_scale));
	menu_insert(m, menu_create_normal(NULL, "1.5", "150%", _menu_action_set_scale));
	menu_insert(m, menu_create_normal(NULL, "2.0", "200%", _menu_action_set_scale));
	menu_set_insert(terminal_menu_bar.set, "zoom", m);

	m = menu_create();
	menu_insert(m, menu_create_normal(NULL, NULL, "View stats", _menu_action_cache_stats));
	menu_insert(m, menu_create_normal(NULL, NULL, "Clear cache", _menu_action_clear_cache));
	menu_set_insert(terminal_menu_bar.set, "cache", m);

	m = menu_create();
	_menu_toggle_borders_bar = menu_create_normal(_no_frame ? NULL : "check", NULL, "Show borders", _menu_action_hide_borders);
	menu_insert(m, _menu_toggle_borders_bar);
	menu_insert(m, menu_create_submenu(NULL,"zoom","Set zoom..."));
	_menu_toggle_bitmap_bar = menu_create_normal(_use_aa ? NULL : "check", NULL, "Bitmap font", _menu_action_toggle_tt);
	menu_insert(m, _menu_toggle_bitmap_bar);
	menu_insert(m, menu_create_normal(_free_size ? NULL : "check", NULL, "Snap to Cell Size", _menu_action_toggle_free_size));
	menu_insert(m, menu_create_separator());
	menu_insert(m, menu_create_normal(NULL, NULL, "Redraw", _menu_action_redraw));
	menu_insert(m, menu_create_submenu(NULL,"cache","Glyph cache..."));
	menu_set_insert(terminal_menu_bar.set, "view", m);

	m = menu_create();
	menu_insert(m, menu_create_normal("help","help","Contents", _menu_action_show_help));
	menu_insert(m, menu_create_separator());
	menu_insert(m, menu_create_normal("star","star","About Terminal", _menu_action_show_about));
	menu_set_insert(terminal_menu_bar.set, "help", m);

	scrollback_list = list_create();
	images_list = list_create();

	/* Initialize the graphics context */
	ctx = init_graphics_yutani_double_buffer(window);

	/* Clear to black */
	draw_fill(ctx, rgba(0,0,0,0));

	if (window_position_set) {
		/* Move to requested position */
		yutani_window_move(yctx, window, window_left, window_top);
	} else {
		/* Move window to screen center */
		yutani_window_move(yctx, window, yctx->display_width / 2 - window->width / 2, yctx->display_height / 2 - window->height / 2);
	}

	/* Open a PTY */
	openpty(&fd_master, &fd_slave, NULL, NULL, NULL);
	terminal = fdopen(fd_slave, "w");

	/* Initialize the terminal buffer and ANSI library for the first time. */
	reinit();

	/* Run thread to handle asynchronous writes to the tty */
	pthread_t input_buffer_thread;
	pipe(input_buffer_semaphore);
	input_buffer_queue = list_create();
	pthread_create(&input_buffer_thread, NULL, handle_input_writing, NULL);

	/* Make sure we're not passing anything to stdin on the child */
	fflush(stdin);

	/* Fork off child */
	child_pid = fork();

	if (!child_pid) {
		setsid();
		/* Prepare stdin/out/err */
		dup2(fd_slave, 0);
		dup2(fd_slave, 1);
		dup2(fd_slave, 2);

		ioctl(STDIN_FILENO, TIOCSCTTY, &(int){1});
		tcsetpgrp(STDIN_FILENO, getpid());

		/* Set the TERM environment variable. */
		putenv("TERM=toaru");

		/* Execute requested initial process */
		if (argv[optind] != NULL) {
			/* Run something specified by the terminal startup */
			execvp(argv[optind], &argv[optind]);
			fprintf(stderr, "Failed to launch requested startup application.\n");
		} else {
			/* Run the user's shell */
			char * shell = getenv("SHELL");
			if (!shell) shell = "/bin/sh"; /* fallback */
			char * tokens[] = {shell,NULL};
			execvp(tokens[0], tokens);
			exit(1);
		}

		/* Failed to start */
		exit_application = 1;
		return 1;
	} else {

		/* Set up fswait to check Yutani and the PTY master */
		int fds[2] = {fileno(yctx->sock), fd_master};

		/* PTY read buffer */
		unsigned char buf[4096];
		int next_wait = 200;

		while (!exit_application) {

			/* Wait for something to happen. */
			int res[] = {0,0};
			fswait3(2,fds,next_wait,res);

			/* Check if the child application has closed. */
			check_for_exit();
			maybe_flip_cursor();

			int force_flip = (!res[1] && (next_wait == 10));

			if (res[1]) {
				/* Read from PTY */
				ssize_t r = read(fd_master, buf, 4096);
				for (ssize_t i = 0; i < r; ++i) {
					ansi_put(ansi_state, buf[i]);
				}
				next_wait = 10;
			} else {
				next_wait = 200;
			}
			if (res[0]) {
				/* Handle Yutani events. */
				handle_incoming();
			}
			maybe_flip_display(force_flip);
		}
	}

	close(input_buffer_semaphore[1]);

	/* Windows will close automatically on exit. */
	return 0;
}
