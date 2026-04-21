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
 * Copyright (C) 2013-2026 K. Lange
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
static int usage(char * argv[]) {
#define X_S "\033[3m"
#define X_E "\033[0m"
	fprintf(stderr,
			"Terminal Emulator\n"
			"\n"
			"usage: %s [-FbexnB] [-s " X_S "SCALE" X_E "] [-g " X_S "WIDTHxHEIGHT" X_E "] [-S " X_S "LINES" X_E "] [COMMAND...]\n"
			"\n"
			" -F --fullscreen        " X_S "Run in fullscreen (background) mode." X_E "\n"
			" -b --bitmap            " X_S "Use the integrated bitmap font." X_E "\n"
			" -e --emulatebold       " X_S "Emulate bold text by double striking bitmap glyphs." X_E "\n"
			" -s --scale " X_S "SCALE       Scale the font in antialiased mode by a given amount." X_E "\n"
			" -h --help              " X_S "Show this help message." X_E "\n"
			" -x --grid              " X_S "Make resizes round to nearest match for character cell size." X_E "\n"
			" -n --no-frame          " X_S "Disable decorations." X_E "\n"
			" -g --geometry " X_S "WxH      Set requested terminal size WIDTHxHEIGHT" X_E "\n"
			" -B --blurred           " X_S "Blur background behind terminal." X_E "\n"
			" -S --scrollback " X_S "LINES  Set the scrollback buffer size, 0 for unlimited." X_E "\n"
			"\n"
			" This terminal emulator provides basic support for VT220 escapes and\n"
			" XTerm extensions, including 256 color support and font effects.\n",
			argv[0]);
	return 1;
}

struct input_data {
	size_t len;
	char data[];
};

#define TERMINAL_TITLE_SIZE 512
struct Terminal_Private {
	int   scale_fonts;
	float font_scaling;
	uint16_t font_size;
	uint16_t char_width;
	uint16_t char_height;
	uint16_t char_offset;
	uint16_t extra_right;
	uint16_t extra_bottom;
	char   terminal_title[TERMINAL_TITLE_SIZE];
	size_t terminal_title_length;

	pthread_t input_buffer_thread;
	volatile int input_buffer_lock;
	int input_buffer_semaphore[2];
	list_t * input_buffer_queue;

	int fd_master, fd_slave;
	pid_t child_pid;

	char tab_title[TERMINAL_TITLE_SIZE]; /* TODO just gonna fill in numbers for now */

	list_t * images_list;

	int use_truetype;
	int emulate_bold;
};

static list_t * terminals = NULL;
static term_state_t * active_terminal = NULL;

static term_state_t * current_terminal(void) {
	return active_terminal;
}

static term_state_t * terminal_create(int scale_fonts, float font_scaling, int max_scrollback, int use_truetype, int emulate_bold, int argc, char * argv[]);

#define this_term() ((struct Terminal_Private*)current_terminal()->priv)

static bool _fullscreen    = 0;    /* Whether or not we are running in fullscreen mode (GUI only) */
static bool _no_frame      = 0;    /* Whether to disable decorations or not */
static bool _free_size     = 1;    /* Disable rounding when resized */

static bool terminal_login_shell_restricted = 0;

static struct TT_Font * _tt_font_normal = NULL;
static struct TT_Font * _tt_font_bold = NULL;
static struct TT_Font * _tt_font_oblique = NULL;
static struct TT_Font * _tt_font_bold_oblique = NULL;

static struct TT_Font * _tt_font_fallback = NULL;
static struct TT_Font * _tt_font_japanese = NULL;

static int menu_bar_height = 24;

/* Text selection information */

static char * selection_text = NULL;
static size_t _selection_count = 0;
static size_t _selection_space = 0;
static int _selection_escapes = 0;
static term_cell_t _selection_state = {0};

/* Mouse state */
static int last_mouse_x   = -1;
static int last_mouse_y   = -1;
static int button_state   = 0;

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
static gfx_context_t * ctx;
static struct MenuList * menu_right_click = NULL;

static void render_decors(void);
static void reinit(void);
static void update_menu_bar_tabs(void);

static int decor_left_width = 0;
static int decor_top_height = 0;
static int decor_right_width = 0;
static int decor_bottom_height = 0;
static int decor_width = 0;
static int decor_height = 0;

/* Menu bar entries */
struct menu_bar_with_tabs terminal_menu_bar = {0};
struct menu_bar_entries terminal_menu_entries[] = {
	{"File", "file"},
	{"Edit", "edit"},
	{"View", "view"},
	{"Terminal", "terminal"},
	{"Help", "help"},
	{NULL, NULL},
};
struct menu_bar_entries * terminal_menu_bar_with_tabs = NULL;

/* We need to track these so we can update their states*/
static struct MenuEntry * _menu_toggle_borders_context = NULL;
static struct MenuEntry * _menu_toggle_borders_bar = NULL;
static struct MenuEntry * _menu_exit = NULL;
static struct MenuEntry * _menu_copy = NULL;
static struct MenuEntry * _menu_copy_escapes = NULL;
static struct MenuEntry * _menu_paste = NULL;
static struct MenuEntry * _menu_scale_075 = NULL;
static struct MenuEntry * _menu_scale_100 = NULL;
static struct MenuEntry * _menu_scale_150 = NULL;
static struct MenuEntry * _menu_scale_200 = NULL;
static struct MenuEntry * _menu_set_zoom = NULL;
static struct MenuEntry * _menu_new_tab = NULL;

/* Terminal state menu */
static struct MenuEntry * _menu_toggle_altscreen = NULL;
static struct MenuEntry * _menu_toggle_mouse_reporting = NULL;
static struct MenuEntry * _menu_toggle_mouse_drag = NULL;
static struct MenuEntry * _menu_toggle_mouse_sgr = NULL;
static struct MenuEntry * _menu_toggle_mouse_altscroll = NULL;
static struct MenuEntry * _menu_toggle_paste_bracketing = NULL;

/* Trigger to exit the terminal when the child process dies or
 * we otherwise receive an exit signal */
static volatile int exit_application = 0;

static void update_bounds(void);
static void update_scale_menu(void);
static void update_font_menu_states(void);

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

/* Set the terminal title string */
static void set_title(term_state_t * state, char * c) {
	struct Terminal_Private * term = state->priv;
	int len = min(TERMINAL_TITLE_SIZE, strlen(c)+1);
	memcpy(term->terminal_title, c, len);
	term->terminal_title[len-1] = '\0';
	term->terminal_title_length = len - 1;
	update_menu_bar_tabs();
	render_decors();
}


static void selection_extend(char * str, size_t len) {
	while (_selection_count + len >= _selection_space) {
		_selection_space *= 2;
		selection_text = realloc(selection_text, _selection_space);
	}

	for (size_t i = 0; i < len; ++i) {
		selection_text[_selection_count++] = str[i];
	}
}

