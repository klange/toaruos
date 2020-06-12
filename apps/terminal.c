/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2020 K. Lange
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
#include <toaru/sdf.h>

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
			" -f --no-ft      \033[3mForce disable the freetype backend.\033[0m\n"
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
static float    font_gamma     = 1.7;  /* Gamma to use for SDF library */
static uint16_t term_width     = 0;    /* Width of the terminal (in cells) */
static uint16_t term_height    = 0;    /* Height of the terminal (in cells) */
static uint16_t font_size      = 16;   /* Font size according to SDF library */
static uint16_t char_width     = 9;    /* Width of a cell in pixels */
static uint16_t char_height    = 17;   /* Height of a cell in pixels */
static uint16_t char_offset    = 0;    /* Offset of the font within the cell */
static int      csr_x          = 0;    /* Cursor X */
static int      csr_y          = 0;    /* Cursor Y */
static uint32_t current_fg     = 7;    /* Current foreground color */
static uint32_t current_bg     = 0;    /* Current background color */

static term_cell_t * term_buffer = NULL; /* The terminal cell buffer */
static term_cell_t * term_buffer_a = NULL;
static term_cell_t * term_buffer_b = NULL;
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
static bool _have_freetype = 0;    /* Whether freetype is available */
static bool _force_no_ft   = 0;    /* Whether to force disable the freetype backend */
static bool _free_size     = 1;    /* Disable rounding when resized */

/** Freetype extension renderer functions */
static void (*freetype_set_font_face)(int face) = NULL;
static void (*freetype_set_font_size)(int size) = NULL;
static void (*freetype_draw_char)(gfx_context_t * ctx, int x, int y, uint32_t fg, uint32_t codepoint) = NULL;

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
#define TERMINAL_TITLE_SIZE 512
static char   terminal_title[TERMINAL_TITLE_SIZE];
static size_t terminal_title_length = 0;
static gfx_context_t * ctx;
static struct MenuList * menu_right_click = NULL;

static void render_decors(void);
static void term_clear();
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

#define MAX_SCROLLBACK 10240
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

static void redraw_new_selection(int old_x, int old_y) {
	if (selection_end_y == selection_start_y && old_y != selection_start_y) {
		int a, b;
		a = selection_end_x;
		b = selection_end_y;
		selection_end_x = old_x;
		selection_end_y = old_y;
		iterate_selection(cell_redraw_offset);
		selection_end_x = a;
		selection_end_y = b;
		iterate_selection(cell_redraw_offset_inverted);
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
				iterate_selection(cell_redraw_offset_inverted);
			} else {
				/* Selection got smaller */
				iterate_selection(cell_redraw_offset);
			}
		} else if (old_y == b) {
			/* Was a single line */
			if (selection_end_y == b) {
				/* And still is */
				if (old_x < a) {
					/* Backwards */
					if (selection_end_x < old_x) {
						iterate_selection(cell_redraw_offset_inverted);
					} else {
						iterate_selection(cell_redraw_offset);
					}
				} else {
					if (selection_end_x < old_x) {
						iterate_selection(cell_redraw_offset);
					} else {
						iterate_selection(cell_redraw_offset_inverted);
					}
				}
			} else if (selection_end_y < b) {
				/* Moved up */
				if (old_x <= a) {
					/* Should be fine with just append */
					iterate_selection(cell_redraw_offset_inverted);
				} else {
					/* Need to erase first */
					iterate_selection(cell_redraw_offset);
					selection_start_x = a;
					selection_start_y = b;
					iterate_selection(cell_redraw_offset_inverted);
				}
			} else if (selection_end_y > b) {
				if (old_x >= a) {
					/* Should be fine with just append */
					iterate_selection(cell_redraw_offset_inverted);
				} else {
					/* Need to erase first */
					iterate_selection(cell_redraw_offset);
					selection_start_x = a;
					selection_start_y = b;
					iterate_selection(cell_redraw_offset_inverted);
				}
			}
		} else {
			/* Forward */
			if (selection_end_y < old_y || (selection_end_y == old_y && selection_end_x < old_x)) {
				/* Selection got smaller */
				iterate_selection(cell_redraw_offset);
			} else {
				/* Selection extended */
				iterate_selection(cell_redraw_offset_inverted);
			}
		}

		cell_redraw_offset_inverted(a,b);
		cell_redraw_offset_inverted(selection_end_x, selection_end_y);

		/* Restore */
		selection_start_x = a;
		selection_start_y = b;
	}
}