static void selection_write_cell(term_cell_t * cell) {
	if (!(cell->flags & ANSI_EXT_IMG)) {
		if (((uint32_t *)cell)[0] != 0x00000000 && cell->c != 0xFFFF) {

			if (_selection_escapes) {
				if (cell->fg != _selection_state.fg) {
					char * tmp;
					if (cell->fg < 8) {
						asprintf(&tmp, "\033[3%dm", cell->fg);
					} else if (cell->fg < 16) {
						asprintf(&tmp, "\033[9%dm", cell->fg - 8);
					} else if (cell->fg < PALETTE_COLORS) {
						asprintf(&tmp, "\033[38;5;%dm", cell->fg);
					} else {
						int red = _RED(cell->fg);
						int gre = _GRE(cell->fg);
						int blu = _BLU(cell->fg);
						asprintf(&tmp, "\033[38;2;%d;%d;%dm", red, gre, blu);
					}
					selection_extend(tmp, strlen(tmp));
					free(tmp);
				}
				if (cell->bg != _selection_state.bg) {
					char * tmp;
					if (cell->bg < 8) {
						asprintf(&tmp, "\033[4%dm", cell->bg);
					} else if (cell->bg < 16) {
						asprintf(&tmp, "\033[10%dm", cell->bg - 8);
					} else if (cell->bg < PALETTE_COLORS) {
						asprintf(&tmp, "\033[48;5;%dm", cell->bg);
					} else {
						int red = _RED(cell->bg);
						int gre = _GRE(cell->bg);
						int blu = _BLU(cell->bg);
						asprintf(&tmp, "\033[48;2;%d;%d;%dm", red, gre, blu);
					}
					selection_extend(tmp, strlen(tmp));
					free(tmp);
				}
				if (cell->flags != _selection_state.flags) {
					uint32_t changed = (cell->flags ^ _selection_state.flags);

					if (changed & ANSI_BOLD) {
						if (cell->flags & ANSI_BOLD) selection_extend("\033[1m", 4);
						else selection_extend("\033[22m", 5);
					}

					if (changed & ANSI_ITALIC) {
						if (cell->flags & ANSI_ITALIC) selection_extend("\033[3m", 4);
						else selection_extend("\033[23m", 5);
					}

					if (changed & ANSI_UNDERLINE) {
						if (cell->flags & ANSI_UNDERLINE) selection_extend("\033[4m", 4);
						else selection_extend("\033[24m", 5);
					}

					if (changed & ANSI_CROSS) {
						if (cell->flags & ANSI_CROSS) selection_extend("\033[9m", 4);
						else selection_extend("\033[29m", 5);
					}

					if (changed & ANSI_INVERT) {
						if (cell->flags & ANSI_INVERT) selection_extend("\033[7m", 4);
						else selection_extend("\033[27m", 5);
					}

				}
			}

			char tmp[7];
			int count = termemu_to_eight(cell->c, tmp);
			selection_extend(tmp, count);

			_selection_state.fg = cell->fg;
			_selection_state.bg = cell->bg;
			_selection_state.flags = cell->flags;
		}
	}
}

/* Fill the selection text buffer with the selected text. */
void write_selection(term_state_t * state, uint16_t x, uint16_t _y) {
	term_cell_t * cell = termemu_cell_at(state, x, _y);
	selection_write_cell(cell);
	if (x == state->width - 1) selection_extend("\n",1);
}

/* Copy the selection text to the clipboard. */
static char * copy_selection(int with_escapes) {
	if (selection_text) free(selection_text);

	_selection_count = 0;
	_selection_space = 10;
	selection_text = malloc(_selection_space);

	_selection_escapes = with_escapes;
	memset(&_selection_state, 0, sizeof(term_cell_t));
	termemu_iterate_selection(current_terminal(), write_selection);
	selection_extend("\0",1);
	_selection_count--;

	if (_selection_count && selection_text[_selection_count-1] == '\n') {
		/* Don't end on a line feed */
		selection_text[_selection_count-1] = '\0';
	}

	yutani_set_clipboard(yctx, selection_text);
	return selection_text;
}

void * handle_input_writing(void * _state) {
	term_state_t * my_state = _state;
	struct Terminal_Private * my_term = my_state->priv;

	while (1) {

		/* Read one byte from semaphore; as long as semaphore has data,
		 * there is another input blob to write to the TTY */
		char tmp[1];
		int c = read(my_term->input_buffer_semaphore[0],tmp,1);
		if (c > 0) {
			/* Retrieve blob */
			spin_lock(&my_term->input_buffer_lock);
			node_t * blob = list_dequeue(my_term->input_buffer_queue);
			spin_unlock(&my_term->input_buffer_lock);
			/* No blobs? This shouldn't happen, but just in case, just continue */
			if (!blob) {
				continue;
			}
			/* Write blob data to the tty */
			struct input_data * value = blob->value;
			write(my_term->fd_master, value->data, value->len);
			free(blob->value);
			free(blob);
		} else {
			/* The pipe has closed, terminal is exiting */
			break;
		}
	}

	return NULL;
}

static void write_input_buffer(term_state_t * state, char * data, size_t len) {
	struct Terminal_Private * priv = state->priv;
	struct input_data * d = malloc(sizeof(struct input_data) + len);
	d->len = len;
	memcpy(&d->data, data, len);
	spin_lock(&priv->input_buffer_lock);
	list_insert(priv->input_buffer_queue, d);
	spin_unlock(&priv->input_buffer_lock);
	write(priv->input_buffer_semaphore[1], d, 1);
}

/* Stuffs a string into the stdin of the terminal's child process
 * Useful for things like the ANSI DSR command. */
static void input_buffer_stuff(term_state_t * state, char * str) {
	size_t len = strlen(str);
	write_input_buffer(state, str, len);
}

static void tab_callback(struct menu_bar_with_tabs * menu, struct menu_bar_entries * entry) {
	int tab = atoi(entry->title+1);
	int i = 1;
	foreach (node, terminals) {
		if (i == tab) {
			active_terminal = node->value;
			update_menu_bar_tabs();
			reinit();
			return;
		}
		i++;
	}
}

static void update_menu_bar_tabs(void) {
	terminal_menu_bar_with_tabs = realloc(terminal_menu_bar_with_tabs, sizeof(terminal_menu_entries) + sizeof(struct menu_bar_entries) * terminals->length);
	memcpy(terminal_menu_bar_with_tabs, &terminal_menu_entries, sizeof(terminal_menu_entries));
	terminal_menu_bar._super.entries = terminal_menu_bar_with_tabs;
	terminal_menu_bar.tab_callback = tab_callback;
	if (terminals->length == 1) return;

	struct menu_bar_entries * entry;
	for (entry = terminal_menu_bar_with_tabs; entry->title; entry++);
	int i = 1;
	foreach (node, terminals) {
		term_state_t * state = node->value;
		struct Terminal_Private * priv = state->priv;
		snprintf(priv->tab_title, TERMINAL_TITLE_SIZE,
			state == current_terminal() ? "\v%d" : "\t%d", i);
		entry->title = priv->tab_title;
		entry->action = "tab";
		entry++;
		i++;
	}
	entry->title = NULL;
}

/* Redraw the decorations */
static void render_decors(void) {
	/* Don't draw decorations or bother advertising the window if in "fullscreen mode" */
	if (_fullscreen) return;

	if (!_no_frame) {
		/* Draw the decorations */
		render_decorations(window, ctx, this_term()->terminal_title_length ? this_term()->terminal_title : "Terminal");
		/* Update menu bar position and size */
		terminal_menu_bar._super.x = decor_left_width;
		terminal_menu_bar._super.y = decor_top_height;
		terminal_menu_bar._super.width = window_width;
		terminal_menu_bar._super.window = window;
		/* Redraw the menu bar */
		menu_bar_render((struct menu_bar*)&terminal_menu_bar, ctx);
	}

	/* Advertise the window icon to the panel. */
	yutani_window_advertise_icon(yctx, window, this_term()->terminal_title_length ? this_term()->terminal_title : "Terminal", "utilities-terminal");

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
static void draw_semi_block(term_state_t * state, int c, int x, int y, uint32_t fg, uint32_t bg) {
	struct Terminal_Private * term = state->priv;

	bg = premultiply(bg);
	fg = alpha_blend_rgba(bg, premultiply(fg));
	_fill_region(bg, x, y, term->char_width, term->char_height);
	if (c == 0x2580) {
		for (uint8_t i = 0; i < term->char_height / 2; ++i) {
			for (uint8_t j = 0; j < term->char_width; ++j) {
				term_set_point(x+j,y+i,fg);
			}
		}
	} else if (c >= 0x2589) {
		c -= 0x2588;
		int width = term->char_width - ((c * term->char_width) / 8);
		for (uint8_t i = 0; i < term->char_height; ++i) {
			for (uint8_t j = 0; j < width; ++j) {
				term_set_point(x+j, y+i, fg);
			}
		}
	} else {
		c -= 0x2580;
		int height = term->char_height - ((c * term->char_height) / 8);
		for (uint8_t i = height; i < term->char_height; ++i) {
			for (uint8_t j = 0; j < term->char_width; ++j) {
				term_set_point(x+j, y+i,fg);
			}
		}
	}
}

static void draw_box_drawing(term_state_t * state, int c, int x, int y, uint32_t fg, uint32_t bg) {
	struct Terminal_Private * term = state->priv;
	bg = premultiply(bg);
	fg = alpha_blend_rgba(bg, premultiply(fg));
	_fill_region(bg, x, y, term->char_width, term->char_height);

	int lineheight = term->char_height / 16;
	int linewidth = term->char_width / 8;

	lineheight = lineheight < 1 ? 1 : lineheight;
	linewidth = linewidth < 1 ? 1 : linewidth;

	int mid_x = term->char_width / 2 - linewidth / 2;
	int mid_y = term->char_height / 2 - lineheight / 2;
	int extra_x = (mid_x * 2 < term->char_width) ? term->char_width - mid_x * 2 : 0;
	int extra_y = (mid_y * 2 < term->char_height) ? term->char_height - mid_y * 2 : 0;

#define UP    _fill_region(fg, x + mid_x, y, linewidth, mid_y + lineheight)
#define DOWN  _fill_region(fg, x + mid_x, y + mid_y, linewidth, mid_y + extra_y)
#define LEFT  _fill_region(fg, x, y + mid_y, mid_x + linewidth, lineheight)
#define RIGHT _fill_region(fg, x + mid_x, y + mid_y, mid_x + extra_x, lineheight)
#define VERT  _fill_region(fg, x + mid_x, y, linewidth, term->char_height)
#define HORI  _fill_region(fg, x, y + mid_y, term->char_width, lineheight)

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

	write(this_term()->fd_slave, msg, strlen(msg));
}

static void _menu_action_clear_cache(struct MenuEntry * self) {
	for (int i = 0; i < 1024; ++i) {
		if (glyph_cache[i].sprite) {
			sprite_free(glyph_cache[i].sprite);
		}
	}
	memset(glyph_cache,0,sizeof(glyph_cache));
}

static void draw_cached_glyph(term_state_t * state, gfx_context_t * ctx, struct TT_Font * _font, uint32_t size, int x, int y, uint32_t glyph, uint32_t fg, int flags) {
	struct Terminal_Private * term = state->priv;
	unsigned int hash = (((uintptr_t)_font >> 8) ^ (glyph * size)) & 1023;

	struct GlyphCacheEntry * entry = &glyph_cache[hash];

	if (entry->font != _font || entry->size != size || entry->glyph != glyph) {
		if (entry->sprite) sprite_free(entry->sprite);
		int wide = (flags & ANSI_WIDE) ? 2 : 1;
		tt_set_size(_font, size);

		entry->font = _font;
		entry->size = size;
		entry->glyph = glyph;
		entry->sprite = create_sprite(term->char_width * wide, term->char_height, ALPHA_EMBEDDED);
		entry->color = _ALP(fg) == 255 ? fg : 0xFFFFFFFF;

		gfx_context_t * _ctx = init_graphics_sprite(entry->sprite);
		draw_fill(_ctx, 0);
		tt_draw_glyph(_ctx, entry->font, 0, term->char_offset, glyph, entry->color);
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
static void term_write_char(term_state_t * state, uint32_t val, uint16_t x, uint16_t y, uint32_t fg, uint32_t bg, uint32_t flags, uint16_t ax, uint16_t ay) {
	struct Terminal_Private * term = state->priv;
	uint32_t _fg, _bg;

	if (flags & ANSI_INVERT) {
		uint32_t _tmp = fg;
		fg = bg;
		bg = _tmp;
		flags |= ANSI_SPECBG; /* The background is modified, so it can't be default. */
	}

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

	int wide = !!(flags & ANSI_WIDE);
	int exr = (ax + 1 + wide == state->width);
	int exb = (ay + 1 == state->height);
	uint32_t fill_color = (flags & ANSI_INVERTED) ? _fg : _bg;
	if (exr && exb) _fill_region(fill_color, x + term->char_width * (wide + 1), y + term->char_height, term->extra_right, term->extra_bottom);
	if (exr) _fill_region(fill_color, x + term->char_width * (wide + 1), y, term->extra_right, term->char_height);
	if (exb) _fill_region(fill_color, x, y + term->char_height, term->char_width * (wide + 1), term->extra_bottom);

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
			draw_box_drawing(state, val, x, y, _fg, _bg);
			goto _extra_stuff;

		/* Semi-filled blocks */
		case 0x2580 ... 0x258f: {
			draw_semi_block(state, val, x, y, _fg, _bg);
			goto _extra_stuff;
		}

		/* Instead of checker, does 50% opacity fill */
		case 0x2591:
		case 0x2592:
		case 0x2593:
			_fill_region(alpha_blend_rgba(premultiply(_bg), interp_colors(rgb(0,0,0), premultiply(_fg), 255 * (val - 0x2590) / 4)), x, y, term->char_width, term->char_height);
			goto _extra_stuff;


		default:
			break;
	}

	/* Draw glyphs */
	if (term->use_truetype) {
		if (val == 0xFFFF) return;
		for (uint8_t i = 0; i < term->char_height; ++i) {
			for (uint8_t j = 0; j < term->char_width; ++j) {
				term_set_point(x+j,y+i,_bg);
			}
		}
		if (wide) {
			for (uint8_t i = 0; i < term->char_height; ++i) {
				for (uint8_t j = term->char_width; j < 2 * term->char_width; ++j) {
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
		draw_cached_glyph(state, ctx, _font, term->font_size, _x,_y, glyph, _fg, flags);
	} else {
		/* Convert other unicode characters. */
		if (val > 128) {
			val = ununicode(val);
		}
		/* Draw using the bitmap font. */
		uint8_t * c = large_font[val];
#define bit_set(i,j) (c[i] & (1 << (LARGE_FONT_MASK-(j))))
		for (uint8_t i = 0; i < term->char_height; ++i) {
			for (uint8_t j = 0; j < term->char_width; ++j) {
				if (bit_set(i,j) || ((flags & ANSI_BOLD) && term->emulate_bold && bit_set(i,j+1))) {
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
		for (uint8_t i = 0; i < term->char_width; ++i) {
			term_set_point(x + i, y + term->char_height - 1, _fg);
		}
	}
	if (flags & ANSI_CROSS) {
		for (uint8_t i = 0; i < term->char_width; ++i) {
			term_set_point(x + i, y + term->char_height - 7, _fg);
		}
	}
	if (flags & ANSI_BORDER) {
		for (uint8_t i = 0; i < term->char_height; ++i) {
			term_set_point(x , y + i, _fg);
			term_set_point(x + (term->char_width - 1), y + i, _fg);
		}
		for (uint8_t j = 0; j < term->char_width; ++j) {
			term_set_point(x + j, y, _fg);
			term_set_point(x + j, y + (term->char_height - 1), _fg);
		}
	}

	/* Calculate the bounds of the updated region of the window */
	l_x = min(l_x, decor_left_width + x);
	l_y = min(l_y, decor_top_height+menu_bar_height + y);

	if (wide) {
		r_x = max(r_x, decor_left_width + x + term->char_width * 2 + (exr ? term->extra_right : 0));
		r_y = max(r_y, decor_top_height+menu_bar_height + y + term->char_height * 2 + (exb ? term->extra_bottom : 0));
	} else {
		r_x = max(r_x, decor_left_width + x + term->char_width + (exr ? term->extra_right : 0));
		r_y = max(r_y, decor_top_height+menu_bar_height + y + term->char_height + (exb ? term->extra_bottom : 0));
	}
}


/* Redraw an embedded image cell */
static void redraw_cell_image(term_state_t * state, uint16_t x, uint16_t y, term_cell_t * cell, int inverted) {
	struct Terminal_Private * term = state->priv;
	/* Avoid setting cells out of range. */
	if (x >= state->width || y >= state->height) return;

	/* Draw the image data */
	uint32_t * data = (uint32_t *)((uintptr_t)cell->bg << 32 | cell->fg);
	if (inverted) {
		for (uint32_t yy = 0; yy < term->char_height; ++yy) {
			for (uint32_t xx = 0; xx < term->char_width; ++xx) {
				/* Extract */
				uint32_t a = _ALP(*data);
				uint32_t r = _RED(*data);
				uint32_t g = _GRE(*data);
				uint32_t b = _BLU(*data);
				/* Unpremultiply */
				if (a) {
					r *= 255 / a;
					g *= 255 / a;
					b *= 255 / a;
				}
				/* Invert colors */
				r = 0xFF - r;
				g = 0xFF - g;
				b = 0xFF - b;
				/* Premultiply again */
				uint32_t color = premultiply(rgba(r,g,b,a));
				term_set_point(x * term->char_width + xx, y * term->char_height + yy, color);
				data++;
			}
		}
	} else {
		for (uint32_t yy = 0; yy < term->char_height; ++yy) {
			for (uint32_t xx = 0; xx < term->char_width; ++xx) {
				term_set_point(x * term->char_width + xx, y * term->char_height + yy, *data);
				data++;
			}
		}
	}

	/* Update bounds */
	l_x = min(l_x, decor_left_width + x * term->char_width);
	l_y = min(l_y, decor_top_height+menu_bar_height + y * term->char_height);
	r_x = max(r_x, decor_left_width + x * term->char_width + term->char_width);
	r_y = max(r_y, decor_top_height+menu_bar_height + y * term->char_height + term->char_height);
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

	term_state_t * state = current_terminal();
	struct Terminal_Private * term = state->priv;

	for (int y = 0; y < state->height; ++y) {
		for (int x = 0; x < state->width; ++x) {
			term_cell_t * cell_m = &state->term_mirror[y * state->width + x];
			term_cell_t * cell_d = &state->term_display[y * state->width + x];
			if (memcmp(cell_m, cell_d, sizeof(term_cell_t))) {
				*cell_d = *cell_m;
				if (cell_m->flags & ANSI_EXT_IMG) {
					redraw_cell_image(state, x,y,cell_m,cell_m->flags & ANSI_INVERTED);
				} else {
					term_write_char(state, cell_m->c, x * term->char_width, y * term->char_height, cell_m->fg, cell_m->bg, cell_m->flags, x, y);
				}
			}
		}
	}
	display_flip();
}

static void _menu_action_redraw(struct MenuEntry * self) {
	termemu_redraw_all(current_terminal());
}

/* Remove no-longer-visible image cell data. */
static void flush_unused_images(term_state_t * state) {
	struct Terminal_Private * term = state->priv;
	if (!term->images_list->length) return;

	list_t * tmp = list_create();

	/* Go through scrollback, too */
	if (state->scrollback->scrollback_list) {
		foreach(node, state->scrollback->scrollback_list) {
			struct TermemuScrollbackRow * row = (struct TermemuScrollbackRow *)node->value;
			for (unsigned int x = 0; x < row->width; ++x) {
				term_cell_t * cell = &row->cells[x];
				if (cell->flags & ANSI_EXT_IMG) {
					uint32_t * data = (uint32_t *)((uintptr_t)cell->bg << 32 | cell->fg);
					list_insert(tmp, data);
				}
			}
		}
	}

	for (int y = 0; y < state->height; ++y) {
		for (int x = 0; x < state->width; ++x) {
			term_cell_t * cell = &state->term_buffer_a[y * state->width + x];
			if (cell->flags & ANSI_EXT_IMG) {
				uint32_t * data = (uint32_t *)((uintptr_t)cell->bg << 32 | cell->fg);
				list_insert(tmp, data);
			}
		}
	}

	for (int y = 0; y < state->height; ++y) {
		for (int x = 0; x < state->width; ++x) {
			term_cell_t * cell = &state->term_buffer_b[y * state->width + x];
			if (cell->flags & ANSI_EXT_IMG) {
				uint32_t * data = (uint32_t *)((uintptr_t)cell->bg << 32 | cell->fg);
				list_insert(tmp, data);
			}
		}
	}

	foreach(node, term->images_list) {
		if (!list_find(tmp, node->value)) {
			free(node->value);
		}
	}

	list_free(term->images_list);
	term->images_list = tmp;
}

/* Scroll the terminal up or down. */
static void term_scroll(term_state_t * state, int how_much) {

	/* Remove image data for image cells that are no longer on screen. */
	flush_unused_images(state);
}

/* ANSI callback to set cell image data. */
static void term_set_cell_contents(term_state_t * state, int x, int y, char * data) {
	struct Terminal_Private * term = state->priv;
	char * cell_data = malloc(term->char_width * term->char_height * sizeof(uint32_t));
	memcpy(cell_data, data, term->char_width * term->char_height * sizeof(uint32_t));
	list_insert(term->images_list, cell_data);

	term_cell_t * cell = &state->term_buffer[y * state->width + x];
	cell->c = ' ';
	cell->fg = (uintptr_t)(cell_data) & 0xFFFFFFFF;
	cell->bg = (uintptr_t)(cell_data) >> 32;
	cell->flags = ANSI_EXT_IMG;
}

/* ANSI callback to get character cell width */
static int term_get_cell_width(term_state_t * state) {
	struct Terminal_Private * term = state->priv;
	return term->char_width;
}

/* ANSI callback to get character cell height */
static int term_get_cell_height(term_state_t * state) {
	struct Terminal_Private * term = state->priv;
	return term->char_height;
}


/* ANSI callback to clear the terminal. */
static void term_clear(term_state_t * state, int i) {
	flush_unused_images(state);
}


static void full_reset(term_state_t * state) {
	struct Terminal_Private * term = state->priv;
	term->terminal_title_length = 0;
}

static void term_state_change(term_state_t * state) {
	/* mouse_on, etc., has possible changed, update things */
	menu_update_toggle_state(_menu_toggle_altscreen, state->active_buffer);
	menu_update_toggle_state(_menu_toggle_mouse_reporting, state->mouse_on & TERMEMU_MOUSE_ENABLE);
	menu_update_toggle_state(_menu_toggle_mouse_drag, state->mouse_on & TERMEMU_MOUSE_DRAG);
	menu_update_toggle_state(_menu_toggle_mouse_sgr, state->mouse_on & TERMEMU_MOUSE_SGR);
	menu_update_toggle_state(_menu_toggle_mouse_altscroll, state->mouse_on & TERMEMU_MOUSE_ALTSCRL);
	menu_update_toggle_state(_menu_toggle_paste_bracketing, state->paste_mode);
}

/* ANSI callbacks */
term_callbacks_t term_callbacks = {
	term_clear,
	term_scroll,
	input_buffer_stuff,
	set_title,
	term_set_cell_contents,
	term_get_cell_width,
	term_get_cell_height,
	full_reset,
	term_state_change,
};

static void scroll_up(int amount) {
	termemu_scroll_up(current_terminal(), amount);
}

static void scroll_down(int amount) {
	termemu_scroll_down(current_terminal(), amount);
}

static void handle_input(char c) {
	write_input_buffer(current_terminal(), &c, 1);
	termemu_unscroll(current_terminal());
}

static void handle_input_s(char * c) {
	size_t len = strlen(c);
	write_input_buffer(current_terminal(), c, len);
	termemu_unscroll(current_terminal());
}

static void new_tab() {
	if (terminals->length == 9) return;
	active_terminal = terminal_create(
		this_term()->scale_fonts,
		this_term()->font_scaling,
		active_terminal->scrollback->max_scrollback,
		this_term()->use_truetype,
		this_term()->emulate_bold,
		0, NULL);
	update_menu_bar_tabs();
	reinit();
}

#define mod_Shift (event->modifiers & KEY_MOD_LEFT_SHIFT || event->modifiers & KEY_MOD_RIGHT_SHIFT)
#define mod_Ctrl  (event->modifiers & KEY_MOD_LEFT_CTRL  || event->modifiers & KEY_MOD_RIGHT_CTRL)
#define mod_Alt   (event->modifiers & KEY_MOD_LEFT_ALT   || event->modifiers & KEY_MOD_RIGHT_ALT)

/* Handle a key press from Yutani */
static void key_event(int ret, key_event_t * event) {
	if (ret) {
		if (mod_Shift && mod_Ctrl && !mod_Alt && (event->keycode == 't')) {
			new_tab();
			return;
		}

		if (mod_Alt && !mod_Ctrl && !mod_Shift && (event->keycode >= '1') && (event->keycode <= '9') && terminals->length > 1) {
			int term = event->keycode - '1';
			int i = 0;

			foreach (node, terminals) {
				if (i == term) {
					active_terminal = node->value;
					update_menu_bar_tabs();
					reinit();
					return;
				}
				i++;
			}
		}

		/* Ctrl-Shift-C - Copy selection */
		if (mod_Shift && mod_Ctrl && !mod_Alt && (event->keycode == 'c')) {
			if (current_terminal()->selection) {
				/* Copy selection */
				copy_selection(0);
			}
			return;
		}

		/* Ctrl-Shift-V - Paste selection */
		if (mod_Shift && mod_Ctrl && !mod_Alt && (event->keycode == 'v')) {
			/* Paste selection */
			yutani_special_request(yctx, NULL, YUTANI_SPECIAL_REQUEST_CLIPBOARD);
			return;
		}

		if (mod_Ctrl && !mod_Shift && !mod_Alt && (event->keycode == '0')) {
			this_term()->scale_fonts  = 0;
			this_term()->font_scaling = 1.0;
			update_scale_menu();
			reinit();
			return;
		}

		if (mod_Shift && mod_Ctrl && !mod_Alt && (event->keycode == '=')) {
			this_term()->scale_fonts  = 1;
			this_term()->font_scaling = this_term()->font_scaling * 1.2;
			update_scale_menu();
			reinit();
			return;
		}

		if (mod_Ctrl && !mod_Shift && !mod_Alt && (event->keycode == '-')) {
			this_term()->scale_fonts  = 1;
			this_term()->font_scaling = this_term()->font_scaling * 0.8333333;
			update_scale_menu();
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
					if (current_terminal()->active_buffer != 1) scroll_up(current_terminal()->height/2);
				} else {
					handle_input_s("\033[5~");
				}
				break;
			case KEY_PAGE_DOWN:
				if (event->modifiers & KEY_MOD_LEFT_SHIFT) {
					if (current_terminal()->active_buffer != 1) scroll_down(current_terminal()->height/2);
				} else {
					handle_input_s("\033[6~");
				}
				break;
			case KEY_HOME:
				if (event->modifiers & KEY_MOD_LEFT_SHIFT) {
					termemu_scroll_top(current_terminal());
				} else {
					handle_input_s("\033[H");
				}
				break;
			case KEY_END:
				if (event->modifiers & KEY_MOD_LEFT_SHIFT) {
					termemu_unscroll(current_terminal());
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
	term_state_t * matched = NULL;
	node_t * matched_node = NULL;
	foreach (node, terminals) {
		term_state_t * term = node->value;
		struct Terminal_Private * priv = term->priv;
		if (pid == priv->child_pid) {
			matched = term;
			matched_node = node;
			break;
		}
	}

	if (!matched) return;

	if (current_terminal() == matched) {
		if (matched_node->prev) {
			active_terminal = matched_node->prev->value;
		} else if (matched_node->next) {
			active_terminal = matched_node->next->value;
		}
	}

	list_delete(terminals, matched_node);
	update_menu_bar_tabs();
	reinit();

	if (terminals->length == 0) exit_application = 1;

	struct Terminal_Private * priv = matched->priv;
	close(priv->input_buffer_semaphore[1]); /* Kills the input processing thread */
	close(priv->fd_master); /* Hangs up the TTY */
	close(priv->fd_slave);

	/* FIXME need to actually free the terminal */
}

static void terminal_calculate_font_size(struct Terminal_Private * priv) {
	/* Set up font sizing */
	if (priv->use_truetype) {
		priv->char_width = 8;
		priv->char_height = 17;
		priv->font_size = 13;
		priv->char_offset = 13;
		if (priv->scale_fonts) {
			priv->font_size   *= priv->font_scaling;
			priv->char_height *= priv->font_scaling;
			priv->char_width  *= priv->font_scaling;
			priv->char_offset *= priv->font_scaling;
		}
	} else {
		priv->char_width = LARGE_FONT_CELL_WIDTH;
		priv->char_height = LARGE_FONT_CELL_HEIGHT;
	}
}

static void terminal_set_size(term_state_t * state) {
	struct Terminal_Private * term = state->priv;
	struct winsize w;
	w.ws_row = state->height;
	w.ws_col = state->width;
	w.ws_xpixel = state->width * term->char_width;
	w.ws_ypixel = state->height * term->char_height;
	ioctl(term->fd_master, TIOCSWINSZ, &w);
}

/* Reinitialize the terminal after a resize. */
static void reinit(void) {
	struct Terminal_Private * this = current_terminal()->priv;
	terminal_calculate_font_size(this);

	/* Resize the terminal buffer */
	int term_width  = window_width  / this->char_width;
	int term_height = window_height / this->char_height;

	this->extra_right = window_width - (term_width * this->char_width);
	this->extra_bottom = window_height - (term_height * this->char_height);

	term_state_change(current_terminal());
	update_font_menu_states();
	update_scale_menu();

	if (current_terminal()->width == term_width && current_terminal()->height == term_height) {
		memset(current_terminal()->term_display, 0xFF, sizeof(term_cell_t) * term_width * term_height);
		goto _done;
	}

	termemu_reinit(current_terminal(), term_width, term_height);
	terminal_set_size(current_terminal());

	/* Redraw the window */
_done:
	render_decors();
	termemu_redraw_all(current_terminal());
	maybe_flip_display(1);
}

static term_state_t * terminal_create(int scale_fonts, float font_scaling, int max_scrollback, int use_truetype, int emulate_bold, int argc, char * argv[]) {
	struct Terminal_Private * priv = calloc(1, sizeof(struct Terminal_Private));

	priv->scale_fonts = scale_fonts;
	priv->font_scaling = font_scaling;
	priv->use_truetype = use_truetype;
	priv->emulate_bold = emulate_bold;
	terminal_calculate_font_size(priv);

	priv->images_list = list_create();

	int term_width  = window_width  / priv->char_width;
	int term_height = window_height / priv->char_height;
	priv->extra_right = window_width - (term_width * priv->char_width);
	priv->extra_bottom = window_height - (term_height * priv->char_height);

	term_state_t * out = termemu_init(term_width, term_height, &term_callbacks);
	termemu_init_scrollback(out, max_scrollback);
	out->priv = priv;

	list_insert(terminals, out);

	pipe(priv->input_buffer_semaphore);
	priv->input_buffer_queue = list_create();
	pthread_create(&priv->input_buffer_thread, NULL, handle_input_writing, out);

	update_menu_bar_tabs();

	/* Open a PTY */
	openpty(&priv->fd_master, &priv->fd_slave, NULL, NULL, NULL);
	terminal_set_size(out);

	priv->child_pid = fork();

	if (!priv->child_pid) {
		setsid();
		/* Prepare stdin/out/err */
		dup2(priv->fd_slave, 0);
		dup2(priv->fd_slave, 1);
		dup2(priv->fd_slave, 2);

		ioctl(STDIN_FILENO, TIOCSCTTY, &(int){1});
		tcsetpgrp(STDIN_FILENO, getpid());

		signal(SIGHUP, SIG_DFL);

		/* Set the TERM environment variable. */
		putenv("TERM=toaru");

		/* Execute requested initial process */
		if (terminal_login_shell_restricted) {
			char * tokens[] = {"/bin/login-loop",NULL};
			execvp(tokens[0], tokens);
		} else if (argc) {
			/* Run something specified by the terminal startup */
			execvp(argv[0], argv);
		} else {
			/* Run the user's shell */
			char * shell = getenv("SHELL");
			if (!shell) shell = "/bin/sh"; /* fallback */
			char * tokens[] = {shell,NULL};
			execvp(tokens[0], tokens);
			exit(1);
		}

		exit(127);
	}

	return out;
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
	if (t_window_width < this_term()->char_width * 20 || t_window_height < this_term()->char_height * 10) {
		resize_attempts++;
		int n_width  = extra_x + max(this_term()->char_width * 20, t_window_width);
		int n_height = extra_y + max(this_term()->char_height * 10, t_window_height);
		yutani_window_resize_offer(yctx, window, n_width, n_height);
		return;
	}

	/* If requested, ensure the terminal resizes to a fixed size based on the cell size. */
	if (!_free_size && ((t_window_width % this_term()->char_width != 0 || t_window_height % this_term()->char_height != 0) && resize_attempts < 3)) {
		resize_attempts++;
		int n_width  = extra_x + t_window_width  - (t_window_width  % this_term()->char_width);
		int n_height = extra_y + t_window_height - (t_window_height % this_term()->char_height);
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
	if (current_terminal()->mouse_on & TERMEMU_MOUSE_SGR) { /* FIXME */
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
					if (ke->wid == window->wid) key_event(ret, &ke->event);
				}
				break;
			case YUTANI_MSG_WINDOW_FOCUS_CHANGE:
				{
					struct yutani_msg_window_focus_change * wf = (void*)m->data;
					yutani_window_t * win = hashmap_get(yctx->windows, (void*)(uintptr_t)wf->wid);
					if (win == window) {
						win->focused = wf->focused;
						render_decors();
						foreach (node, terminals) {
							((term_state_t*)node->value)->focused = wf->focused;
						}
						termemu_draw_cursor(current_terminal());
						maybe_flip_display(1);
					}
				}
				break;
			case YUTANI_MSG_WINDOW_CLOSE:
				{
					struct yutani_msg_window_close * wc = (void*)m->data;
					if (wc->wid == window->wid) {
						exit_application = 1;
					}
				}
				break;
			case YUTANI_MSG_SESSION_END:
				{
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
					if (current_terminal()->paste_mode) {
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
								exit_application = 1;
								break;
							case DECOR_RIGHT:
								/* right click in decoration, show appropriate menu */
								decor_show_default_menu(window, window->x + me->new_x, window->y + me->new_y);
								break;
							default:
								break;
						}

						menu_bar_mouse_event(yctx, window, (struct menu_bar*)&terminal_menu_bar, me, me->new_x, me->new_y);
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

					if (!(current_terminal()->mouse_on & TERMEMU_MOUSE_ENABLE)) {
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
					new_x /= this_term()->char_width;
					new_y /= this_term()->char_height;

					if (new_x < 0 || new_y < 0) break;
					if (new_x >= current_terminal()->width || new_y >= current_terminal()->height) break; /* FIXME */

					/* Map Cursor Action */
					if ((current_terminal()->mouse_on & TERMEMU_MOUSE_ENABLE) && !(me->modifiers & YUTANI_KEY_MODIFIER_SHIFT)) {

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
						} else if (current_terminal()->mouse_on & TERMEMU_MOUSE_DRAG) {
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
							termemu_selection_click(current_terminal(), new_x, new_y);
							if (_menu_copy) menu_update_enabled(_menu_copy, current_terminal()->selection);
							if (_menu_copy_escapes) menu_update_enabled(_menu_copy_escapes, current_terminal()->selection);
						}
						if (me->command == YUTANI_MOUSE_EVENT_DRAG && me->buttons & YUTANI_MOUSE_BUTTON_LEFT ){
							termemu_selection_drag(current_terminal(), new_x, new_y);
							if (_menu_copy) menu_update_enabled(_menu_copy, current_terminal()->selection);
							if (_menu_copy_escapes) menu_update_enabled(_menu_copy_escapes, current_terminal()->selection);
						}
						if (me->command == YUTANI_MOUSE_EVENT_RAISE) {
							if (me->new_x == me->old_x && me->new_y == me->old_y) {
								current_terminal()->selection = 0;
								termemu_redraw_scrollback(current_terminal());
								if (_menu_copy) menu_update_enabled(_menu_copy, current_terminal()->selection);
								if (_menu_copy_escapes) menu_update_enabled(_menu_copy_escapes, current_terminal()->selection);
							} /* else selection */
						}
						if (me->buttons & YUTANI_MOUSE_SCROLL_UP) {
							if (current_terminal()->active_buffer == 1) {
								if (current_terminal()->mouse_on & TERMEMU_MOUSE_ALTSCRL) {
									handle_input_s("\033[A");
								}
							} else {
								scroll_up(5);
							}
						} else if (me->buttons & YUTANI_MOUSE_SCROLL_DOWN) {
							if (current_terminal()->active_buffer == 1) {
								if (current_terminal()->mouse_on & TERMEMU_MOUSE_ALTSCRL) {
									handle_input_s("\033[B");
								}
							} else {
								scroll_down(5);
							}
						} else if (me->buttons & YUTANI_MOUSE_BUTTON_RIGHT) {
							if (!menu_right_click->window) {
								menu_show_at(menu_right_click, window, me->new_x, me->new_y);
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
	exit_application = 1;
}

static void _menu_action_new_tab(struct MenuEntry * self) {
	new_tab();
}

static void _menu_action_hide_borders(struct MenuEntry * self) {
	_no_frame = !(_no_frame);
	update_bounds();
	window_width = window->width - decor_width;
	window_height = window->height - (decor_height + menu_bar_height);
	menu_update_toggle_state(_menu_toggle_borders_context, !_no_frame);
	menu_update_toggle_state(_menu_toggle_borders_bar, !_no_frame);
	
	reinit();
}

static struct MenuEntry * _menu_toggle_bitmap_context = NULL;
static struct MenuEntry * _menu_toggle_bitmap_bar = NULL;
static struct MenuEntry * _menu_toggle_bold_bar = NULL;
static struct MenuEntry * _menu_toggle_bold_context = NULL;

static void update_font_menu_states(void) {
	menu_update_toggle_state(_menu_toggle_bitmap_context, !this_term()->use_truetype );
	menu_update_toggle_state(_menu_toggle_bitmap_bar, !this_term()->use_truetype );
	menu_update_enabled(_menu_set_zoom, this_term()->use_truetype );
	menu_update_enabled(_menu_toggle_bold_bar, !this_term()->use_truetype );
	menu_update_enabled(_menu_toggle_bold_context, !this_term()->use_truetype );
	menu_update_toggle_state(_menu_toggle_bold_bar, this_term()->emulate_bold);
	menu_update_toggle_state(_menu_toggle_bold_context, this_term()->emulate_bold);
}

static void _menu_action_toggle_tt(struct MenuEntry * self) {
	this_term()->use_truetype = !this_term()->use_truetype;
	update_font_menu_states();
	reinit();
}

static void _menu_action_toggle_bold(struct MenuEntry * self) {
	this_term()->emulate_bold = !this_term()->emulate_bold;
	update_font_menu_states();
	reinit();
}

static void _menu_action_toggle_free_size(struct MenuEntry * self) {
	_free_size = !(_free_size);
	menu_update_toggle_state(self, !_free_size);
}

static void _menu_action_toggle_altscreen(struct MenuEntry * self) {
	termemu_switch_buffer(current_terminal(), !current_terminal()->active_buffer);
	term_state_change(current_terminal());
}

static void _menu_action_toggle_mouse_reporting(struct MenuEntry * self) {
	if (current_terminal()->mouse_on & TERMEMU_MOUSE_ENABLE) {
		current_terminal()->mouse_on &= ~TERMEMU_MOUSE_ENABLE;
	} else {
		current_terminal()->mouse_on |= TERMEMU_MOUSE_ENABLE;
	}
	term_state_change(current_terminal());
}

static void _menu_action_toggle_mouse_drag(struct MenuEntry * self) {
	if (current_terminal()->mouse_on & TERMEMU_MOUSE_DRAG) {
		current_terminal()->mouse_on &= ~TERMEMU_MOUSE_DRAG;
	} else {
		current_terminal()->mouse_on |= TERMEMU_MOUSE_DRAG;
	}
	term_state_change(current_terminal());
}

static void _menu_action_toggle_mouse_sgr(struct MenuEntry * self) {
	if (current_terminal()->mouse_on & TERMEMU_MOUSE_SGR) {
		current_terminal()->mouse_on &= ~TERMEMU_MOUSE_SGR;
	} else {
		current_terminal()->mouse_on |= TERMEMU_MOUSE_SGR;
	}
	term_state_change(current_terminal());
}

static void _menu_action_toggle_mouse_altscroll(struct MenuEntry * self) {
	if (current_terminal()->mouse_on & TERMEMU_MOUSE_ALTSCRL) {
		current_terminal()->mouse_on &= ~TERMEMU_MOUSE_ALTSCRL;
	} else {
		current_terminal()->mouse_on |= TERMEMU_MOUSE_ALTSCRL;
	}
	term_state_change(current_terminal());
}

static void _menu_action_toggle_paste_bracketing(struct MenuEntry * self) {
	current_terminal()->paste_mode = !current_terminal()->paste_mode;
	term_state_change(current_terminal());
}

static void _menu_action_show_about(struct MenuEntry * self) {
	char about_cmd[1024] = "\0";
	strcat(about_cmd, "about \"About Terminal\" /usr/share/icons/48/utilities-terminal.png \"ToaruOS Terminal\" \"© 2013-2026 K. Lange\n-\nPart of ToaruOS, which is free software\nreleased under the NCSA/University of Illinois\nlicense.\n-\n%https://toaruos.org\n%https://github.com/klange/toaruos\" ");
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
	copy_selection(0);
}

static void _menu_action_copy_escapes(struct MenuEntry * self) {
	copy_selection(1);
}

static void _menu_action_paste(struct MenuEntry * self) {
	yutani_special_request(yctx, NULL, YUTANI_SPECIAL_REQUEST_CLIPBOARD);
}

static void _menu_action_signal(struct MenuEntry * self) {
	int sig;
	str2sig(((struct MenuEntry_Normal*)self)->action, &sig);

	pid_t pgrp = tcgetpgrp(this_term()->fd_master);
	if (pgrp != -1) kill(pgrp, sig);
}

static void _menu_action_reset(struct MenuEntry * self) {
	termemu_full_reset(current_terminal());
}

static void _menu_action_clear(struct MenuEntry * self) {
	termemu_clear(current_terminal(), 2);
}

static void _menu_action_clear_scrollback(struct MenuEntry * self) {
	termemu_clear(current_terminal(), 3);
}

static void update_scale_menu(void) {
	menu_update_toggle_state(_menu_scale_075, this_term()->font_scaling == 0.75);
	menu_update_toggle_state(_menu_scale_100, this_term()->font_scaling == 1.00);
	menu_update_toggle_state(_menu_scale_150, this_term()->font_scaling == 1.50);
	menu_update_toggle_state(_menu_scale_200, this_term()->font_scaling == 2.00);
}

static void _menu_action_set_scale(struct MenuEntry * self) {
	struct MenuEntry_Normal * _self = (struct MenuEntry_Normal *)self;
	if (!_self->action) {
		this_term()->scale_fonts  = 0;
		this_term()->font_scaling = 1.0;
		update_scale_menu();
	} else {
		this_term()->scale_fonts  = 1;
		this_term()->font_scaling = atof(_self->action);
		update_scale_menu();
	}
	reinit();
}

static void render_decors_callback(struct menu_bar * self) {
	(void)self;
	render_decors();
}

static void do_sig_menu(struct MenuList * m, int i) {
	char sig[SIG2STR_MAX]; /* will be dup'd by menu_create_normal */
	sig2str(i, sig);
	char * tmp;
	asprintf(&tmp, "SIG%s (%d)", sig, i);
	menu_insert(m, menu_create_normal(NULL,sig,tmp, _menu_action_signal));
	free(tmp); /* dup'd by menu_creat_normal */
}

static void do_sig_menus(struct MenuList *p, int i, int max, char * name) {
	char * tmp;
	struct MenuList *ms = menu_create();
	for (; i < max; ++i) do_sig_menu(ms, i);
	menu_set_insert(terminal_menu_bar._super.set, name, ms);
	asprintf(&tmp, "SIG%s-SIG%s...",
		((struct MenuEntry_Normal*)list_index(ms->entries, 0))->action,
		((struct MenuEntry_Normal*)list_index(ms->entries, ms->entries->length-1))->action);
	menu_insert(p, menu_create_submenu(NULL,name,tmp));
	free(tmp);
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
	window_width = atoi(str) * (in_chars ? 8 : 1);
	window_height = atoi(c) * (in_chars ? 17 : 1);

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
	window_width  = 8 * 80;
	window_height = 17 * 24;

	int set_max_scrollback = 10000;
	int set_scale_fonts = 0;
	float set_font_scaling = 1.0;
	int set_truetype = 1;
	int set_bold = 0;

	static struct option long_opts[] = {
		{"fullscreen", no_argument,       0, 'F'},
		{"bitmap",     no_argument,       0, 'b'},
		{"emulatebold",no_argument,       0, 'e'},
		{"scale",      required_argument, 0, 's'},
		{"help",       no_argument,       0, 'h'},
		{"grid",       no_argument,       0, 'x'},
		{"no-frame",   no_argument,       0, 'n'},
		{"geometry",   required_argument, 0, 'g'},
		{"blurred",    no_argument,       0, 'B'},
		{"scrollback", required_argument, 0, 'S'},
		{"login",      no_argument,       0, 'l'},
		{0,0,0,0}
	};

	/* Read some arguments */
	int index, c;
	while ((c = getopt_long(argc, argv, "behxlnFs:g:BS:", long_opts, &index)) != -1) {
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
				set_truetype = 0;
				break;
			case 'e':
				set_bold = 1;
				break;
			case 'h':
				usage(argv);
				return 0;
			case 's':
				set_scale_fonts = 1;
				set_font_scaling = atof(optarg);
				break;
			case 'g':
				parse_geometry(argv,optarg);
				break;
			case 'B':
				_flags = YUTANI_WINDOW_FLAG_BLUR_BEHIND;
				break;
			case 'S':
				set_max_scrollback = strtoull(optarg,NULL,10);
				break;
			case 'l':
				terminal_login_shell_restricted = 1;
				break;
			case '?':
				return usage(argv);
		}
	}

	if (terminal_login_shell_restricted && optind != argc) {
		fprintf(stderr, "%s: arguments may not be provided with '--login'\n", argv[0]);
		return 1;
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
	terminal_menu_bar._super.entries = terminal_menu_entries;
	terminal_menu_bar._super.redraw_callback = render_decors_callback;

	_menu_new_tab = menu_create_normal(NULL, NULL, "New Tab", _menu_action_new_tab);
	_menu_exit = menu_create_normal("exit","exit","Exit", _menu_action_exit);
	_menu_copy = menu_create_normal(NULL, NULL, "Copy", _menu_action_copy);
	_menu_copy_escapes = menu_create_normal(NULL, NULL, "Copy with escapes", _menu_action_copy_escapes);
	_menu_paste = menu_create_normal(NULL, NULL, "Paste", _menu_action_paste);

	menu_update_enabled(_menu_copy, 0);
	menu_update_enabled(_menu_copy_escapes, 0);

	menu_right_click = menu_create();
	menu_insert(menu_right_click, _menu_new_tab);
	menu_insert(menu_right_click, menu_create_separator());
	menu_insert(menu_right_click, _menu_copy);
	menu_insert(menu_right_click, _menu_copy_escapes);
	menu_insert(menu_right_click, _menu_paste);
	menu_insert(menu_right_click, menu_create_separator());
	if (!_fullscreen) {
		_menu_toggle_borders_context = menu_create_toggle(NULL, "Show borders", !_no_frame, _menu_action_hide_borders);
		menu_insert(menu_right_click, _menu_toggle_borders_context);
	}
	_menu_toggle_bitmap_context = menu_create_toggle(NULL, "Bitmap font", !set_truetype, _menu_action_toggle_tt);
	menu_insert(menu_right_click, _menu_toggle_bitmap_context);
	_menu_toggle_bold_context = menu_create_toggle(NULL, "Emulate bold", set_bold, _menu_action_toggle_bold);
	menu_update_enabled(_menu_toggle_bold_context, !set_truetype);
	menu_insert(menu_right_click, _menu_toggle_bold_context);
	menu_insert(menu_right_click, menu_create_separator());
	menu_insert(menu_right_click, menu_create_submenu(NULL,"termstate","Terminal state..."));
	menu_insert(menu_right_click, menu_create_submenu(NULL,"signal", "Send signal..."));
	menu_insert(menu_right_click, menu_create_separator());
	menu_insert(menu_right_click, _menu_exit);

	/* Menu Bar menus */
	terminal_menu_bar._super.set = menu_set_create();

	menu_set_insert(terminal_menu_bar._super.set, "context", menu_right_click);

	struct MenuList * m;
	m = menu_create(); /* File */
	menu_insert(m, _menu_new_tab);
	menu_insert(m, menu_create_separator());
	menu_insert(m, _menu_exit);
	menu_set_insert(terminal_menu_bar._super.set, "file", m);

	m = menu_create();
	menu_insert(m, _menu_copy);
	menu_insert(m, _menu_copy_escapes);
	menu_insert(m, _menu_paste);
	menu_set_insert(terminal_menu_bar._super.set, "edit", m);

	m = menu_create();
	menu_insert(m, (_menu_scale_075 = menu_create_toggle("0.75", "75%", 0, _menu_action_set_scale)));
	menu_insert(m, (_menu_scale_100 = menu_create_toggle(NULL,  "100%", 0, _menu_action_set_scale)));
	menu_insert(m, (_menu_scale_150 = menu_create_toggle("1.5", "150%", 0, _menu_action_set_scale)));
	menu_insert(m, (_menu_scale_200 = menu_create_toggle("2.0", "200%", 0, _menu_action_set_scale)));
	menu_set_insert(terminal_menu_bar._super.set, "zoom", m);

	m = menu_create();
	menu_insert(m, menu_create_normal(NULL, NULL, "View stats", _menu_action_cache_stats));
	menu_insert(m, menu_create_normal(NULL, NULL, "Clear cache", _menu_action_clear_cache));
	menu_set_insert(terminal_menu_bar._super.set, "cache", m);

	m = menu_create();
	menu_insert(m, (_menu_toggle_altscreen        = menu_create_toggle(NULL, "Alternate screen", 0,        _menu_action_toggle_altscreen)));
	menu_insert(m, (_menu_toggle_mouse_reporting  = menu_create_toggle(NULL, "Mouse reporting", 0,         _menu_action_toggle_mouse_reporting)));
	menu_insert(m, (_menu_toggle_mouse_drag       = menu_create_toggle(NULL, "Drag reporting", 0,          _menu_action_toggle_mouse_drag)));
	menu_insert(m, (_menu_toggle_mouse_sgr        = menu_create_toggle(NULL, "SGR 1006 mouse mode", 0,     _menu_action_toggle_mouse_sgr)));
	menu_insert(m, (_menu_toggle_mouse_altscroll  = menu_create_toggle(NULL, "Alt. screen scroll mode", 0, _menu_action_toggle_mouse_altscroll)));
	menu_insert(m, (_menu_toggle_paste_bracketing = menu_create_toggle(NULL, "Paste bracketing", 0,        _menu_action_toggle_paste_bracketing)));
	menu_set_insert(terminal_menu_bar._super.set, "termstate", m);

	m = menu_create();
	_menu_toggle_borders_bar = menu_create_toggle(NULL, "Show borders", !_no_frame, _menu_action_hide_borders);
	menu_insert(m, _menu_toggle_borders_bar);
	menu_insert(m, menu_create_toggle(NULL, "Snap to Cell Size", !_free_size, _menu_action_toggle_free_size));

	menu_insert(m, menu_create_separator());

	menu_insert(m, (_menu_set_zoom = menu_create_submenu(NULL,"zoom","Set zoom...")));
	menu_update_enabled(_menu_set_zoom, set_truetype);
	_menu_toggle_bitmap_bar = menu_create_toggle(NULL, "Bitmap font", !set_truetype, _menu_action_toggle_tt);
	menu_insert(m, _menu_toggle_bitmap_bar);
	_menu_toggle_bold_bar = menu_create_toggle(NULL, "Emulate bold", set_bold, _menu_action_toggle_bold);
	menu_update_enabled(_menu_toggle_bold_bar, !set_truetype);
	menu_insert(m, _menu_toggle_bold_bar);

	menu_insert(m, menu_create_separator());

	menu_insert(m, menu_create_normal(NULL, NULL, "Redraw", _menu_action_redraw));
	menu_insert(m, menu_create_submenu(NULL,"cache","Glyph cache..."));
	menu_set_insert(terminal_menu_bar._super.set, "view", m);

	m = menu_create();
	menu_insert(m, menu_create_normal("help","help","Contents", _menu_action_show_help));
	menu_insert(m, menu_create_separator());
	menu_insert(m, menu_create_normal("star","star","About Terminal", _menu_action_show_about));
	menu_set_insert(terminal_menu_bar._super.set, "help", m);

	m = menu_create();
	do_sig_menus(m,1,16,"signala");
	do_sig_menus(m,16,31,"signalb");
	do_sig_menus(m,31,NSIG,"signalc");
	menu_set_insert(terminal_menu_bar._super.set, "signal", m);

	m = menu_create();

	m = menu_create();
	menu_insert(m, menu_create_submenu(NULL,"termstate","Terminal state..."));
	menu_insert(m, menu_create_submenu(NULL,"signal", "Send signal..."));
	menu_insert(m, menu_create_separator());
	menu_insert(m, menu_create_normal(NULL,NULL,"Reset", _menu_action_reset));
	menu_insert(m, menu_create_normal(NULL,NULL,"Clear", _menu_action_clear));
	menu_insert(m, menu_create_normal(NULL,NULL,"Clear scrollback", _menu_action_clear_scrollback));
	menu_set_insert(terminal_menu_bar._super.set, "terminal", m);

	/* FIXME hack */
	m = menu_create();
	menu_insert(m, menu_create_separator());
	menu_set_insert(terminal_menu_bar._super.set, "tab", m);

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

	terminals = list_create();
	active_terminal = terminal_create(set_scale_fonts, set_font_scaling, set_max_scrollback, set_truetype, set_bold, argc-optind, &argv[optind]);

	/* PTY read buffer */
	unsigned char buf[4096];
	int next_wait = 200;

	size_t fds_size = 1;
	int * fds = malloc(sizeof(int));
	int * res = malloc(sizeof(int));
	term_state_t ** term = malloc(sizeof(term_state_t*));

	while (!exit_application) {

		if (fds_size != 1 + terminals->length) {
			fds_size = 1 + terminals->length;
			fds = realloc(fds, fds_size * sizeof(int));
			term = realloc(term, fds_size * sizeof(term_state_t *));
			fds[0] = fileno(yctx->sock);
			size_t i = 1;
			foreach(node, terminals) {
				term[i] = node->value;
				struct Terminal_Private * priv = term[i]->priv;
				fds[i] = priv->fd_master;
				i++;
			}
			res = realloc(res, fds_size * sizeof(int));
		}

		/* Wait for something to happen. */
		fswait3(fds_size,fds,next_wait,res);

		/* Check if the child application has closed. */
		check_for_exit();
		termemu_maybe_flip_cursor(current_terminal());

		int force_flip = (next_wait == 10);
		next_wait = 200;
		for (size_t i = 1; i < fds_size; ++i) {
			if (res[i]) {
				struct Terminal_Private * priv = term[i]->priv;
				if (term[i] == current_terminal()) {
					force_flip = 0;
					next_wait = 10;
				}
				ssize_t r = read(priv->fd_master, buf, 4096);
				for (ssize_t j = 0; j < r; ++j) {
					termemu_put(term[i], buf[j]);
				}
			}
		}

		if (res[0]) {
			/* Handle Yutani events. */
			handle_incoming();
		}
		maybe_flip_display(force_flip);
	}

	foreach(node, terminals) {
		struct Terminal_Private * priv = ((term_state_t*)node->value)->priv;
		close(priv->input_buffer_semaphore[1]);
	}

	/* Windows will close automatically on exit. */
	return 0;
}