/* Figure out how long the UTF-8 selection string should be. */
static void count_selection(uint16_t x, uint16_t _y) {
	int y = _y;
	y -= scrollback_offset;
	if (y >= 0) {
		term_cell_t * cell = (term_cell_t *)((uintptr_t)term_buffer + (y * term_width + x) * sizeof(term_cell_t));
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
	if (_fullscreen) {
		/* In full screen mode, pre-blend the color over black. */
		color = alpha_blend_rgba(premultiply(rgba(0,0,0,0xFF)), color);
	}
	if (!_no_frame) {
		GFX(ctx, (x+decor_left_width),(y+decor_top_height+menu_bar_height)) = color;
	} else {
		GFX(ctx, x,y) = color;
	}
}

/* Draw a partial block character. */
static void draw_semi_block(int c, int x, int y, uint32_t fg, uint32_t bg) {
	bg = premultiply(bg);
	fg = premultiply(fg);
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

#include "apps/ununicode.h"

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

	/* Draw block characters */
	if (val >= 0x2580 && val <= 0x258F) {
		for (uint8_t i = 0; i < char_height; ++i) {
			for (uint8_t j = 0; j < char_width; ++j) {
				term_set_point(x+j,y+i,premultiply(_bg));
			}
		}
		draw_semi_block(val, x, y, _fg, _bg);
		goto _extra_stuff;
	}

	/* Draw glyphs */
	if (_use_aa && !_have_freetype) {
		/* Draw using the Toaru SDF rendering library */
		char tmp[7];
		to_eight(val, tmp);
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
	} else if (_use_aa && _have_freetype) {
		/* Draw using freetype extension */
		if (val == 0xFFFF) { return; } /* Unicode, do not redraw here */
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

#define FONT_MONOSPACE              4
#define FONT_MONOSPACE_BOLD         5
#define FONT_MONOSPACE_ITALIC       6
#define FONT_MONOSPACE_BOLD_ITALIC  7
		int _font = FONT_MONOSPACE;
		if (flags & ANSI_BOLD && flags & ANSI_ITALIC) {
			_font = FONT_MONOSPACE_BOLD_ITALIC;
		} else if (flags & ANSI_ITALIC) {
			_font = FONT_MONOSPACE_ITALIC;
		} else if (flags & ANSI_BOLD) {
			_font = FONT_MONOSPACE_BOLD;
		}
		freetype_set_font_face(_font);
		freetype_set_font_size(font_size);
		if (_no_frame) {
			freetype_draw_char(ctx, x, y + char_offset, _fg, val);
		} else {
			freetype_draw_char(ctx, x + decor_left_width, y + char_offset + decor_top_height + menu_bar_height, _fg, val);
		}
	} else {
		/* Convert other unicode characters. */
		if (val > 128) {
			val = ununicode(val);
		}
		/* Draw using the bitmap font. */
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

/* Set a terminal cell */
static void cell_set(uint16_t x, uint16_t y, uint32_t c, uint32_t fg, uint32_t bg, uint32_t flags) {
	/* Avoid setting cells out of range. */
	if (x >= term_width || y >= term_height) return;

	/* Calculate the cell position in the terminal buffer */
	term_cell_t * cell = (term_cell_t *)((uintptr_t)term_buffer + (y * term_width + x) * sizeof(term_cell_t));

	/* Set cell attributes */
	cell->c     = c;
	cell->fg    = fg;
	cell->bg    = bg;
	cell->flags = flags;
}

/* Redraw an embedded image cell */
static void redraw_cell_image(uint16_t x, uint16_t y, term_cell_t * cell) {
	/* Avoid setting cells out of range. */
	if (x >= term_width || y >= term_height) return;

	/* Draw the image data */
	uint32_t * data = (uint32_t *)cell->fg;
	for (uint32_t yy = 0; yy < char_height; ++yy) {
		for (uint32_t xx = 0; xx < char_width; ++xx) {
			term_set_point(x * char_width + xx, y * char_height + yy, *data);
			data++;
		}
	}

	/* Update bounds */
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

static void cell_redraw_offset(uint16_t x, uint16_t _y) {
	int y = _y;
	int i = y;

	y -= scrollback_offset;

	if (y >= 0) {
		term_cell_t * cell = (term_cell_t *)((uintptr_t)term_buffer + (y * term_width + x) * sizeof(term_cell_t));
		if (cell->flags & ANSI_EXT_IMG) { redraw_cell_image(x,i,cell); return; }
		if (((uint32_t *)cell)[0] == 0x00000000) {
			term_write_char(' ', x * char_width, i * char_height, TERM_DEFAULT_FG, TERM_DEFAULT_BG, TERM_DEFAULT_FLAGS);
		} else {
			term_write_char(cell->c, x * char_width, i * char_height, cell->fg, cell->bg, cell->flags);
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
				if (!cell || ((uint32_t *)cell)[0] == 0x00000000) {
					term_write_char(' ', x * char_width, i * char_height, TERM_DEFAULT_FG, TERM_DEFAULT_BG, TERM_DEFAULT_FLAGS);
				} else {
					term_write_char(cell->c, x * char_width, i * char_height, cell->fg, cell->bg, cell->flags);
				}
			} else {
				term_write_char(' ', x * char_width, i * char_height, TERM_DEFAULT_FG, TERM_DEFAULT_BG, TERM_DEFAULT_FLAGS);
			}
		}
	}
}

static void cell_redraw_offset_inverted(uint16_t x, uint16_t _y) {
	int y = _y;
	int i = y;

	y -= scrollback_offset;

	if (y >= 0) {
		term_cell_t * cell = (term_cell_t *)((uintptr_t)term_buffer + (y * term_width + x) * sizeof(term_cell_t));
		if (cell->flags & ANSI_EXT_IMG) { redraw_cell_image(x,i,cell); return; }
		if (((uint32_t *)cell)[0] == 0x00000000) {
			term_write_char(' ', x * char_width, i * char_height, TERM_DEFAULT_BG, TERM_DEFAULT_FG, TERM_DEFAULT_FLAGS|ANSI_SPECBG);
		} else {
			term_write_char(cell->c, x * char_width, i * char_height, cell->bg, cell->fg, cell->flags);
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
				if (!cell || ((uint32_t *)cell)[0] == 0x00000000) {
					term_write_char(' ', x * char_width, i * char_height, TERM_DEFAULT_BG, TERM_DEFAULT_FG, TERM_DEFAULT_FLAGS);
				} else {
					term_write_char(cell->c, x * char_width, i * char_height, cell->bg, cell->fg, cell->flags);
				}
			} else {
				term_write_char(' ', x * char_width, i * char_height, TERM_DEFAULT_BG, TERM_DEFAULT_FG, TERM_DEFAULT_FLAGS);
			}
		}
	}
}


/* Redraw a text cell normally. */
static void cell_redraw(uint16_t x, uint16_t y) {
	/* Avoid cells out of range. */
	if (x >= term_width || y >= term_height) return;

	/* Calculate the cell position in the terminal buffer */
	term_cell_t * cell = (term_cell_t *)((uintptr_t)term_buffer + (y * term_width + x) * sizeof(term_cell_t));

	/* If it's an image cell, redraw the image data. */
	if (cell->flags & ANSI_EXT_IMG) {
		redraw_cell_image(x,y,cell);
		return;
	}

	/* Special case empty cells. */
	if (((uint32_t *)cell)[0] == 0x00000000) {
		term_write_char(' ', x * char_width, y * char_height, TERM_DEFAULT_FG, TERM_DEFAULT_BG, TERM_DEFAULT_FLAGS);
	} else {
		term_write_char(cell->c, x * char_width, y * char_height, cell->fg, cell->bg, cell->flags);
	}
}

/* Redraw text cell inverted. */
static void cell_redraw_inverted(uint16_t x, uint16_t y) {
	/* Avoid cells out of range. */
	if (x >= term_width || y >= term_height) return;

	/* Calculate the cell position in the terminal buffer */
	term_cell_t * cell = (term_cell_t *)((uintptr_t)term_buffer + (y * term_width + x) * sizeof(term_cell_t));

	/* If it's an image cell, redraw the image data. */
	if (cell->flags & ANSI_EXT_IMG) {
		redraw_cell_image(x,y,cell);
		return;
	}

	/* Special case empty cells. */
	if (((uint32_t *)cell)[0] == 0x00000000) {
		term_write_char(' ', x * char_width, y * char_height, TERM_DEFAULT_BG, TERM_DEFAULT_FG, TERM_DEFAULT_FLAGS | ANSI_SPECBG);
	} else {
		term_write_char(cell->c, x * char_width, y * char_height, cell->bg, cell->fg, cell->flags | ANSI_SPECBG);
	}
}

/* Redraw text cell with a surrounding box (used by cursor) */
static void cell_redraw_box(uint16_t x, uint16_t y) {
	/* Avoid cells out of range. */
	if (x >= term_width || y >= term_height) return;

	/* Calculate the cell position in the terminal buffer */
	term_cell_t * cell = (term_cell_t *)((uintptr_t)term_buffer + (y * term_width + x) * sizeof(term_cell_t));

	/* If it's an image cell, redraw the image data. */
	if (cell->flags & ANSI_EXT_IMG) {
		redraw_cell_image(x,y,cell);
		return;
	}

	/* Special case empty cells. */
	if (((uint32_t *)cell)[0] == 0x00000000) {
		term_write_char(' ', x * char_width, y * char_height, TERM_DEFAULT_FG, TERM_DEFAULT_BG, TERM_DEFAULT_FLAGS | ANSI_BORDER);
	} else {
		term_write_char(cell->c, x * char_width, y * char_height, cell->fg, cell->bg, cell->flags | ANSI_BORDER);
	}
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
	mouse_ticks = get_ticks();
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
		display_flip();
		cursor_flipped = 1 - cursor_flipped;
	}
}

/* Draw all cells. Duplicates code from cell_redraw to avoid unecessary bounds checks. */
static void term_redraw_all() {
	for (int i = 0; i < term_height; i++) {
		for (int x = 0; x < term_width; ++x) {
			/* Calculate the cell position in the terminal buffer */
			term_cell_t * cell = (term_cell_t *)((uintptr_t)term_buffer + (i * term_width + x) * sizeof(term_cell_t));
			/* If it's an image cell, redraw the image data. */
			if (cell->flags & ANSI_EXT_IMG) {
				redraw_cell_image(x,i,cell);
				continue;
			}

			/* Special case empty cells. */
			if (((uint32_t *)cell)[0] == 0x00000000) {
				term_write_char(' ', x * char_width, i * char_height, TERM_DEFAULT_FG, TERM_DEFAULT_BG, TERM_DEFAULT_FLAGS);
			} else {
				term_write_char(cell->c, x * char_width, i * char_height, cell->fg, cell->bg, cell->flags);
			}
		}
	}
}

static void _menu_action_redraw(struct MenuEntry * self) {
	term_redraw_all();
}

/* Remove no-longer-visible image cell data. */
static void flush_unused_images(void) {
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
		/* Move displayed as well */
		cell_redraw(csr_x, csr_y); /* Otherwise we may copy the inverted cursor */
		uintptr_t dst = (uintptr_t)ctx->backbuffer + GFX_W(ctx) * (destination / term_width * char_height) * GFX_B(ctx);
		uintptr_t src = (uintptr_t)ctx->backbuffer + GFX_W(ctx) * (source / term_width * char_height) * GFX_B(ctx);
		if (!_no_frame) {
			dst += (GFX_W(ctx) * (decor_top_height + menu_bar_height) + decor_left_width) * GFX_B(ctx);
			src += (GFX_W(ctx) * (decor_top_height + menu_bar_height) + decor_left_width) * GFX_B(ctx);
			if (dst < src) {
				for (int i = 0; i < count * char_height; ++i) {
					memmove((void*)(dst + i * GFX_W(ctx) * GFX_B(ctx)), (void*)(src + i * GFX_W(ctx) * GFX_B(ctx)), term_width * char_width * GFX_B(ctx));
				}
			} else {
				for (int i = (count - 1) * char_height; i >= 0; --i) {
					memmove((void*)(dst + i * GFX_W(ctx) * GFX_B(ctx)), (void*)(src + i * GFX_W(ctx) * GFX_B(ctx)), term_width * char_width * GFX_B(ctx));
				}
			}
		} else {
			size_t siz = count * char_height * GFX_W(ctx) * GFX_B(ctx);
			memmove((void*)dst, (void*)src, siz);
		}
	}

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

	/* Flip the entire window. */
	yutani_flip(yctx, window);
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
	if (scrollback_list->length == MAX_SCROLLBACK) {
		node_t * n = list_dequeue(scrollback_list);
		free(n->value);
		free(n);
	}

	struct scrollback_row * row = malloc(sizeof(struct scrollback_row) + sizeof(term_cell_t) * term_width + 20);
	row->width = term_width;
	for (int i = 0; i < term_width; ++i) {
		term_cell_t * cell = (term_cell_t *)((uintptr_t)term_buffer + (i) * sizeof(term_cell_t));
		memcpy(&row->cells[i], cell, sizeof(term_cell_t));
	}

	list_insert(scrollback_list, row);
}

/* Draw the scrollback. */
static void redraw_scrollback(void) {
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

/*
 * ANSI callback for writing characters.
 * Parses some things (\n\r, etc.) itself that should probably
 * be moved into the ANSI library.
 */
static void term_write(char c) {
	static uint32_t unicode_state = 0;
	static uint32_t codepoint = 0;

	cell_redraw(csr_x, csr_y);

	if (!decode(&unicode_state, &codepoint, (uint8_t)c)) {
		uint32_t o = codepoint;
		codepoint = 0;
		if (c == '\r') {
			csr_x = 0;
			draw_cursor();
			return;
		}
		if (csr_x < 0) csr_x = 0;
		if (csr_y < 0) csr_y = 0;
		if (csr_x == term_width) {
			csr_x = 0;
			++csr_y;
			if (c == '\n') return;
		}
		if (csr_y == term_height) {
			save_scrollback();
			term_scroll(1);
			csr_y = term_height - 1;
		}
		if (c == '\n') {
			++csr_y;
			if (csr_y == term_height) {
				save_scrollback();
				term_scroll(1);
				csr_y = term_height - 1;
			}
			draw_cursor();
		} else if (c == '\007') {
			/* bell */
			/* XXX play sound */
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

/* ANSI callback to set cursor position */
static void term_set_csr(int x, int y) {
	cell_redraw(csr_x,csr_y);
	csr_x = x;
	csr_y = y;
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
	cell_set(x, y, ' ', (uint32_t)cell_data, 0, ANSI_EXT_IMG);
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
		display_flip();
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
	display_flip();
	if (scrollback_offset != 0) {
		scrollback_offset = 0;
		term_redraw_all();
	}
}

static void handle_input_s(char * c) {
	size_t len = strlen(c);
	write_input_buffer(c, len);
	display_flip();
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
					window_width = window->width - decor_width * (!_no_frame);
					window_height = window->height - (decor_height + menu_bar_height) * (!_no_frame);
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
			term_cell_t * old_cell = (term_cell_t *)((uintptr_t)term_buffer + ((row + offset) * old_width + col) * sizeof(term_cell_t));
			term_cell_t * new_cell = (term_cell_t *)((uintptr_t)new_term_buffer + (row * term_width + col) * sizeof(term_cell_t));
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
	if (_use_aa && !_have_freetype) {
		char_width = 9;
		char_height = 17;
		font_size = 16;
		if (scale_fonts) {
			font_size   *= font_scaling;
			char_height *= font_scaling;
			char_width  *= font_scaling;
		}
	} else if (_use_aa && _have_freetype) {
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
	} else {
		char_width = 9;
		char_height = 20;
	}

	int old_width  = term_width;
	int old_height = term_height;

	/* Resize the terminal buffer */
	term_width  = window_width  / char_width;
	term_height = window_height / char_height;
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

	/* Reset the ANSI library, ensuring we keep certain values */
	int old_mouse_state = 0;
	if (ansi_state) old_mouse_state = ansi_state->mouse_on;
	ansi_state = ansi_init(ansi_state, term_width, term_height, &term_callbacks);
	ansi_state->mouse_on = old_mouse_state;

	/* Redraw the window */
	draw_fill(ctx, rgba(0,0,0, TERM_DEFAULT_OPAC));
	render_decors();
	term_redraw_all();
	display_flip();

	/* Send window size change ioctl */
	struct winsize w;
	w.ws_row = term_height;
	w.ws_col = term_width;
	w.ws_xpixel = term_width * char_width;
	w.ws_ypixel = term_height * char_height;
	ioctl(fd_master, TIOCSWINSZ, &w);
}

static void update_bounds(void) {
	struct decor_bounds bounds;
	decor_get_bounds(window, &bounds);

	decor_left_width = bounds.left_width;
	decor_top_height = bounds.top_height;
	decor_right_width = bounds.right_width;
	decor_bottom_height = bounds.bottom_height;
	decor_width = bounds.width;
	decor_height = bounds.height;
}

/* Handle window resize event. */
static void resize_finish(int width, int height) {
	static int resize_attempts = 0;

	int extra_x = 0;
	int extra_y = 0;

	/* Calculate window size */
	if (!_no_frame) {
		update_bounds();

		extra_x = decor_width;
		extra_y = decor_height + menu_bar_height;
	}

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
					yutani_window_t * win = hashmap_get(yctx->windows, (void*)wf->wid);
					if (win == window) {
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

					if (me->new_x < 0 || me->new_y < 0) break;
					if (!_no_frame) {
						if (me->new_x >= (int)window_width + (int)decor_width) break;
						if (me->new_y < (int)decor_top_height+menu_bar_height) break;
						if (me->new_y >= (int)(window_height + decor_top_height+menu_bar_height)) break;
						if (me->new_x < (int)decor_left_width) break;
						if (me->new_x >= (int)(window_width + decor_left_width)) break;
					} else {
						if (me->new_x >= (int)window_width) break;
						if (me->new_y >= (int)window_height) break;
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
							selection_start_x = new_x;
							selection_start_y = new_y;
							selection_end_x = new_x;
							selection_end_y = new_y;
							selection = 1;
							redraw_selection();
							display_flip();
						}
						if (me->command == YUTANI_MOUSE_EVENT_DRAG && me->buttons & YUTANI_MOUSE_BUTTON_LEFT ){
							int old_end_x = selection_end_x;
							int old_end_y = selection_end_y;
							selection_end_x = new_x;
							selection_end_y = new_y;
							redraw_new_selection(old_end_x, old_end_y);
							display_flip();
						}
						if (me->command == YUTANI_MOUSE_EVENT_RAISE) {
							if (me->new_x == me->old_x && me->new_y == me->old_y) {
								selection = 0;
								term_redraw_all();
								redraw_scrollback();
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
								if (window->x + me->new_x + menu_right_click->window->width > yctx->display_width) {
									yutani_window_move(yctx, menu_right_click->window, window->x + me->new_x - menu_right_click->window->width, window->y + me->new_y);
								} else {
									yutani_window_move(yctx, menu_right_click->window, window->x + me->new_x, window->y + me->new_y);
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
	window_width = window->width - decor_width * (!_no_frame);
	window_height = window->height - (decor_height + menu_bar_height) * (!_no_frame);
	menu_update_title(_menu_toggle_borders_context, _no_frame ? "Show borders" : "Hide borders");
	menu_update_title(_menu_toggle_borders_bar, _no_frame ? "Show borders" : "Hide borders");
	reinit();
}

static void _menu_action_toggle_sdf(struct MenuEntry * self) {
	_use_aa = !(_use_aa);
	menu_update_title(self, _use_aa ? "Bitmap font" : "Anti-aliased font");
	reinit();
}

static void _menu_action_toggle_free_size(struct MenuEntry * self) {
	_free_size = !(_free_size);
	menu_update_title(self, _free_size ? "Snap to Cell Size" : "Freely Resize");
}

static void _menu_action_show_about(struct MenuEntry * self) {
	char about_cmd[1024] = "\0";
	strcat(about_cmd, "about \"About Terminal\" /usr/share/icons/48/utilities-terminal.png \"ToaruOS Terminal\" \"(C) 2013-2020 K. Lange\n-\nPart of ToaruOS, which is free software\nreleased under the NCSA/University of Illinois\nlicense.\n-\n%https://toaruos.org\n%https://github.com/klange/toaruos\" ");
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

int main(int argc, char ** argv) {

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
		{"no-ft",      no_argument,       0, 'f'},
		{0,0,0,0}
	};

	/* Read some arguments */
	int index, c;
	while ((c = getopt_long(argc, argv, "bhxnfFls:g:", long_opts, &index)) != -1) {
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
			case 'f':
				_force_no_ft = 1;
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

	if (!_force_no_ft) {
		void * freetype = dlopen("libtoaru_ext_freetype_fonts.so", 0);
		if (freetype) {
			_have_freetype = 1;
			freetype_set_font_face = dlsym(freetype, "freetype_set_font_face");
			freetype_set_font_size = dlsym(freetype, "freetype_set_font_size");
			freetype_draw_char   = dlsym(freetype, "freetype_draw_char");
		}
	}

	/* Initialize the windowing library */
	yctx = yutani_init();

	if (!yctx) {
		fprintf(stderr, "%s: failed to connect to compositor\n", argv[0]);
		return 1;
	}

	/* Full screen mode forces window size to be that the display server */
	if (_fullscreen) {
		window_width = yctx->display_width;
		window_height = yctx->display_height;
	}

	if (_no_frame) {
		window = yutani_window_create(yctx, window_width, window_height);
	} else {
		init_decorations();
		struct decor_bounds bounds;
		decor_get_bounds(NULL, &bounds);
		window = yutani_window_create(yctx, window_width + bounds.width, window_height + bounds.height + menu_bar_height);
		update_bounds();
	}

	if (_fullscreen) {
		/* If fullscreen, assume we're always focused and put us on the bottom. */
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
	if (!_fullscreen) {
		menu_insert(menu_right_click, menu_create_separator());
		_menu_toggle_borders_context = menu_create_normal(NULL, NULL, _no_frame ? "Show borders" : "Hide borders", _menu_action_hide_borders);
		menu_insert(menu_right_click, _menu_toggle_borders_context);
	}
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
	_menu_toggle_borders_bar = menu_create_normal(NULL, NULL, _no_frame ? "Show borders" : "Hide borders", _menu_action_hide_borders);
	menu_insert(m, _menu_toggle_borders_bar);
	menu_insert(m, menu_create_submenu(NULL,"zoom","Set zoom..."));
	menu_insert(m, menu_create_normal(NULL, NULL, _use_aa ? "Bitmap font" : "Anti-aliased font", _menu_action_toggle_sdf));
	menu_insert(m, menu_create_normal(NULL, NULL, _free_size ? "Snap to Cell Size" : "Freely Resize", _menu_action_toggle_free_size));
	menu_insert(m, menu_create_separator());
	menu_insert(m, menu_create_normal(NULL, NULL, "Redraw", _menu_action_redraw));
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

	/* Move window to screen center (XXX maybe remove this and do better window placement elsewhere */
	yutani_window_move(yctx, window, yctx->display_width / 2 - window->width / 2, yctx->display_height / 2 - window->height / 2);

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

		while (!exit_application) {

			/* Wait for something to happen. */
			int res[] = {0,0};
			fswait3(2,fds,200,res);

			/* Check if the child application has closed. */
			check_for_exit();
			maybe_flip_cursor();

			if (res[1]) {
				/* Read from PTY */
				int r = read(fd_master, buf, 4096);
				for (int i = 0; i < r; ++i) {
					ansi_put(ansi_state, buf[i]);
				}
				display_flip();
			}
			if (res[0]) {
				/* Handle Yutani events. */
				handle_incoming();
			}
		}
	}

	close(input_buffer_semaphore[1]);

	/* Windows will close automatically on exit. */
	return 0;
}
