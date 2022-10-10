/* Bim - A Text Editor
 *
 * Copyright (C) 2012-2021 K. Lange
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include "bim.h"

#define BIM_VERSION   "3.1.0" TAG
#define BIM_COPYRIGHT "Copyright 2012-2021 K. Lange <\033[3mklange@toaruos.org\033[23m>"

#include <kuroko/kuroko.h>
#include <kuroko/vm.h>
#include <kuroko/debug.h>
#include <kuroko/util.h>
#include <kuroko/scanner.h>

global_config_t global_config = {
	/* State */
	.term_width = 0,
	.term_height = 0,
	.bottom_size = 2,
	.yanks = NULL,
	.yank_count = 0,
	.yank_is_full_lines = 0,
	.tty_in = STDIN_FILENO,
	.bimrc_path = "~/.bim3rc",
	.syntax_fallback = NULL, /* Syntax to fall back to if no other match applies */
	.search = NULL,
	.overlay_mode = OVERLAY_MODE_NONE,
	.command_buffer = NULL,
	.command_offset = 0,
	.command_col_no = 0,
	.history_point = -1,
	.search_point = -1,
	/* Bitset starts here */
	.highlight_on_open = 1,
	.initial_file_is_read_only = 0,
	.go_to_line = 1,
	.break_from_selection = 1,
	/* Terminal capabilities */
	.can_scroll = 1,
	.can_hideshow = 1,
	.can_altscreen = 1,
	.can_mouse = 1,
	.can_unicode = 1,
	.can_bright = 1,
	.can_title = 1,
	.can_bce = 1,
	.can_24bit = 1, /* can use 24-bit color */
	.can_256color = 1, /* can use 265 colors */
	.can_italic = 1, /* can use italics (without inverting) */
	.can_insert = 0, /* ^[[L */
	.can_bracketedpaste = 0, /* puts escapes before and after pasted stuff */
	.can_sgrmouse = 0, /* Whether SGR mouse mode is availabe (large coordinates) */
	/* Configuration options */
	.history_enabled = 1,
	.highlight_parens = 1, /* highlight parens/braces when cursor moves */
	.smart_case = 1, /* smart case-sensitivity while searching */
	.highlight_current_line = 1,
	.shift_scrolling = 1, /* shift rather than moving cursor*/
	.check_git = 0,
	.color_gutter = 1, /* shows modified lines */
	.relative_lines = 0,
	.numbers = 1,
	.horizontal_shift_scrolling = 0, /* Whether to shift the whole screen when scrolling horizontally */
	.hide_statusbar = 0,
	.tabs_visible = 1,
	.autohide_tabs = 0,
	.smart_complete = 0,
	.has_terminal = 0,
	.search_wraps = 1,
	.had_error = 0,
	.use_biminfo = 1,
	/* Integer config values */
	.cursor_padding = 4,
	.split_percent = 50,
	.scroll_amount = 5,
	.tab_offset = 0,
	.background_task = NULL,
	.tail_task = NULL,
};

struct key_name_map KeyNames[] = {
	{KEY_TIMEOUT, "[timeout]"},
	{KEY_BACKSPACE, "<backspace>"},
	{KEY_ENTER, "<enter>"},
	{KEY_ESCAPE, "<escape>"},
	{KEY_TAB, "<tab>"},
	{' ', "<space>"},
	/* These are mostly here for markdown output. */
	{'`', "<backtick>"},
	{'|', "<pipe>"},
	{KEY_DELETE, "<del>"},
	{KEY_MOUSE, "<mouse>"},
	{KEY_MOUSE_SGR, "<mouse-sgr>"},
	{KEY_F1, "<f1>"},{KEY_F2, "<f2>"},{KEY_F3, "<f3>"},{KEY_F4, "<f4>"},
	{KEY_F5, "<f5>"},{KEY_F6, "<f6>"},{KEY_F7, "<f7>"},{KEY_F8, "<f8>"},
	{KEY_F9, "<f9>"},{KEY_F10, "<f10>"},{KEY_F11, "<f11>"},{KEY_F12, "<f12>"},
	{KEY_HOME,"<home>"},{KEY_END,"<end>"},{KEY_PAGE_UP,"<page-up>"},{KEY_PAGE_DOWN,"<page-down>"},
	{KEY_UP, "<up>"},{KEY_DOWN, "<down>"},{KEY_RIGHT, "<right>"},{KEY_LEFT, "<left>"},
	{KEY_SHIFT_UP, "<shift-up>"},{KEY_SHIFT_DOWN, "<shift-down>"},{KEY_SHIFT_RIGHT, "<shift-right>"},{KEY_SHIFT_LEFT, "<shift-left>"},
	{KEY_CTRL_UP, "<ctrl-up>"},{KEY_CTRL_DOWN, "<ctrl-down>"},{KEY_CTRL_RIGHT, "<ctrl-right>"},{KEY_CTRL_LEFT, "<ctrl-left>"},
	{KEY_ALT_UP, "<alt-up>"},{KEY_ALT_DOWN, "<alt-down>"},{KEY_ALT_RIGHT, "<alt-right>"},{KEY_ALT_LEFT, "<alt-left>"},
	{KEY_ALT_SHIFT_UP, "<alt-shift-up>"},{KEY_ALT_SHIFT_DOWN, "<alt-shift-down>"},{KEY_ALT_SHIFT_RIGHT, "<alt-shift-right>"},{KEY_ALT_SHIFT_LEFT, "<alt-shift-left>"},
	{KEY_SHIFT_TAB,"<shift-tab>"},
	{KEY_PASTE_BEGIN,"<paste-begin>"},{KEY_PASTE_END,"<paste-end>"},
};

char * name_from_key(enum Key keycode) {
	for (unsigned int i = 0;  i < sizeof(KeyNames)/sizeof(KeyNames[0]); ++i) {
		if (KeyNames[i].keycode == keycode) return KeyNames[i].name;
	}
	static char keyNameTmp[8] = {0};
	if (keycode <= KEY_CTRL_UNDERSCORE) {
		keyNameTmp[0] = '^';
		keyNameTmp[1] = '@' + keycode;
		keyNameTmp[2] = 0;
		return keyNameTmp;
	}
	to_eight(keycode, keyNameTmp);
	return keyNameTmp;
}

#define S(c) (krk_copyString(c,sizeof(c)-1))

static KrkClass * syntaxStateClass = NULL;

struct SyntaxState {
	KrkInstance inst;
	struct syntax_state state;
};

static void schedule_complete_recalc(void);

/**
 * Theming data
 *
 * This default set is pretty simple "default foreground on default background"
 * except for search and selections which are black-on-white specifically.
 *
 * The theme colors get set by separate configurable theme scripts.
 */
const char * COLOR_FG        = "@9";
const char * COLOR_BG        = "@9";
const char * COLOR_ALT_FG    = "@9";
const char * COLOR_ALT_BG    = "@9";
const char * COLOR_NUMBER_FG = "@9";
const char * COLOR_NUMBER_BG = "@9";
const char * COLOR_STATUS_FG = "@9";
const char * COLOR_STATUS_BG = "@9";
const char * COLOR_STATUS_ALT= "@9";
const char * COLOR_TABBAR_BG = "@9";
const char * COLOR_TAB_BG    = "@9";
const char * COLOR_ERROR_FG  = "@9";
const char * COLOR_ERROR_BG  = "@9";
const char * COLOR_SEARCH_FG = "@0";
const char * COLOR_SEARCH_BG = "@17";
const char * COLOR_KEYWORD   = "@9";
const char * COLOR_STRING    = "@9";
const char * COLOR_COMMENT   = "@9";
const char * COLOR_TYPE      = "@9";
const char * COLOR_PRAGMA    = "@9";
const char * COLOR_NUMERAL   = "@9";
const char * COLOR_SELECTFG  = "@0";
const char * COLOR_SELECTBG  = "@17";
const char * COLOR_RED       = "@1";
const char * COLOR_GREEN     = "@2";
const char * COLOR_BOLD      = "@9";
const char * COLOR_LINK      = "@9";
const char * COLOR_ESCAPE    = "@9";
const char * current_theme = "none";

struct ColorName color_names[] = {
	{"text-fg", &COLOR_FG},
	{"text-bg", &COLOR_BG},
	{"alternate-fg", &COLOR_ALT_FG},
	{"alternate-bg", &COLOR_ALT_BG},
	{"number-fg", &COLOR_NUMBER_FG},
	{"number-bg", &COLOR_NUMBER_BG},
	{"status-fg", &COLOR_STATUS_FG},
	{"status-bg", &COLOR_STATUS_BG},
	{"status-alt", &COLOR_STATUS_ALT},
	{"tabbar-bg", &COLOR_TABBAR_BG},
	{"tab-bg", &COLOR_TAB_BG},
	{"error-fg", &COLOR_ERROR_FG},
	{"error-bg", &COLOR_ERROR_BG},
	{"search-fg", &COLOR_SEARCH_FG},
	{"search-bg", &COLOR_SEARCH_BG},
	{"keyword", &COLOR_KEYWORD},
	{"string", &COLOR_STRING},
	{"comment", &COLOR_COMMENT},
	{"type", &COLOR_TYPE},
	{"pragma", &COLOR_PRAGMA},
	{"numeral", &COLOR_NUMERAL},
	{"select-fg", &COLOR_SELECTFG},
	{"select-bg", &COLOR_SELECTBG},
	{"red", &COLOR_RED},
	{"green", &COLOR_GREEN},
	{"bold", &COLOR_BOLD},
	{"link", &COLOR_LINK},
	{"escape", &COLOR_ESCAPE},
	{NULL,NULL},
};


#define FLEXIBLE_ARRAY(name, add_name, type, zero) \
	int flex_ ## name ## _count = 0; \
	int flex_ ## name ## _space = 0; \
	type * name = NULL; \
	void add_name (type input) { \
		if (flex_ ## name ## _space == 0) { \
			flex_ ## name ## _space = 4; \
			name = calloc(sizeof(type), flex_ ## name ## _space); \
		} else if (flex_ ## name ## _count + 1 == flex_ ## name ## _space) { \
			flex_ ## name ## _space *= 2; \
			name = realloc(name, sizeof(type) * flex_ ## name ## _space); \
			for (int i = flex_ ## name ## _count; i < flex_ ## name ## _space; ++i) name[i] = zero; \
		} \
		name[flex_ ## name ## _count] = input; \
		flex_ ## name ## _count ++; \
	}

FLEXIBLE_ARRAY(mappable_actions, add_action, struct action_def, ((struct action_def){NULL,0,0,NULL}))
FLEXIBLE_ARRAY(regular_commands, add_command, struct command_def, ((struct command_def){NULL,NULL,NULL}))
FLEXIBLE_ARRAY(prefix_commands, add_prefix_command, struct command_def, ((struct command_def){NULL,NULL,NULL}))
FLEXIBLE_ARRAY(themes, add_colorscheme, struct theme_def, ((struct theme_def){NULL,NULL}))

/**
 * Special implementation of getch with a timeout
 */
int _bim_unget = -1;

void bim_unget(int c) {
	_bim_unget = c;
}

void redraw_statusbar(void);
int bim_getch_timeout(int timeout) {
	fflush(stdout);
	if (_bim_unget != -1) {
		int out = _bim_unget;
		_bim_unget = -1;
		return out;
	}
	struct pollfd fds[1];
	fds[0].fd = global_config.tty_in;
	fds[0].events = POLLIN;
	int ret = poll(fds,1,timeout);
	if (ret > 0 && fds[0].revents & POLLIN) {
		unsigned char buf[1];
		read(global_config.tty_in, buf, 1);
		return buf[0];
	} else {
		background_task_t * task = global_config.background_task;
		if (task) {
			global_config.background_task = task->next;
			task->func(task);
			free(task);
			if (!global_config.background_task) {
				global_config.tail_task = NULL;
				redraw_statusbar();
			}
		}
		return -1;
	}
}

/**
 * UTF-8 parser state
 */
static uint32_t codepoint_r;
static uint32_t state = 0;

#define UTF8_ACCEPT 0
#define UTF8_REJECT 1

static inline uint32_t decode(uint32_t* state, uint32_t* codep, uint32_t byte) {
	static int state_table[32] = {
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 0xxxxxxx */
		1,1,1,1,1,1,1,1,                 /* 10xxxxxx */
		2,2,2,2,                         /* 110xxxxx */
		3,3,                             /* 1110xxxx */
		4,                               /* 11110xxx */
		1                                /* 11111xxx */
	};

	static int mask_bytes[32] = {
		0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,
		0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x1F,0x1F,0x1F,0x1F,
		0x0F,0x0F,
		0x07,
		0x00
	};

	static int next[5] = {
		0,
		1,
		0,
		2,
		3
	};

	if (*state == UTF8_ACCEPT) {
		*codep = byte & mask_bytes[byte >> 3];
		*state = state_table[byte >> 3];
	} else if (*state > 0) {
		*codep = (byte & 0x3F) | (*codep << 6);
		*state = next[*state];
	}
	return *state;
}

#define shift_key(i) _shift_key((i), this_buf, &timeout);
int _shift_key(int i, int this_buf[20], int *timeout) {
	int thing = this_buf[*timeout-1];
	(*timeout) = 0;
	switch (thing) {
		/* There are other combinations we can handle... */
		case '2': return i + 4;
		case '5': return i + 8;
		case '3': return i + 12;
		case '4': return i + 16;
		default: return i;
	}
}

int bim_getkey(int read_timeout) {

	int timeout = 0;
	int this_buf[20];

	int cin;
	uint32_t c;
	uint32_t istate = 0;

	while ((cin = bim_getch_timeout((timeout == 1) ? 50 : read_timeout))) {
		if (cin == -1) {
			if (timeout && this_buf[timeout-1] == '\033')
				return KEY_ESCAPE;
			return KEY_TIMEOUT;
		}

		if (!decode(&istate, &c, cin)) {
			if (timeout == 0) {
				switch (c) {
					case '\033':
						if (timeout == 0) {
							this_buf[timeout] = c;
							timeout++;
						}
						continue;
					case KEY_LINEFEED: return KEY_ENTER;
					case KEY_DELETE: return KEY_BACKSPACE;
				}
				return c;
			} else {
				if (timeout >= 1 && this_buf[timeout-1] == '\033' && c == '\033') {
					bim_unget(c);
					timeout = 0;
					return KEY_ESCAPE;
				}
				if (timeout >= 1 && this_buf[0] == '\033' && c == 'O') {
					this_buf[timeout] = c;
					timeout++;
					continue;
				}
				if (timeout >= 2 && this_buf[0] == '\033' && this_buf[1] == 'O') {
					switch (c) {
						case 'P': return KEY_F1;
						case 'Q': return KEY_F2;
						case 'R': return KEY_F3;
						case 'S': return KEY_F4;
					}
					timeout = 0;
					continue;
				}
				if (timeout >= 1 && this_buf[timeout-1] == '\033' && c != '[') {
					timeout = 0;
					bim_unget(c);
					return KEY_ESCAPE;
				}
				if (timeout >= 1 && this_buf[timeout-1] == '\033' && c == '[') {
					timeout = 1;
					this_buf[timeout] = c;
					timeout++;
					continue;
				}
				if (timeout >= 2 && this_buf[0] == '\033' && this_buf[1] == '[' &&
				    (isdigit(c) || (c == ';'))) {
					this_buf[timeout] = c;
					timeout++;
					continue;
				}
				if (timeout >= 2 && this_buf[0] == '\033' && this_buf[1] == '[') {
					switch (c) {
						case 'M': return KEY_MOUSE;
						case '<': return KEY_MOUSE_SGR;
						case 'A': return shift_key(KEY_UP);
						case 'B': return shift_key(KEY_DOWN);
						case 'C': return shift_key(KEY_RIGHT);
						case 'D': return shift_key(KEY_LEFT);
						case 'H': return KEY_HOME;
						case 'F': return KEY_END;
						case 'I': return KEY_PAGE_UP;
						case 'G': return KEY_PAGE_DOWN;
						case 'Z': return KEY_SHIFT_TAB;
						case '~':
							if (timeout == 3) {
								switch (this_buf[2]) {
									case '1': return KEY_HOME;
									case '3': return KEY_DELETE;
									case '4': return KEY_END;
									case '5': return KEY_PAGE_UP;
									case '6': return KEY_PAGE_DOWN;
								}
							} else if (timeout == 5) {
								if (this_buf[2] == '2' && this_buf[3] == '0' && this_buf[4] == '0') {
									return KEY_PASTE_BEGIN;
								} else if (this_buf[2] == '2' && this_buf[3] == '0' && this_buf[4] == '1') {
									return KEY_PASTE_END;
								}
							} else if (this_buf[2] == '1') {
								switch (this_buf[3]) {
									case '5': return KEY_F5;
									case '7': return KEY_F6;
									case '8': return KEY_F7;
									case '9': return KEY_F8;
								}
							} else if (this_buf[2] == '2') {
								switch (this_buf[3]) {
									case '0': return KEY_F9;
									case '1': return KEY_F10;
									case '3': return KEY_F11;
									case '4': return KEY_F12;
								}
							}
							break;
					}
				}
				timeout = 0;
				continue;
			}
		} else if (istate == UTF8_REJECT) {
			istate = 0;
		}
	}

	return KEY_TIMEOUT;
}

enum Key key_from_name(const char * name) {
	for (unsigned int i = 0;  i < sizeof(KeyNames)/sizeof(KeyNames[0]); ++i) {
		if (!strcmp(KeyNames[i].name, name)) return KeyNames[i].keycode;
	}
	if (name[0] == '^' && name[1] && !name[2]) {
		return name[1] - '@';
	}
	if (!name[1]) return name[0];
	/* Try decoding */
	uint32_t c, state = 0;
	int candidate = -1;
	while (*name) {
		if (!decode(&state, &c, (unsigned char)*name)) {
			if (candidate == -1) candidate = c;
			else return -1; /* Reject `name` if it is multiple codepoints */
		} else if (state == UTF8_REJECT) {
			return -1;
		}
	}
	return candidate;
}


/**
 * Pointer to current active buffer
 */
buffer_t * env = NULL;

buffer_t * left_buffer = NULL;
buffer_t * right_buffer = NULL;

/**
 * A buffer for holding a number (line, repetition count)
 */
#define NAV_BUFFER_MAX 10
char nav_buf[NAV_BUFFER_MAX+1];
int nav_buffer = 0;

/**
 * Available buffers
 */
int    buffers_len = 0;
int    buffers_avail = 0;
buffer_t ** buffers = NULL;

/**
 * Create a new buffer
 */
buffer_t * buffer_new(void) {
	if (buffers_len == buffers_avail) {
		/* If we are out of buffer space, expand the buffers vector */
		buffers_avail *= 2;
		buffers = realloc(buffers, sizeof(buffer_t *) * buffers_avail);
	}

	/* TODO: Clean up split support and support multiple splits... */
	if (left_buffer) {
		left_buffer->left = 0;
		left_buffer->width = global_config.term_width;
		right_buffer->left = 0;
		right_buffer->width = global_config.term_width;
		left_buffer = NULL;
		right_buffer = NULL;
	}

	/* Allocate a new buffer */
	buffers[buffers_len] = malloc(sizeof(buffer_t));
	memset(buffers[buffers_len], 0x00, sizeof(buffer_t));
	buffers[buffers_len]->left = 0;
	buffers[buffers_len]->width = global_config.term_width;
	buffers[buffers_len]->highlighting_paren = -1;
	buffers[buffers_len]->numbers = global_config.numbers;
	buffers[buffers_len]->gutter = 1;
	buffers_len++;
	global_config.tabs_visible = (!global_config.autohide_tabs) || (buffers_len > 1);

	return buffers[buffers_len-1];
}

/**
 * Open the biminfo file.
 */
FILE * open_biminfo(void) {
	if (!global_config.use_biminfo) return NULL;

	char * home = getenv("HOME");
	if (!home) {
		/* ... but since it's not, we need $HOME, so fail if it isn't set. */
		return NULL;
	}

	/* biminfo lives at ~/.biminfo */
	char biminfo_path[PATH_MAX+1] = {0};
	sprintf(biminfo_path,"%s/.biminfo",home);

	/* Try to open normally first... */
	FILE * biminfo = fopen(biminfo_path,"r+");
	if (!biminfo) {
		/* Otherwise, try to create it. */
		biminfo = fopen(biminfo_path,"w+");
	}
	return biminfo;
}

/**
 * Check if a file is open by examining the biminfo file
 */
int file_is_open(char * file_name) {
	/* Get the absolute path of the file to normalize for lookup */
	char tmp_path[PATH_MAX+2];
	if (!realpath(file_name, tmp_path)) {
		return 0; /* Assume not */
	}
	strcat(tmp_path," ");

	FILE * biminfo = open_biminfo();
	if (!biminfo) return 0; /* Assume not */

	/* Scan */
	char line[PATH_MAX+64];

	while (!feof(biminfo)) {
		fpos_t start_of_line;
		fgetpos(biminfo, &start_of_line);
		fgets(line, PATH_MAX+63, biminfo);
		if (line[0] != '%') {
			continue;
		}

		if (!strncmp(&line[1],tmp_path, strlen(tmp_path))) {
			/* File is currently open */
			int pid = -1;
			sscanf(line+1+strlen(tmp_path)+1,"%d",&pid);
			if (pid != -1 && pid != getpid()) {
				if (!kill(pid, 0)) {
					int key = 0;
					render_error("biminfo indicates another instance may already be editing this file");
					render_commandline_message("\n");
					render_commandline_message("file path = %s\n", tmp_path);
					render_commandline_message("pid = %d (still running)\n", pid);
					render_commandline_message("Open file anyway? (y/N)");
					while ((key = bim_getkey(DEFAULT_KEY_WAIT)) == KEY_TIMEOUT);
					if (key != 'y') {
						fclose(biminfo);
						return 1;
					}
				}
			}
			fclose(biminfo);
			return 0;
		}
	}
	fclose(biminfo);
	return 0;
}

/**
 * Fetch the cursor position from a biminfo file
 */
int fetch_from_biminfo(buffer_t * buf) {
	/* Can't fetch if we don't have a filename */
	if (!buf->file_name) return 1;

	/* Get the absolute path of the file to normalize for lookup */
	char tmp_path[PATH_MAX+2];
	if (!realpath(buf->file_name, tmp_path)) {
		return 1;
	}
	strcat(tmp_path," ");

	FILE * biminfo = open_biminfo();
	if (!biminfo) return 1;

	/* Scan */
	char line[PATH_MAX+64];

	while (!feof(biminfo)) {
		fpos_t start_of_line;
		fgetpos(biminfo, &start_of_line);
		fgets(line, PATH_MAX+63, biminfo);
		if (line[0] != '>') {
			continue;
		}

		if (!strncmp(&line[1],tmp_path, strlen(tmp_path))) {
			/* Read */
			sscanf(line+1+strlen(tmp_path)+1,"%d",&buf->line_no);
			sscanf(line+1+strlen(tmp_path)+21,"%d",&buf->col_no);

			if (buf->line_no > buf->line_count) buf->line_no = buf->line_count;
			if (buf->col_no > buf->lines[buf->line_no-1]->actual) buf->col_no = buf->lines[buf->line_no-1]->actual;
			try_to_center();

			fclose(biminfo);
			return 0;
		}
	}

	fclose(biminfo);
	return 0;
}

/**
 * Write a file containing the last cursor position of a buffer.
 */
int update_biminfo(buffer_t * buf, int is_open) {
	if (!buf->file_name) return 1;

	/* Get the absolute path of the file to normalize for lookup */
	char tmp_path[PATH_MAX+1];
	if (!realpath(buf->file_name, tmp_path)) {
		return 1;
	}
	strcat(tmp_path," ");

	FILE * biminfo = open_biminfo();
	if (!biminfo) return 1;

	/* Scan */
	char line[PATH_MAX+64];

	while (!feof(biminfo)) {
		fpos_t start_of_line;
		fgetpos(biminfo, &start_of_line);
		fgets(line, PATH_MAX+63, biminfo);
		if (line[0] != '>' && line[0] != '%') {
			continue;
		}

		if (!strncmp(&line[1],tmp_path, strlen(tmp_path))) {
			/* Update */
			fsetpos(biminfo, &start_of_line);
			fprintf(biminfo,"%c%s %20d %20d\n", is_open ? '%' : '>', tmp_path,
				is_open ? getpid() : buf->line_no, buf->col_no);
			goto _done;
		}
	}

	if (ftell(biminfo) == 0) {
		/* New biminfo */
		fprintf(biminfo, "# This is a biminfo file.\n");
		fprintf(biminfo, "# It was generated by bim. Do not edit it by hand!\n");
		fprintf(biminfo, "# Cursor positions and other state are stored here.\n");
	}

	/* If we reach this point, we didn't find a record for this file
	 * and the write cursor should be at the end, so just add a new line */
	fprintf(biminfo,"%c%s %20d %20d\n", is_open ? '%' : '>', tmp_path,
		is_open ? getpid() : buf->line_no, buf->col_no);

_done:
	fclose(biminfo);
	return 0;
}

void cancel_background_tasks(buffer_t * buf) {
	background_task_t * t = global_config.background_task;
	background_task_t * last = NULL;
	while (t) {
		if (t->env == buf) {
			if (last) {
				last->next = t->next;
			} else {
				global_config.background_task = t->next;
			}
			if (!t->next) {
				global_config.tail_task = last;
			}
			background_task_t * tmp = t->next;
			free(t);
			t = tmp;
		} else {
			last = t;
			t = t->next;
		}
	}
}

/**
 * Close a buffer
 */
buffer_t * buffer_close(buffer_t * buf) {
	int i;

	/* Locate the buffer in the buffer list */
	for (i = 0; i < buffers_len; i++) {
		if (buf == buffers[i])
			break;
	}

	/* This buffer doesn't exist? */
	if (i == buffers_len) {
		return NULL;
	}

	/* Cancel any background tasks for this env */
	cancel_background_tasks(buf);

	update_biminfo(buf, 0);

	/* Clean up lines used by old buffer */
	for (int i = 0; i < buf->line_count; ++i) {
		free(buf->lines[i]);
	}

	free(buf->lines);

	if (buf->file_name) {
		free(buf->file_name);
	}

	history_t * h = buf->history;
	while (h->next) {
		h = h->next;
	}
	while (h) {
		history_t * x = h->previous;
		free(h);
		h = x;
	}

	/* Clean up the old buffer */
	free(buf);

	/* Remove the buffer from the vector, moving others up */
	if (i != buffers_len - 1) {
		memmove(&buffers[i], &buffers[i+1], sizeof(*buffers) * (buffers_len - i - 1));
	}

	/* There is one less buffer */
	buffers_len--;
	if (buffers_len && global_config.tab_offset >= buffers_len) global_config.tab_offset--;
	global_config.tabs_visible = (!global_config.autohide_tabs) || (buffers_len > 1);
	if (!buffers_len) { 
		/* There are no more buffers. */
		return NULL;
	}

	/* If this was the last buffer in the list, return the previous last buffer */
	if (i == buffers_len) {
		return buffers[buffers_len-1];
	}

	/* Otherwise return the buffer in the same location */
	return buffers[i];
}

/**
 * Convert syntax highlighting flag to color code
 */
const char * flag_to_color(int _flag) {
	int flag = _flag & FLAG_MASK_COLORS;
	switch (flag) {
		case FLAG_KEYWORD:
			return COLOR_KEYWORD;
		case FLAG_STRING:
			return COLOR_STRING;
		case FLAG_COMMENT:
			return COLOR_COMMENT;
		case FLAG_TYPE:
			return COLOR_TYPE;
		case FLAG_NUMERAL:
			return COLOR_NUMERAL;
		case FLAG_PRAGMA:
			return COLOR_PRAGMA;
		case FLAG_DIFFPLUS:
			return COLOR_GREEN;
		case FLAG_DIFFMINUS:
			return COLOR_RED;
		case FLAG_SELECT:
			return COLOR_FG;
		case FLAG_BOLD:
			return COLOR_BOLD;
		case FLAG_LINK_COLOR:
			return COLOR_LINK;
		case FLAG_ESCAPE:
			return COLOR_ESCAPE;
		default:
			return COLOR_FG;
	}
}

/**
 * Match and paint a single keyword. Returns 1 if the keyword was matched and 0 otherwise,
 * so it can be used for prefix checking for things that need further special handling.
 */
static int match_and_paint(struct syntax_state * state, const char * keyword, int flag, int (*keyword_qualifier)(int c)) {
	if (keyword_qualifier(lastchar())) return 0;
	if (!keyword_qualifier(charat())) return 0;
	int i = state->i;
	int slen = 0;
	while (i < state->line->actual || *keyword == '\0') {
		if (*keyword == '\0' && (i >= state->line->actual || !keyword_qualifier(state->line->text[i].codepoint))) {
			for (int j = 0; j < slen; ++j) {
				paint(1, flag);
			}
			return 1;
		}
		if (*keyword != state->line->text[i].codepoint) return 0;

		i++;
		keyword++;
		slen++;
	}
	return 0;
}

/**
 * This is a basic character matcher for "keyword" characters.
 */
static int simple_keyword_qualifier(int c) {
	return isalnum(c) || (c == '_');
}

/**
 * These words can appear in comments and should be highlighted.
 * Since there are a lot of comment highlighters, this is provided
 * as a common function that can be used by multiple highlighters.
 */
static int common_comment_buzzwords(struct syntax_state * state) {
	if (match_and_paint(state, "TODO", FLAG_NOTICE, simple_keyword_qualifier)) { return 1; }
	else if (match_and_paint(state, "XXX", FLAG_NOTICE, simple_keyword_qualifier)) { return 1; }
	else if (match_and_paint(state, "FIXME", FLAG_ERROR, simple_keyword_qualifier)) { return 1; }
	return 0;
}

/**
 * Paint a comment until end of line; assumes this comment can not continue.
 * (Some languages have comments that can continue with a \ - don't use this!)
 * Assumes you've already painted your comment start characters.
 */
static int paint_comment(struct syntax_state * state) {
	while (charat() != -1) {
		if (common_comment_buzzwords(state)) continue;
		else { paint(1, FLAG_COMMENT); }
	}
	return -1;
}

/**
 * Find and return a highlighter by name, or return NULL if none was found.
 */
static struct syntax_definition * find_syntax_calculator(const char * name) {
	for (struct syntax_definition * s = syntaxes; syntaxes && s->name; ++s) {
		if (!strcmp(s->name, name)) {
			return s;
		}
	}
	return NULL;
}

int syntax_count = 0;
int syntax_space = 0;
struct syntax_definition * syntaxes = NULL;

void add_syntax(struct syntax_definition def) {
	/* See if a name match already exists for this def. */
	for (struct syntax_definition * s = syntaxes; syntaxes && s->name; ++s) {
		if (!strcmp(def.name,s->name)) {
			*s = def;
			return;
		}
	}

	if (syntax_space == 0) {
		syntax_space = 4;
		syntaxes = calloc(sizeof(struct syntax_definition), syntax_space);
	} else if (syntax_count +1 == syntax_space) {
		syntax_space *= 2;
		syntaxes = realloc(syntaxes, sizeof(struct syntax_definition) * syntax_space);
		for (int i = syntax_count; i < syntax_space; ++i) syntaxes[i].name = NULL;
	}
	syntaxes[syntax_count] = def;
	syntax_count++;
}

void redraw_all(void);

/**
 * Calculate syntax highlighting for the given line, and lines after
 * if their initial syntax state has changed by this recalculation.
 *
 * If `line_no` is -1, this line is taken to be a special line and not
 * part of a buffer; search highlighting will not be processed and syntax
 * highlighting will halt after the line is finished.
 *
 * If `env->slowop` is currently enabled, recalculation is skipped.
 */
void recalculate_syntax(line_t * line, int line_no) {
	if (env->slowop) return;
	/* Clear syntax for this line first */
	int is_original = 1;
	while (1) {
		for (int i = 0; i < line->actual; ++i) {
			line->text[i].flags = line->text[i].flags & (3 << 5);
		}

		if (!env->syntax) {
			if (line_no != -1) rehighlight_search(line);
			return;
		}

		/* Start from the line's stored in initial state */
		struct SyntaxState * s = (void*)krk_newInstance(env->syntax->krkClass);
		s->state.env = env;
		s->state.line = line;
		s->state.line_no = line_no;
		s->state.state = line->istate;
		s->state.i = 0;

		while (1) {
			struct termios old, new;
			tcgetattr(global_config.tty_in, &old);
			new = old; new.c_lflag |= ISIG;
			tcsetattr(global_config.tty_in, TCSANOW, &new);
			ptrdiff_t before = krk_currentThread.stackTop - krk_currentThread.stack;
			krk_push(OBJECT_VAL(env->syntax->krkFunc));
			krk_push(OBJECT_VAL(s));
			KrkValue result = krk_callStack(1);
			tcsetattr(global_config.tty_in, TCSANOW, &old);
			krk_currentThread.stackTop = krk_currentThread.stack + before;
			if (IS_NONE(result) && (krk_currentThread.flags & KRK_THREAD_HAS_EXCEPTION)) {
				render_error("Exception occurred in plugin: %s", AS_INSTANCE(krk_currentThread.currentException)->_class->name->chars);
				render_commandline_message("\n");
				krk_dumpTraceback();
				goto _syntaxError;
			} else if (!IS_NONE(result) && !IS_INTEGER(result)) {
				render_error("Instead of an integer, got %s", krk_typeName(result));
				render_commandline_message("\n");
				goto _syntaxError;
			}
			s->state.state = IS_NONE(result) ? -1 : AS_INTEGER(result);

			if (s->state.state != 0) {
				if (line_no == -1) return;
				rehighlight_search(line);
				if (!is_original) {
					redraw_line(line_no);
				}
				if (line_no + 1 < env->line_count && env->lines[line_no+1]->istate != s->state.state) {
					line_no++;
					line = env->lines[line_no];
					line->istate = s->state.state;
					if (env->loading) return;
					is_original = 0;
					goto _next;
				}
				return;
			}
		}
_next:
		(void)0;
	}

_syntaxError:
	krk_resetStack();
	fprintf(stderr,"This syntax highlighter will be disabled in this environment.");
	env->syntax = NULL;
	cancel_background_tasks(env);
	pause_for_key();
	redraw_all();
}

/**
 * Recalculate tab widths.
 */
void recalculate_tabs(line_t * line) {
	if (env->loading) return;

	int j = 0;
	for (int i = 0; i < line->actual; ++i) {
		if (line->text[i].codepoint == '\t') {
			line->text[i].display_width = env->tabstop - (j % env->tabstop);
		}
		j += line->text[i].display_width;
	}
}

/**
 * The next section contains the basic editing primitives. All other
 * actions are built out of these primitives, and they are the basic
 * instructions that get stored in history to be undone (or redone).
 *
 * Primitives may recalculate syntax or redraw lines, if needed, but only
 * when conditions for redrawing are met (such as not being in `slowop`,
 * or loading the file; also replaying history, or when loading files).
 *
 * At the moment, primitives and most other functions do not take the current
 * buffer (environment) as an argument and instead rely on a global variable;
 * this should definitely be fixed at some point...
 */

/**
 * When a new action that produces history happens and there is forward
 * history that can be redone, we need to erase it as our tree has branched.
 * If we wanted, we could actually story things in a tree structure so that
 * the new branch and the old branch can both stick around, and that should
 * probably be explored in the future...
 */
void history_free(history_t * root) {
	if (!root->next) return;

	/* Find the last entry so we can free backwards */
	history_t * n = root->next;
	while (n->next) {
		n = n->next;
	}

	/* Free everything after the root, including stored content. */
	while (n != root) {
		history_t * p = n->previous;
		switch (n->type) {
			case HISTORY_REPLACE_LINE:
				free(n->contents.remove_replace_line.contents);
				/* fall-through */
			case HISTORY_REMOVE_LINE:
				free(n->contents.remove_replace_line.old_contents);
				break;
			default:
				/* Nothing extra to free */
				break;
		}
		free(n);
		n = p;
	}

	root->next = NULL;
}

/**
 * This macro is called by primitives to insert history elements for each
 * primitive action when performing in edit modes.
 */
#define HIST_APPEND(e) do { \
		e->col = env->col_no; \
		e->line = env->line_no; \
		if (env->history) { \
			e->previous = env->history; \
			history_free(env->history); \
			env->history->next = e; \
			e->next = NULL; \
		} \
		env->history = e; \
	} while (0)

/**
 * Mark a point where a complete set of actions has ended.
 * Individual history entries include things like "insert one character"
 * but a user action that should be undone is "insert several characters";
 * breaks should be inserted after a series of primitives when there is
 * a clear "end", such as when switching out of insert mode after typing.
 */
void set_history_break(void) {
	if (!global_config.history_enabled) return;

	/* Do not produce duplicate breaks, or add breaks if we are at a sentinel. */
	if (env->history->type != HISTORY_BREAK && env->history->type != HISTORY_SENTINEL) {
		history_t * e = malloc(sizeof(history_t));
		e->type = HISTORY_BREAK;
		HIST_APPEND(e);
	}
}

/**
 * (Primitive) Insert a character into an existing line.
 *
 * This is the most basic primitive action: Take a line, and insert a codepoint
 * into it at a given offset. If `lineno` is not -1, the line is assumed to
 * be part of the active buffer. If inserting a character means the line needs
 * to grow, then it will be reallocated, so the return value of the new line
 * must ALWAYS be used. This primitive will NOT automatically update the
 * buffer with the new pointer, so if you are calling insert on a buffer you
 * MUST update env->lines[lineno-1] yourself.
 */
__attribute__((warn_unused_result)) line_t * line_insert(line_t * line, char_t c, int offset, int lineno) {

	if (!env->loading && global_config.history_enabled && lineno != -1) {
		history_t * e = malloc(sizeof(history_t));
		e->type = HISTORY_INSERT;
		e->contents.insert_delete_replace.lineno = lineno;
		e->contents.insert_delete_replace.offset = offset;
		e->contents.insert_delete_replace.codepoint = c.codepoint;
		HIST_APPEND(e);
	}

	/* If there is not enough space... */
	if (line->actual == line->available) {
		/* Expand the line buffer */
		if (line->available == 0) {
			line->available = 8;
		} else {
			line->available *= 2;
		}
		line = realloc(line, sizeof(line_t) + sizeof(char_t) * line->available);
	}

	/* If this was not the last character, then shift remaining characters forward. */
	if (offset < line->actual) {
		memmove(&line->text[offset+1], &line->text[offset], sizeof(char_t) * (line->actual - offset));
	}

	/* Insert the new character */
	line->text[offset] = c;

	/* There is one new character in the line */
	line->actual += 1;

	if (!env->loading) {
		line->rev_status = 2; /* Modified */
		recalculate_tabs(line);
		recalculate_syntax(line, lineno);
	}

	return line;
}

/**
 * (Primitive) Delete a character from a line.
 *
 * Remove the character at the given offset. We never shrink lines, so this
 * does not have a return value, and delete should never be called during
 * a loading operation (though it may be called during a history replay).
 */
void line_delete(line_t * line, int offset, int lineno) {

	/* Can't delete character before start of line. */
	if (offset == 0) return;
	/* Can't delete past end of line either */
	if (offset > line->actual) return;

	if (!env->loading && global_config.history_enabled && lineno != -1) {
		history_t * e = malloc(sizeof(history_t));
		e->type = HISTORY_DELETE;
		e->contents.insert_delete_replace.lineno = lineno;
		e->contents.insert_delete_replace.offset = offset;
		e->contents.insert_delete_replace.old_codepoint = line->text[offset-1].codepoint;
		HIST_APPEND(e);
	}

	/* If this isn't the last character, we need to move all subsequent characters backwards */
	if (offset < line->actual) {
		memmove(&line->text[offset-1], &line->text[offset], sizeof(char_t) * (line->actual - offset));
	}

	/* The line is one character shorter */
	line->actual -= 1;
	line->rev_status = 2;

	recalculate_tabs(line);
	recalculate_syntax(line, lineno);
}

/**
 * (Primitive) Replace a character in a line.
 *
 * Replaces the codepoint at the given offset with a new character. Since this
 * does not involve any size changes, it does not have a return value.
 * Since a replacement character may be a tab, we do still need to recalculate
 * character widths for tabs as they may change.
 */
void line_replace(line_t * line, char_t _c, int offset, int lineno) {

	if (!env->loading && global_config.history_enabled && lineno != -1) {
		history_t * e = malloc(sizeof(history_t));
		e->type = HISTORY_REPLACE;
		e->contents.insert_delete_replace.lineno = lineno;
		e->contents.insert_delete_replace.offset = offset;
		e->contents.insert_delete_replace.codepoint = _c.codepoint;
		e->contents.insert_delete_replace.old_codepoint = line->text[offset].codepoint;
		HIST_APPEND(e);
	}

	line->text[offset] = _c;

	if (!env->loading) {
		line->rev_status = 2; /* Modified */
		recalculate_tabs(line);
		recalculate_syntax(line, lineno);
	}
}

/**
 * (Primitive) Remove a line from the active buffer
 *
 * This primitive is only valid for a buffer. Delete a line, or if this is the
 * only line in the buffer, clear it but keep the line around with no
 * characters. We use the `line_delete` primitive to clear that line,
 * otherwise we are our own primitive and produce history entries.
 *
 * While we do not shrink the `lines` array, it is returned here anyway.
 */
line_t ** remove_line(line_t ** lines, int offset) {

	/* If there is only one line, clear it instead of removing it. */
	if (env->line_count == 1) {
		while (lines[offset]->actual > 0) {
			line_delete(lines[offset], lines[offset]->actual, offset);
		}
		return lines;
	}

	/* When a line is removed, we need to keep its contents so we
	 * can un-remove it on redo... */
	if (!env->loading && global_config.history_enabled) {
		history_t * e = malloc(sizeof(history_t));
		e->type = HISTORY_REMOVE_LINE;
		e->contents.remove_replace_line.lineno = offset;
		e->contents.remove_replace_line.old_contents = malloc(sizeof(line_t) + sizeof(char_t) * lines[offset]->available);
		memcpy(e->contents.remove_replace_line.old_contents, lines[offset], sizeof(line_t) + sizeof(char_t) * lines[offset]->available);
		HIST_APPEND(e);
	}

	/* Otherwise, free the data used by the line */
	free(lines[offset]);

	/* Move other lines up */
	if (offset < env->line_count-1) {
		memmove(&lines[offset], &lines[offset+1], sizeof(line_t *) * (env->line_count - (offset - 1)));
		lines[env->line_count-1] = NULL;
	}

	/* There is one less line */
	env->line_count -= 1;
	return lines;
}

/**
 * (Primitive) Add a new line to the active buffer.
 *
 * Inserts a new line into a buffer at the given line offset.
 * Since this grows the buffer, it will return the new line array
 * after reallocation if needed.
 */
line_t ** add_line(line_t ** lines, int offset) {

	/* Invalid offset? */
	if (offset > env->line_count) return lines;

	if (!env->loading && global_config.history_enabled) {
		history_t * e = malloc(sizeof(history_t));
		e->type = HISTORY_ADD_LINE;
		e->contents.add_merge_split_lines.lineno = offset;
		HIST_APPEND(e);
	}

	/* Not enough space */
	if (env->line_count == env->line_avail) {
		/* Allocate more space */
		env->line_avail *= 2;
		lines = realloc(lines, sizeof(line_t *) * env->line_avail);
	}

	/* If this isn't the last line, move other lines down */
	if (offset < env->line_count) {
		memmove(&lines[offset+1], &lines[offset], sizeof(line_t *) * (env->line_count - offset));
	}

	/* Allocate the new line */
	lines[offset] = calloc(sizeof(line_t) + sizeof(char_t) * 32, 1);
	lines[offset]->available = 32;

	/* There is one new line */
	env->line_count += 1;
	env->lines = lines;

	if (!env->loading) {
		lines[offset]->rev_status = 2; /* Modified */
	}

	if (offset > 0 && !env->loading) {
		recalculate_syntax(lines[offset-1],offset-1);
	}
	return lines;
}

/**
 * (Primitive) Replace a line with data from another line.
 *
 * This is only called when pasting yanks after calling `add_line`,
 * but it allows us to have simpler history for that action.
 */
void replace_line(line_t ** lines, int offset, line_t * replacement) {

	if (!env->loading && global_config.history_enabled) {
		history_t * e = malloc(sizeof(history_t));
		e->type = HISTORY_REPLACE_LINE;
		e->contents.remove_replace_line.lineno = offset;
		e->contents.remove_replace_line.old_contents = malloc(sizeof(line_t) + sizeof(char_t) * lines[offset]->available);
		memcpy(e->contents.remove_replace_line.old_contents, lines[offset], sizeof(line_t) + sizeof(char_t) * lines[offset]->available);
		e->contents.remove_replace_line.contents = malloc(sizeof(line_t) + sizeof(char_t) * replacement->available);
		memcpy(e->contents.remove_replace_line.contents, replacement, sizeof(line_t) + sizeof(char_t) * replacement->available);
		HIST_APPEND(e);
	}

	if (lines[offset]->available < replacement->actual) {
		lines[offset] = realloc(lines[offset], sizeof(line_t) + sizeof(char_t) * replacement->available);
		lines[offset]->available = replacement->available;
	}
	lines[offset]->actual = replacement->actual;
	memcpy(&lines[offset]->text, &replacement->text, sizeof(char_t) * replacement->actual);

	if (!env->loading) {
		lines[offset]->rev_status = 2;
		recalculate_syntax(lines[offset],offset);
	}
}

/**
 * (Primitive) Merge two consecutive lines.
 *
 * Take two lines in a buffer and turn them into one line.
 * `lineb` is the offset of the second line... or the
 * line number of the first line, depending on which indexing
 * system you prefer to think about. This won't grow `lines`,
 * but it will likely modify it and can reallocate individual
 * lines as well.
 */
line_t ** merge_lines(line_t ** lines, int lineb) {

	/* linea is the line immediately before lineb */
	int linea = lineb - 1;

	if (!env->loading && global_config.history_enabled) {
		history_t * e = malloc(sizeof(history_t));
		e->type = HISTORY_MERGE_LINES;
		e->contents.add_merge_split_lines.lineno = lineb;
		e->contents.add_merge_split_lines.split = env->lines[linea]->actual;
		HIST_APPEND(e);
	}

	/* If there isn't enough space in linea hold both... */
	if (lines[linea]->available < lines[linea]->actual + lines[lineb]->actual) {
		while (lines[linea]->available < lines[linea]->actual + lines[lineb]->actual) {
			/* ... allocate more space until it fits */
			if (lines[linea]->available == 0) {
				lines[linea]->available = 8;
			} else {
				lines[linea]->available *= 2;
			}
		}
		lines[linea] = realloc(lines[linea], sizeof(line_t) + sizeof(char_t) * lines[linea]->available);
	}

	/* Copy the second line into the first line */
	memcpy(&lines[linea]->text[lines[linea]->actual], &lines[lineb]->text, sizeof(char_t) * lines[lineb]->actual);

	/* The first line is now longer */
	lines[linea]->actual = lines[linea]->actual + lines[lineb]->actual;

	if (!env->loading) {
		lines[linea]->rev_status = 2;
		recalculate_tabs(lines[linea]);
		recalculate_syntax(lines[linea], linea);
	}

	/* Remove the second line */
	free(lines[lineb]);

	/* Move other lines up */
	if (lineb < env->line_count) {
		memmove(&lines[lineb], &lines[lineb+1], sizeof(line_t *) * (env->line_count - (lineb - 1)));
		lines[env->line_count-1] = NULL;
	}

	/* There is one less line */
	env->line_count -= 1;
	return lines;
}

/**
 * (Primitive) Split a line into two lines at the given column.
 *
 * Takes one line and makes it two lines. There are some optimizations
 * if you are trying to "split" at the first or last column, which
 * are both just treated as add_line.
 */
line_t ** split_line(line_t ** lines, int line, int split) {

	/* If we're trying to split from the start, just add a new blank line before */
	if (split == 0) {
		return add_line(lines, line);
	}

	if (!env->loading && global_config.history_enabled) {
		history_t * e = malloc(sizeof(history_t));
		e->type = HISTORY_SPLIT_LINE;
		e->contents.add_merge_split_lines.lineno = line;
		e->contents.add_merge_split_lines.split  = split;
		HIST_APPEND(e);
	}

	if (!env->loading) {
		unhighlight_matching_paren();
	}

	/* Allocate more space as needed */
	if (env->line_count == env->line_avail) {
		env->line_avail *= 2;
		lines = realloc(lines, sizeof(line_t *) * env->line_avail);
	}

	/* Shift later lines down */
	if (line < env->line_count) {
		memmove(&lines[line+2], &lines[line+1], sizeof(line_t *) * (env->line_count - line));
	}

	int remaining = lines[line]->actual - split;

	/* This is some wacky math to get a good power-of-two */
	int v = remaining;
	v--;
	v |= v >> 1;
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v |= v >> 16;
	v++;

	/* Allocate space for the new line */
	lines[line+1] = calloc(sizeof(line_t) + sizeof(char_t) * v, 1);
	lines[line+1]->available = v;
	lines[line+1]->actual = remaining;

	/* Move the data from the old line into the new line */
	memmove(lines[line+1]->text, &lines[line]->text[split], sizeof(char_t) * remaining);
	lines[line]->actual = split;

	if (!env->loading) {
		lines[line]->rev_status = 2;
		lines[line+1]->rev_status = 2;
		recalculate_tabs(lines[line]);
		recalculate_tabs(lines[line+1]);
		recalculate_syntax(lines[line], line);
		recalculate_syntax(lines[line+1], line+1);
	}

	/* There is one new line */
	env->line_count += 1;

	/* We may have reallocated lines */
	return lines;
}

/**
 * Primitives end here. Everything after this point that wants to
 * perform modifications must be built on these primitives and
 * should never directly modify env->lines or the contents thereof,
 * outside of syntax highlighting flag changes.
 */

/**
 * The following section is where we implement "smart" automatic
 * indentation. A lot of this is hacked-together nonsense and "smart"
 * is a bit of an overstatement.
 */

/**
 * Understand spaces and comments and check if the previous line
 * ended with a brace or a colon.
 */
int line_ends_with_brace(line_t * line) {
	int i = line->actual-1;
	while (i >= 0) {
		if ((line->text[i].flags & 0x1F) == FLAG_COMMENT || line->text[i].codepoint == ' ') {
			i--;
		} else {
			break;
		}
	}
	if (i < 0) return 0;
	return (line->text[i].codepoint == '{' || line->text[i].codepoint == ':') ? i+1 : 0;
}

/**
 * Determine if a given line is a comment by checking its initial
 * syntax highlighting state value. This is used by automatic indentation
 * to continue block comments in languages like C.
 *
 * TODO: Why isn't this a syntax-> method?
 */
int line_is_comment(line_t * line) {
	if (!env->syntax) return 0;

	if (!strcmp(env->syntax->name,"c")) {
		if (line->istate == 1) return 1;
	} else if (!strcmp(env->syntax->name,"java")) {
		if (line->istate == 1) return 1;
	} else if (!strcmp(env->syntax->name,"kotlin")) {
		if (line->istate == 1) return 1;
	} else if (!strcmp(env->syntax->name,"rust")) {
		if (line->istate > 0) return 1;
	}

	return 0;
}

/**
 * Figure out where the indentation for a brace belongs by finding
 * where the start of the line is after whitespace. This is called
 * by default when we insert } and tries to align it with the indentation
 * of the matching opening {.
 */
int find_brace_line_start(int line, int col) {
	int ncol = col - 1;
	while (ncol > 0) {
		if (env->lines[line-1]->text[ncol-1].codepoint == ')') {
			int t_line_no = env->line_no;
			int t_col_no = env->col_no;
			env->line_no = line;
			env->col_no = ncol;
			int paren_match_line = -1, paren_match_col = -1;
			find_matching_paren(&paren_match_line, &paren_match_col, 1);

			if (paren_match_line != -1) {
				line = paren_match_line;
			}

			env->line_no = t_line_no;
			env->col_no = t_col_no;
			break;
		} else if (env->lines[line-1]->text[ncol-1].codepoint == ' ') {
			ncol--;
		} else {
			break;
		}
	}
	return line;
}

/**
 * Add indentation from the previous (temporally) line.
 *
 * By "temporally", we mean not necessarily the line above, but
 * potentially the line below if we are inserting a line above
 * the cursor.
 */
void add_indent(int new_line, int old_line, int ignore_brace) {
	if (env->indent) {
		int changed = 0;
		if (old_line < new_line && line_is_comment(env->lines[new_line])) {
			for (int i = 0; i < env->lines[old_line]->actual; ++i) {
				if (env->lines[old_line]->text[i].codepoint == '/') {
					if (env->lines[old_line]->text[i+1].codepoint == '*') {
						/* Insert ' * ' */
						char_t space = {1,FLAG_COMMENT,' '};
						char_t asterisk = {1,FLAG_COMMENT,'*'};
						env->lines[new_line] = line_insert(env->lines[new_line],space,i,new_line);
						env->lines[new_line] = line_insert(env->lines[new_line],asterisk,i+1,new_line);
						env->lines[new_line] = line_insert(env->lines[new_line],space,i+2,new_line);
						env->col_no += 3;
					}
					break;
				} else if (env->lines[old_line]->text[i].codepoint == ' ' && env->lines[old_line]->text[i+1].codepoint == '*') {
					/* Insert ' * ' */
					char_t space = {1,FLAG_COMMENT,' '};
					char_t asterisk = {1,FLAG_COMMENT,'*'};
					env->lines[new_line] = line_insert(env->lines[new_line],space,i,new_line);
					env->lines[new_line] = line_insert(env->lines[new_line],asterisk,i+1,new_line);
					env->lines[new_line] = line_insert(env->lines[new_line],space,i+2,new_line);
					env->col_no += 3;
					break;
				} else if (env->lines[old_line]->text[i].codepoint == ' ' ||
					env->lines[old_line]->text[i].codepoint == '\t' ||
					env->lines[old_line]->text[i].codepoint == '*') {
					env->lines[new_line] = line_insert(env->lines[new_line],env->lines[old_line]->text[i],i,new_line);
					env->col_no++;
					changed = 1;
				} else {
					break;
				}
			}
		} else {
			int line_to_copy_from = old_line;
			int col;
			if (old_line < new_line &&
				!ignore_brace &&
				(col = line_ends_with_brace(env->lines[old_line])) &&
				env->lines[old_line]->text[col-1].codepoint == '{') {
				line_to_copy_from = find_brace_line_start(old_line+1, col)-1;
			}
			for (int i = 0; i < env->lines[line_to_copy_from]->actual; ++i) {
				if (line_to_copy_from < new_line && i == env->lines[line_to_copy_from]->actual - 3 &&
					env->lines[line_to_copy_from]->text[i].codepoint == ' ' &&
					env->lines[line_to_copy_from]->text[i+1].codepoint == '*' &&
					env->lines[line_to_copy_from]->text[i+2].codepoint == '/') {
					break;
				} else if (env->lines[line_to_copy_from]->text[i].codepoint == ' ' ||
					env->lines[line_to_copy_from]->text[i].codepoint == '\t') {
					env->lines[new_line] = line_insert(env->lines[new_line],env->lines[line_to_copy_from]->text[i],i,new_line);
					env->col_no++;
					changed = 1;
				} else {
					break;
				}
			}
		}
		if (old_line < new_line && !ignore_brace && line_ends_with_brace(env->lines[old_line])) {
			if (env->tabs) {
				char_t c = {0};
				c.codepoint = '\t';
				c.display_width = env->tabstop;
				env->lines[new_line] = line_insert(env->lines[new_line], c, env->col_no-1, new_line);
				env->col_no++;
				changed = 1;
			} else {
				for (int j = 0; j < env->tabstop; ++j) {
					char_t c = {0};
					c.codepoint = ' ';
					c.display_width = 1;
					env->lines[new_line] = line_insert(env->lines[new_line], c, env->col_no-1, new_line);
					env->col_no++;
				}
				changed = 1;
			}
		}
		int was_whitespace = 1;
		for (int i = 0; i < env->lines[old_line]->actual; ++i) {
			if (env->lines[old_line]->text[i].codepoint != ' ' &&
				env->lines[old_line]->text[i].codepoint != '\t') {
				was_whitespace = 0;
				break;
			}
		}
		if (was_whitespace) {
			while (env->lines[old_line]->actual) {
				line_delete(env->lines[old_line], env->lines[old_line]->actual, old_line);
			}
		}
		if (changed) {
			recalculate_syntax(env->lines[new_line],new_line);
		}
	}
}

/**
 * Initialize a buffer with default values.
 *
 * Should be called after creating a buffer.
 */
void setup_buffer(buffer_t * env) {
	/* If this buffer was already initialized, clear out its line data */
	if (env->lines) {
		for (int i = 0; i < env->line_count; ++i) {
			free(env->lines[i]);
		}
		free(env->lines);
	}

	/* Default state parameters */
	env->line_no     = 1; /* Default cursor position */
	env->col_no      = 1;
	env->line_count  = 1; /* Buffers always have at least one line */
	env->modified    = 0;
	env->readonly    = 0;
	env->offset      = 0;
	env->line_avail  = 8; /* Default line buffer capacity */
	env->tabs        = 1; /* Tabs by default */
	env->tabstop     = 4; /* Tab stop width */
	env->indent      = 1; /* Auto-indent by default */
	env->history     = malloc(sizeof(struct history));
	memset(env->history, 0, sizeof(struct history));
	env->last_save_history = env->history;

	/* Allocate line buffer */
	env->lines = malloc(sizeof(line_t *) * env->line_avail);

	/* Initialize the first line */
	env->lines[0] = calloc(sizeof(line_t) + sizeof(char_t) * 32, 1);
	env->lines[0]->available = 32;
}

/**
 * Toggle buffered / unbuffered modes
 */
struct termios old;
void get_initial_termios(void) {
	tcgetattr(STDOUT_FILENO, &old);
}

void set_unbuffered(void) {
	struct termios new = old;
	new.c_iflag &= (~ICRNL) & (~IXON);
	new.c_lflag &= (~ICANON) & (~ECHO) & (~ISIG);
#ifdef VLNEXT
	new.c_cc[VLNEXT] = 0;
#endif
	tcsetattr(STDOUT_FILENO, TCSAFLUSH, &new);
}

void set_buffered(void) {
	tcsetattr(STDOUT_FILENO, TCSAFLUSH, &old);
}

/**
 * Convert codepoint to utf-8 string
 */
int to_eight(uint32_t codepoint, char * out) {
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

/**
 * Get the presentation width of a codepoint
 */
int codepoint_width(wchar_t codepoint) {
	if (codepoint == '\t') {
		return 1; /* Recalculate later */
	}
	if (codepoint < 32) {
		/* We render these as ^@ */
		return 2;
	}
	if (codepoint == 0x7F) {
		/* Renders as ^? */
		return 2;
	}
	if (codepoint > 0x7f && codepoint < 0xa0) {
		/* Upper control bytes <xx> */
		return 4;
	}
	if (codepoint == 0xa0) {
		/* Non-breaking space _ */
		return 1;
	}
	/* Skip wcwidth for anything under 256 */
	if (codepoint > 127) {
		if (global_config.can_unicode) {
			/* Higher codepoints may be wider (eg. Japanese) */
			int out = wcwidth(codepoint);
			if (out >= 1) return out;
		}
		/* Invalid character, render as [U+ABCD] or [U+ABCDEF] */
		return (codepoint < 0x10000) ? 8 : 10;
	}
	return 1;
}

/**
 * The following section contains methods for crafting terminal escapes
 * for rendering the display. We do not use curses or any similar
 * library, so we have to generate these sequences ourself based on
 * some assumptions about the target terminal.
 */

/**
 * Move the terminal cursor
 */
void place_cursor(int x, int y) {
	printf("\033[%d;%dH", y, x);
}

/**
 * Given two color codes from a theme, convert them to an escape
 * sequence to set the foreground and background color. This allows
 * us to support basic 16, xterm 256, and true color in themes.
 */
char * color_string(const char * fg, const char * bg) {
	static char output[100];
	char * t = output;
	t += sprintf(t,"\033[22;23;");
	if (*bg == '@') {
		int _bg = atoi(bg+1);
		if (_bg < 10) {
			t += sprintf(t, "4%d;", _bg);
		} else {
			t += sprintf(t, "10%d;", _bg-10);
		}
	} else {
		t += sprintf(t, "48;%s;", bg);
	}
	if (*fg == '@') {
		int _fg = atoi(fg+1);
		if (_fg < 10) {
			t += sprintf(t, "3%dm", _fg);
		} else {
			t += sprintf(t, "9%dm", _fg-10);
		}
	} else {
		t += sprintf(t, "38;%sm", fg);
	}
	return output;
}

/**
 * Set text colors
 *
 * Normally, text colors are just strings, but if they
 * start with @ they will be parsed as integers
 * representing one of the 16 standard colors, suitable
 * for terminals without support for the 256- or 24-bit
 * color modes.
 */
void set_colors(const char * fg, const char * bg) {
	printf("%s", color_string(fg, bg));
}

/**
 * Set just the foreground color
 *
 * (See set_colors above)
 */
void set_fg_color(const char * fg) {
	printf("\033[22;23;");
	if (*fg == '@') {
		int _fg = atoi(fg+1);
		if (_fg < 10) {
			printf("3%dm", _fg);
		} else {
			printf("9%dm", _fg-10);
		}
	} else {
		printf("38;%sm", fg);
	}
}

/**
 * Clear the rest of this line
 */
void clear_to_end(void) {
	if (global_config.can_bce) {
		printf("\033[K");
	}
}

/**
 * For terminals without bce, prepaint the whole line, so we don't have to track
 * where the cursor is for everything. Inefficient, but effective.
 */
void paint_line(const char * bg) {
	if (!global_config.can_bce) {
		set_colors(COLOR_FG, bg);
		for (int i = 0; i < global_config.term_width; ++i) {
			printf(" ");
		}
		printf("\r");
	}
}

/**
 * Enable bold text display
 */
void set_bold(void) {
	printf("\033[1m");
}

/**
 * Disable bold
 */
void unset_bold(void) {
	printf("\033[22m");
}

/**
 * Enable underlined text display
 */
void set_underline(void) {
	printf("\033[4m");
}

/**
 * Disable underlined text display
 */
void unset_underline(void) {
	printf("\033[24m");
}

/**
 * Reset text display attributes
 */
void reset(void) {
	printf("\033[0m");
}

/**
 * Clear the entire screen
 */
void clear_screen(void) {
	printf("\033[H\033[2J");
}

/**
 * Hide the cursor
 */
void hide_cursor(void) {
	if (global_config.can_hideshow) {
		printf("\033[?25l");
	}
}

/**
 * Show the cursor
 */
void show_cursor(void) {
	if (global_config.can_hideshow) {
		printf("\033[?25h");
	}
}

/**
 * Store the cursor position
 */
void store_cursor(void) {
	printf("\0337");
}

/**
 * Restore the cursor position.
 */
void restore_cursor(void) {
	printf("\0338");
}

/**
 * Request mouse events
 */
void mouse_enable(void) {
	if (global_config.can_mouse) {
		printf("\033[?1000h");
		if (global_config.can_sgrmouse) {
			printf("\033[?1006h");
		}
	}
}

/**
 * Stop mouse events
 */
void mouse_disable(void) {
	if (global_config.can_mouse) {
		if (global_config.can_sgrmouse) {
			printf("\033[?1006l");
		}
		printf("\033[?1000l");
	}
}

/**
 * Shift the screen up one line
 */
void shift_up(int amount) {
	printf("\033[%dS", amount);
}

/**
 * Shift the screen down one line.
 */
void shift_down(int amount) {
	printf("\033[%dT", amount);
}

void insert_lines_at(int line, int count) {
	place_cursor(1, line);
	printf("\033[%dL", count);
}

void delete_lines_at(int line, int count) {
	place_cursor(1, line);
	printf("\033[%dM", count);
}

/**
 * Switch to the alternate terminal screen.
 */
void set_alternate_screen(void) {
	if (global_config.can_altscreen) {
		printf("\033[?1049h");
	}
}

/**
 * Restore the standard terminal screen.
 */
void unset_alternate_screen(void) {
	if (global_config.can_altscreen) {
		printf("\033[?1049l");
	}
}

/**
 * Enable bracketed paste mode.
 */
void set_bracketed_paste(void) {
	if (global_config.can_bracketedpaste) {
		printf("\033[?2004h");
	}
}

/**
 * Disable bracketed paste mode.
 */
void unset_bracketed_paste(void) {
	if (global_config.can_bracketedpaste) {
		printf("\033[?2004l");
	}
}

/**
 * Get the name of just a file from a full path.
 * Returns a pointer within the original string.
 *
 * Called in a few different places where the name of a file
 * is needed from its full path, such as drawing tab names or
 * building HTML files.
 */
char * file_basename(char * file) {
	char * c = strrchr(file, '/');
	if (!c) return file;
	return (c+1);
}

/**
 * Print a tab name with fixed width and modifiers
 * into an output buffer and return the written width.
 */
int draw_tab_name(buffer_t * _env, char * out, int max_width, int * width) {
	uint32_t c, state = 0;
	char * t = _env->file_name ? file_basename(_env->file_name) : "[No Name]";

#define ADD(c) do { \
	*o = c; \
	o++; \
	*o = '\0'; \
	bytes++; \
} while (0)

	char * o = out;
	*o = '\0';

	int bytes = 0;

	if (max_width < 2) return 1;

	ADD(' ');
	(*width)++;

	if (_env->modified) {
		if (max_width < 4) return 1;
		ADD('+');
		(*width)++;
		ADD(' ');
		(*width)++;
	}

	while (*t) {
		/* File names can definitely by UTF-8, and we need to
		 * understand their display width... */
		if (!decode(&state, &c, (unsigned char)*t)) {

			/* But our displayed tab name is also just stored
			 * as UTF-8 again, so we essentially rebuild it... */
			char tmp[7];
			int size = to_eight(c, tmp);
			if (bytes + size > 62) break;
			if (*width + size >= max_width) return 1;

			for (int i = 0; i < size; ++i) {
				ADD(tmp[i]);
			}

			(*width) += codepoint_width(c);

		} else if (state == UTF8_REJECT) {
			state = 0;
		}
		t++;
	}

	if (max_width == *width + 1) return 1;

	ADD(' ');
	(*width)++;

#undef ADD
	return 0;
}

/**
 * Redraw the tabbar, with a tab for each buffer.
 *
 * The active buffer is highlighted.
 */
void redraw_tabbar(void) {
	if (!global_config.tabs_visible) return;
	/* Hide cursor while rendering UI */
	hide_cursor();

	/* Move to upper left */
	place_cursor(1,1);

	paint_line(COLOR_TABBAR_BG);

	/* For each buffer... */
	int offset = 0;

	if (global_config.tab_offset) {
		set_colors(COLOR_NUMBER_FG, COLOR_NUMBER_BG);
		printf("<");
		offset++;
	}

	for (int i = global_config.tab_offset; i < buffers_len; i++) {
		buffer_t * _env = buffers[i];

		if (_env == env) {
			/* If this is the active buffer, highlight it */
			reset();
			set_colors(COLOR_FG, COLOR_BG);
			set_bold();
		} else {
			/* Otherwise use default tab color */
			reset();
			set_colors(COLOR_FG, COLOR_TAB_BG);
			set_underline();
		}

		if (global_config.overlay_mode == OVERLAY_MODE_FILESEARCH) {
			if (global_config.command_buffer->actual) {
				char * f = _env->file_name ? file_basename(_env->file_name) : "";
				/* TODO: Support unicode input here; needs conversion */
				int i = 0;
				for (; i < global_config.command_buffer->actual &&
				      f[i] == global_config.command_buffer->text[i].codepoint; ++i);
				if (global_config.command_buffer->actual == i) {
					set_colors(COLOR_SEARCH_FG, COLOR_SEARCH_BG);
				}
			}
		}


		char title[64];
		int size = 0;
		int filled = draw_tab_name(_env, title, global_config.term_width - offset, &size);

		if (filled) {
			offset += size;
			printf("%s", title);
			set_colors(COLOR_NUMBER_FG, COLOR_NUMBER_BG);
			while (offset != global_config.term_width - 1) {
				printf(" ");
				offset++;
			}
			printf(">");
			break;
		}

		printf("%s", title);

		offset += size;
	}

	/* Reset bold/underline */
	reset();
	/* Fill the rest of the tab bar */
	set_colors(COLOR_FG, COLOR_TABBAR_BG);
	clear_to_end();
}

/**
 * Braindead log10 implementation for figuring out the width of the
 * line number column.
 */
int log_base_10(unsigned int v) {
	int r = (v >= 1000000000) ? 9 : (v >= 100000000) ? 8 : (v >= 10000000) ? 7 :
		(v >= 1000000) ? 6 : (v >= 100000) ? 5 : (v >= 10000) ? 4 :
		(v >= 1000) ? 3 : (v >= 100) ? 2 : (v >= 10) ? 1 : 0;
	return r;
}

/**
 * Render a line of text.
 *
 * This handles rendering the actual text content. A full line of text
 * also includes a line number and some padding, but those elements
 * are rendered elsewhere; this method can be used for lines that are
 * not attached to a buffer such as command input lines.
 *
 * width: width of the text display region (term width - line number width)
 * offset: how many cells into the line to start rendering at
 */
void render_line(line_t * line, int width, int offset, int line_no) {
	int i = 0; /* Offset in char_t line data entries */
	int j = 0; /* Offset in terminal cells */

	const char * last_color = NULL;
	int was_selecting = 0, was_searching = 0, was_underlining = 0;

	/* Set default text colors */
	set_colors(COLOR_FG, line->is_current ? COLOR_ALT_BG : COLOR_BG);

	/*
	 * When we are rendering in the middle of a wide character,
	 * we render -'s to fill the remaining amount of the character's width.
	 */
	int remainder = 0;

	int is_spaces = 1;

	/* For each character in the line ... */
	while (i < line->actual) {

		/* If there is remaining text... */
		if (remainder) {

			/* If we should be drawing by now... */
			if (j >= offset) {
				if (was_underlining) printf("\033[24m");
				/* Fill remainder with -'s */
				set_colors(COLOR_ALT_FG, COLOR_ALT_BG);
				printf("-");
				set_colors(COLOR_FG, line->is_current ? COLOR_ALT_BG : COLOR_BG);
			}

			/* One less remaining width cell to fill */
			remainder--;

			/* Terminal offset moves forward */
			j++;

			/*
			 * If this was the last remaining character, move to
			 * the next codepoint in the line
			 */
			if (remainder == 0) {
				i++;
			}

			continue;
		}

		/* Get the next character to draw */
		char_t c = line->text[i];

		if (c.codepoint != ' ') is_spaces = 0;

		/* If we should be drawing by now... */
		if (j >= offset) {

			/* If this character is going to fall off the edge of the screen... */
			if (j - offset + c.display_width >= width) {
				/* We draw this with special colors so it isn't ambiguous */
				set_colors(COLOR_ALT_FG, COLOR_ALT_BG);
				if (was_underlining) printf("\033[24m");

				/* If it's wide, draw ---> as needed */
				while (j - offset < width - 1) {
					printf("-");
					j++;
				}

				/* End the line with a > to show it overflows */
				printf(">");
				set_colors(COLOR_FG, COLOR_BG);
				return;
			}

			/* Syntax highlighting */
			const char * color = flag_to_color(c.flags);

			if (c.flags & FLAG_SELECT) {
				if ((c.flags & FLAG_MASK_COLORS) == FLAG_NONE) color = COLOR_SELECTFG;
				set_colors(color, COLOR_SELECTBG);
				was_selecting = 1;
			} else if ((c.flags & FLAG_SEARCH) || (c.flags == FLAG_NOTICE)) {
				set_colors(COLOR_SEARCH_FG, COLOR_SEARCH_BG);
				was_searching = 1;
			} else if (c.flags == FLAG_ERROR) {
				set_colors(COLOR_ERROR_FG, COLOR_ERROR_BG);
				was_searching = 1; /* co-opting this should work... */
			} else {
				if (was_selecting || was_searching) {
					was_selecting = 0;
					was_searching = 0;
					set_colors(color, line->is_current ? COLOR_ALT_BG : COLOR_BG);
					last_color = color;
				} else if (!last_color || strcmp(color, last_color)) {
					set_fg_color(color);
					last_color = color;
				}
			}

			if ((c.flags & FLAG_UNDERLINE) && !was_underlining) {
				printf("\033[4m");
				was_underlining = 1;
			} else if (!(c.flags & FLAG_UNDERLINE) && was_underlining) {
				printf("\033[24m");
				was_underlining = 0;
			}

			if ((env->mode == MODE_COL_SELECTION || env->mode == MODE_COL_INSERT) &&
				line_no >= ((env->start_line < env->line_no) ? env->start_line : env->line_no) &&
				line_no <= ((env->start_line < env->line_no) ? env->line_no : env->start_line) &&
				((j == env->sel_col) ||
				(j < env->sel_col && j + c.display_width > env->sel_col))) {
				set_colors(COLOR_SELECTFG, COLOR_SELECTBG);
				was_selecting = 1;
			}

#define _set_colors(fg,bg) \
	if (!(c.flags & FLAG_SELECT) && !(c.flags & FLAG_SEARCH) && !(was_selecting)) { \
		set_colors(fg,(line->is_current && bg == COLOR_BG) ? COLOR_ALT_BG : bg); \
	}

			/* Render special characters */
			if (c.codepoint == '\t') {
				_set_colors(COLOR_ALT_FG, COLOR_ALT_BG);
				printf("%s", global_config.tab_indicator);
				for (int i = 1; i < c.display_width; ++i) {
					printf("%s" ,global_config.space_indicator);
				}
				_set_colors(last_color ? last_color : COLOR_FG, COLOR_BG);
			} else if (c.codepoint < 32) {
				/* Codepoints under 32 to get converted to ^@ escapes */
				_set_colors(COLOR_ALT_FG, COLOR_ALT_BG);
				printf("^%c", '@' + c.codepoint);
				_set_colors(last_color ? last_color : COLOR_FG, COLOR_BG);
			} else if (c.codepoint == 0x7f) {
				_set_colors(COLOR_ALT_FG, COLOR_ALT_BG);
				printf("^?");
				_set_colors(last_color ? last_color : COLOR_FG, COLOR_BG);
			} else if (c.codepoint > 0x7f && c.codepoint < 0xa0) {
				_set_colors(COLOR_ALT_FG, COLOR_ALT_BG);
				printf("<%2x>", c.codepoint);
				_set_colors(last_color ? last_color : COLOR_FG, COLOR_BG);
			} else if (c.codepoint == 0xa0) {
				_set_colors(COLOR_ALT_FG, COLOR_ALT_BG);
				printf("_");
				_set_colors(last_color ? last_color : COLOR_FG, COLOR_BG);
			} else if (c.display_width == 8) {
				_set_colors(COLOR_ALT_FG, COLOR_ALT_BG);
				printf("[U+%04x]", c.codepoint);
				_set_colors(last_color ? last_color : COLOR_FG, COLOR_BG);
			} else if (c.display_width == 10) {
				_set_colors(COLOR_ALT_FG, COLOR_ALT_BG);
				printf("[U+%06x]", c.codepoint);
				_set_colors(last_color ? last_color : COLOR_FG, COLOR_BG);
			} else if (i > 0 && is_spaces && c.codepoint == ' ' && !(i % env->tabstop)) {
				_set_colors(COLOR_ALT_FG, COLOR_BG); /* Normal background so this is more subtle */
				if (global_config.can_unicode) {
					printf("▏");
				} else {
					printf("|");
				}
				_set_colors(last_color ? last_color : COLOR_FG, COLOR_BG);
			} else if (c.codepoint == ' ' && i == line->actual - 1) {
				/* Special case: space at end of line */
				_set_colors(COLOR_ALT_FG, COLOR_ALT_BG);
				printf("%s",global_config.space_indicator);
				_set_colors(COLOR_FG, COLOR_BG);
			} else {
				/* Normal characters get output */
				char tmp[7]; /* Max six bytes, use 7 to ensure last is always nil */
				to_eight(c.codepoint, tmp);
				printf("%s", tmp);
			}

			/* Advance the terminal cell offset by the render width of this character */
			j += c.display_width;

			/* Advance to the next character */
			i++;
		} else if (c.display_width > 1) {
			/*
			 * If this is a wide character but we aren't ready to render yet,
			 * we may need to draw some filler text for the remainder of its
			 * width to ensure we don't jump around when horizontally scrolling
			 * past wide characters.
			 */
			remainder = c.display_width - 1;
			j++;
		} else {
			/* Regular character, not ready to draw, advance without doing anything */
			j++;
			i++;
		}
	}

	if (was_underlining) printf("\033[24m");

	/**
	 * Determine what color the rest of the line should be.
	 */
	if (env->mode != MODE_LINE_SELECTION) {
		/* If we are not selecting, then use the normal background or highlight
		 * the current line if that feature is enabled. */
		if (line->is_current) {
			set_colors(COLOR_FG, COLOR_ALT_BG);
		} else {
			set_colors(COLOR_FG, COLOR_BG);
		}
	} else {
		/* If this line was empty but was part of the selection, we didn't
		 * set the selection color already, so we need to do that here. */
		if (!line->actual) {
			if (env->line_no == line_no ||
				(env->start_line > env->line_no && 
					(line_no >= env->line_no && line_no <= env->start_line)) ||
				(env->start_line < env->line_no &&
					(line_no >= env->start_line && line_no <= env->line_no))) {
				set_colors(COLOR_SELECTFG, COLOR_SELECTBG);
			}
		}
	}

	/**
	 * In column modes, we may need to draw a column select beyond the end
	 * of a given line, so we need to draw up to that point first.
	 */
	if ((env->mode == MODE_COL_SELECTION  || env->mode == MODE_COL_INSERT) &&
		line_no >= ((env->start_line < env->line_no) ? env->start_line : env->line_no) &&
		line_no <= ((env->start_line < env->line_no) ? env->line_no : env->start_line) &&
		j <= env->sel_col &&
		env->sel_col < width) {
		set_colors(COLOR_FG, COLOR_BG);
		while (j < env->sel_col) {
			printf(" ");
			j++;
		}
		set_colors(COLOR_SELECTFG, COLOR_SELECTBG);
		printf(" ");
		j++;
		set_colors(COLOR_FG, COLOR_BG);
	}

	/*
	 * `maxcolumn` renders the background outside of the requested line length
	 * in a different color, with a line at the border between the two.
	 */
	if (env->maxcolumn && line_no > -1 /* ensures we don't do this for command line */) {

		/* Fill out the normal background */
		if (j < offset) j = offset;
		for (; j < width + offset && j < env->maxcolumn; ++j) {
			printf(" ");
		}

		/* Draw the line */
		if (j < width + offset && j == env->maxcolumn) {
			j++;
			set_colors(COLOR_ALT_FG, COLOR_ALT_BG);
			if (global_config.can_unicode) {
				printf("▏"); /* Should this be configurable? */
			} else {
				printf("|");
			}
		}

		/* Fill the rest with the alternate background color */
		set_colors(COLOR_ALT_FG, COLOR_ALT_BG);
	}

	/**
	 * Clear out the rest of the line. If we are the only buffer or the right split,
	 * and our terminal supports `bce`, we can just bce; otherwise write spaces
	 * until we reach the right side of the screen.
	 */
	if (global_config.can_bce && (line_no == -1 || env->left + env->width == global_config.term_width)) {
		clear_to_end();
	} else {
		/* Paint the rest of the line */
		if (j < offset) j = offset;
		for (; j < width + offset; ++j) {
			printf(" ");
		}
	}
}

/**
 * Get the width of the line number region
 */
int num_width(void) {
	if (!env->numbers) return 0;
	int w = log_base_10(env->line_count) + 3;
	if (w < 4) return 4;
	return w;
}

/**
 * Display width of the revision status gutter.
 */
int gutter_width(void) {
	return env->gutter;
}

/**
 * Draw the gutter and line numbers.
 */
void draw_line_number(int x) {
	if (!env->numbers) return;
	/* Draw the line number */
	if (env->lines[x]->is_current) {
		set_colors(COLOR_NUMBER_BG, COLOR_NUMBER_FG);
	} else {
		set_colors(COLOR_NUMBER_FG, COLOR_NUMBER_BG);
	}
	if (global_config.relative_lines && x+1 != env->line_no) {
		x = x+1 - env->line_no;
		x = ((x < 0) ? -x : x)-1;
	}
	int num_size = num_width() - 2; /* Padding */
	for (int y = 0; y < num_size - log_base_10(x + 1); ++y) {
		printf(" ");
	}
	printf("%d%c", x + 1, ((x+1 == env->line_no || global_config.horizontal_shift_scrolling) && env->coffset > 0) ? '<' : ' ');
}

/**
 * Used to highlight the current line after moving the cursor.
 */
void recalculate_current_line(void) {
	int something_changed = 0;
	if (global_config.highlight_current_line) {
		for (int i = 0; i < env->line_count; ++i) {
			if (env->lines[i]->is_current && i != env->line_no-1) {
				env->lines[i]->is_current = 0;
				something_changed = 1;
				redraw_line(i);
			} else if (i == env->line_no-1 && !env->lines[i]->is_current) {
				env->lines[i]->is_current = 1;
				something_changed = 1;
				redraw_line(i);
			}
		}
	} else {
		something_changed = 1;
	}
	if (something_changed && global_config.relative_lines) {
		for (int i = env->offset; i < env->offset + global_config.term_height - global_config.bottom_size - 1 && i < env->line_count; ++i) {
			/* Place cursor for line number */
			place_cursor(1 + gutter_width() + env->left, (i)-env->offset + 1 + global_config.tabs_visible);
			draw_line_number(i);
		}
	}
}

/**
 * Redraw line.
 *
 * This draws the line number as well as the actual text.
 */
void redraw_line(int x) {
	if (env->loading) return;

	/* Determine if this line is visible. */
	if (x - env->offset < 0 || x - env->offset > global_config.term_height - global_config.bottom_size - 1 - global_config.tabs_visible) {
		return;
	}

	/* Calculate offset in screen */
	int j = x - env->offset;

	/* Hide cursor when drawing */
	hide_cursor();

	/* Move cursor to upper left most cell of this line */
	place_cursor(1 + env->left,1 + global_config.tabs_visible + j);

	/* Draw a gutter on the left. */
	if (env->gutter) {
		switch (env->lines[x]->rev_status) {
			case 1:
				set_colors(COLOR_NUMBER_FG, COLOR_GREEN);
				printf(" ");
				break;
			case 2:
				set_colors(COLOR_NUMBER_FG, global_config.color_gutter ? COLOR_SEARCH_BG : COLOR_ALT_FG);
				printf(" ");
				break;
			case 3:
				set_colors(COLOR_NUMBER_FG, COLOR_KEYWORD);
				printf(" ");
				break;
			case 4:
				set_colors(COLOR_ALT_FG, COLOR_RED);
				printf("▆");
				break;
			case 5:
				set_colors(COLOR_KEYWORD, COLOR_RED);
				printf("▆");
				break;
			default:
				set_colors(COLOR_NUMBER_FG, COLOR_ALT_FG);
				printf(" ");
				break;
		}
	}

	draw_line_number(x);

	int should_shift = x + 1 == env->line_no || global_config.horizontal_shift_scrolling || 
			((env->mode == MODE_COL_SELECTION || env->mode == MODE_COL_INSERT) &&
			x + 1 >= ((env->start_line < env->line_no) ? env->start_line : env->line_no) &&
			x + 1 <= ((env->start_line < env->line_no) ? env->line_no : env->start_line));
	

	/*
	 * Draw the line text 
	 * If this is the active line, the current character cell offset should be used.
	 * (Non-active lines are not shifted and always render from the start of the line)
	 */
	render_line(env->lines[x], env->width - gutter_width() - num_width(), should_shift ? env->coffset : 0, x+1);

}

/**
 * Draw a ~ line where there is no buffer text.
 */
void draw_excess_line(int j) {
	place_cursor(1+env->left,1 + global_config.tabs_visible + j);
	paint_line(COLOR_ALT_BG);
	set_colors(COLOR_ALT_FG, COLOR_ALT_BG);
	printf("~");
	if (env->left + env->width == global_config.term_width && global_config.can_bce) {
		clear_to_end();
	} else {
		/* Paint the rest of the line */
		for (int x = 1; x < env->width; ++x) {
			printf(" ");
		}
	}
}

/**
 * Redraw the entire text area
 */
void redraw_text(void) {
	if (!env) return;
	if (!global_config.has_terminal) return;

	/* Hide cursor while rendering */
	hide_cursor();

	/* Figure out the available size of the text region */
	int l = global_config.term_height - global_config.bottom_size - global_config.tabs_visible;
	int j = 0;

	/* Draw each line */
	for (int x = env->offset; j < l && x < env->line_count; x++) {
		redraw_line(x);
		j++;
	}

	/* Draw the rest of the text region as ~ lines */
	for (; j < l; ++j) {
		draw_excess_line(j);
	}
}

static int view_left_offset = 0;
static int view_right_offset = 0;

/**
 * When in split view, draw the other buffer.
 * Has special handling for when the split is
 * on a single buffer.
 */
void redraw_alt_buffer(buffer_t * buf) {
	if (left_buffer == right_buffer) {
		/* draw the opposite view */
		int left, width, offset;
		left = env->left;
		width = env->width;
		offset = env->offset;
		if (left == 0) {
			/* Draw the right side */

			env->left = width;
			env->width = global_config.term_width - width;
			env->offset = view_right_offset;
			view_left_offset = offset;
		} else {
			env->left = 0;
			env->width = global_config.term_width * global_config.split_percent / 100;
			env->offset = view_left_offset;
			view_right_offset = offset;
		}
		redraw_text();

		env->left = left;
		env->width = width;
		env->offset = offset;
	}
	/* Swap out active buffer */
	buffer_t * tmp = env;
	env = buf;
	/* Redraw text */
	redraw_text();
	/* Return original active buffer */
	env = tmp;
}

/**
 * Basically wcswidth() but implemented internally using our
 * own utf-8 decoder to ensure it works properly.
 */
int display_width_of_string(const char * str) {
	uint8_t * s = (uint8_t *)str;

	int out = 0;
	uint32_t c, state = 0;
	while (*s) {
		if (!decode(&state, &c, *s)) {
			out += codepoint_width(c);
		} else if (state == UTF8_REJECT) {
			state = 0;
		}
		s++;
	}

	return out;
}

void statusbar_append_status(int *remaining_width, size_t *filled, char * output, char * base, ...) {
	va_list args;
	va_start(args, base);
	char tmp[100] = {0}; /* should be big enough */
	vsnprintf(tmp, 100, base, args);
	va_end(args);

	int width = display_width_of_string(tmp) + 2;

	size_t totalWidth = strlen(tmp);
	totalWidth += strlen(color_string(COLOR_STATUS_ALT, COLOR_STATUS_BG));
	totalWidth += strlen(color_string(COLOR_STATUS_FG, COLOR_STATUS_BG));
	totalWidth += strlen(color_string(COLOR_STATUS_ALT, COLOR_STATUS_BG));
	totalWidth += 3;

	if (totalWidth + *filled >= 2047) {
		return;
	}

	if (width < *remaining_width) {
		strcat(output,color_string(COLOR_STATUS_ALT, COLOR_STATUS_BG));
		strcat(output,"[");
		strcat(output,color_string(COLOR_STATUS_FG, COLOR_STATUS_BG));
		strcat(output, tmp);
		strcat(output,color_string(COLOR_STATUS_ALT, COLOR_STATUS_BG));
		strcat(output,"]");
		(*remaining_width) -= width;
		(*filled) += totalWidth;
	}
}

int statusbar_build_right(char * right_hand) {
	char tmp[1024] = {0};
	sprintf(tmp, " Line %d/%d Col: %d ", env->line_no, env->line_count, env->col_no);
	int out = display_width_of_string(tmp);
	char * s = right_hand;

	s += sprintf(s, "%s", color_string(COLOR_STATUS_ALT, COLOR_STATUS_BG));
	s += sprintf(s, " Line ");
	s += sprintf(s, "%s", color_string(COLOR_STATUS_FG, COLOR_STATUS_BG));
	s += sprintf(s, "%d/%d ", env->line_no, env->line_count);
	s += sprintf(s, "%s", color_string(COLOR_STATUS_ALT, COLOR_STATUS_BG));
	s += sprintf(s, " Col: ");
	s += sprintf(s, "%s", color_string(COLOR_STATUS_FG, COLOR_STATUS_BG));
	s += sprintf(s, "%d ", env->col_no);

	return out;
}

/**
 * Draw the status bar
 *
 * The status bar shows the name of the file, whether it has modifications,
 * and (in the future) what syntax highlighting mode is enabled.
 *
 * The right side of the status bar shows the line number and column.
 */
void redraw_statusbar(void) {
	if (global_config.hide_statusbar) return;
	if (!global_config.has_terminal) return;
	if (!env) return;
	/* Hide cursor while rendering */
	hide_cursor();

	/* Move cursor to the status bar line (second from bottom */
	place_cursor(1, global_config.term_height - 1);

	/* Set background colors for status line */
	paint_line(COLOR_STATUS_BG);
	set_colors(COLOR_STATUS_FG, COLOR_STATUS_BG);


	/* Pre-render the right hand side of the status bar */
	char right_hand[1024] = {0};
	int right_width = statusbar_build_right(right_hand);

	char status_bits[2048] = {0}; /* Sane maximum */
	size_t filled = 0;

	int remaining_width = global_config.term_width - right_width;

#define ADD(...) do { statusbar_append_status(&remaining_width, &filled, status_bits, __VA_ARGS__); } while (0)
	if (env->syntax) {
		ADD("%s",env->syntax->name);
	}

	/* Print file status indicators */
	if (env->modified) {
		ADD("+");
	}

	if (env->readonly) {
		ADD("ro");
	}

	if (env->crnl) {
		ADD("crnl");
	}

	if (env->tabs) {
		ADD("tabs");
	} else {
		ADD("spaces=%d", env->tabstop);
	}

	if (global_config.yanks) {
		ADD("y:%ld", global_config.yank_count);
	}

	if (env->indent) {
		ADD("indent");
	}

	if (global_config.smart_complete) {
		ADD("complete");
	}

	if (global_config.background_task) {
		ADD("working");
	}

#undef ADD

	uint8_t * file_name = (uint8_t *)(env->file_name ? env->file_name : "[No Name]");
	int file_name_width = display_width_of_string((char*)file_name);

	if (remaining_width > 3) {
		int is_chopped = 0;
		while (remaining_width < file_name_width + 3) {
			is_chopped = 1;
			if ((*file_name & 0xc0) == 0xc0) { /* First byte of a multibyte character */
				file_name++;
				while ((*file_name & 0xc0) == 0x80) file_name++;
			} else {
				file_name++;
			}
			file_name_width = display_width_of_string((char*)file_name);
		}
		if (is_chopped) {
			set_colors(COLOR_ALT_FG, COLOR_STATUS_BG);
			printf("<");
		}
		set_colors(COLOR_STATUS_FG, COLOR_STATUS_BG);
		printf("%s ", file_name);
	}

	printf("%s", status_bits);

	/* Clear the rest of the status bar */
	clear_to_end();

	/* Move the cursor appropriately to draw it */
	place_cursor(global_config.term_width - right_width, global_config.term_height - 1);
	set_colors(COLOR_STATUS_FG, COLOR_STATUS_BG);
	printf("%s",right_hand);
}

/**
 * Redraw the navigation numbers on the right side of the command line
 */
void redraw_nav_buffer(void) {
	if (!global_config.has_terminal) return;
	if (nav_buffer) {
		store_cursor();
		place_cursor(global_config.term_width - nav_buffer - 2, global_config.term_height);
		printf("%s", nav_buf);
		clear_to_end();
		restore_cursor();
	}
}

/**
 * Draw the command line
 *
 * The command line either has input from the user (:quit, :!make, etc.)
 * or shows the INSERT (or VISUAL in the future) mode name.
 */
void redraw_commandline(void) {
	if (!global_config.has_terminal) return;
	if (!env) return;

	/* Hide cursor while rendering */
	hide_cursor();

	/* Move cursor to the last line */
	place_cursor(1, global_config.term_height);

	/* Set background color */
	paint_line(COLOR_BG);
	set_colors(COLOR_FG, COLOR_BG);

	/* If we are in an edit mode, note that. */
	if (env->mode == MODE_INSERT) {
		set_bold();
		printf("-- INSERT --");
		clear_to_end();
		unset_bold();
	} else if (env->mode == MODE_LINE_SELECTION) {
		set_bold();
		printf("-- LINE SELECTION -- (%d:%d)",
			(env->start_line < env->line_no) ? env->start_line : env->line_no,
			(env->start_line < env->line_no) ? env->line_no : env->start_line
		);
		clear_to_end();
		unset_bold();
	} else if (env->mode == MODE_COL_SELECTION) {
		set_bold();
		printf("-- COL SELECTION -- (%d:%d %d)",
			(env->start_line < env->line_no) ? env->start_line : env->line_no,
			(env->start_line < env->line_no) ? env->line_no : env->start_line,
			(env->sel_col)
		);
		clear_to_end();
		unset_bold();
	} else if (env->mode == MODE_COL_INSERT) {
		set_bold();
		printf("-- COL INSERT -- (%d:%d %d)",
			(env->start_line < env->line_no) ? env->start_line : env->line_no,
			(env->start_line < env->line_no) ? env->line_no : env->start_line,
			(env->sel_col)
		);
		clear_to_end();
		unset_bold();
	} else if (env->mode == MODE_REPLACE) {
		set_bold();
		printf("-- REPLACE --");
		clear_to_end();
		unset_bold();
	} else if (env->mode == MODE_CHAR_SELECTION) {
		set_bold();
		printf("-- CHAR SELECTION -- ");
		clear_to_end();
		unset_bold();
	} else if (env->mode == MODE_DIRECTORY_BROWSE) {
		set_bold();
		printf("-- DIRECTORY BROWSE --");
		clear_to_end();
		unset_bold();
	} else {
		clear_to_end();
	}

	redraw_nav_buffer();
}

/**
 * Draw a message on the command line.
 */
void render_commandline_message(char * message, ...) {
	/* varargs setup */
	va_list args;
	va_start(args, message);

	/* Hide cursor while rendering */
	hide_cursor();

	/* Move cursor to the last line */
	place_cursor(1, global_config.term_height);

	/* Set background color */
	paint_line(COLOR_BG);
	set_colors(COLOR_FG, COLOR_BG);

	vprintf(message, args);
	va_end(args);

	/* Clear the rest of the status bar */
	clear_to_end();

	redraw_nav_buffer();
}

BIM_ACTION(redraw_all, 0,
	"Repaint the screen."
,void) {
	if (!env) return;
	redraw_tabbar();
	redraw_text();
	if (left_buffer) {
		redraw_alt_buffer(left_buffer == env ? right_buffer : left_buffer);
	}
	redraw_statusbar();
	redraw_commandline();
	if (global_config.overlay_mode == OVERLAY_MODE_COMMAND ||
	    global_config.overlay_mode == OVERLAY_MODE_SEARCH ||
	    global_config.overlay_mode == OVERLAY_MODE_FILESEARCH) {
		render_command_input_buffer();
	}
}

void pause_for_key(void) {
	int c;
	while ((c = bim_getch())== -1);
	bim_unget(c);
	redraw_all();
}

/**
 * Redraw all screen elements except the other split view.
 */
void redraw_most(void) {
	redraw_tabbar();
	redraw_text();
	redraw_statusbar();
	redraw_commandline();
}

/**
 * Disable screen splitting.
 */
void unsplit(void) {
	if (left_buffer) {
		left_buffer->left = 0;
		left_buffer->width = global_config.term_width;
	}
	if (right_buffer) {
		right_buffer->left = 0;
		right_buffer->width = global_config.term_width;
	}
	left_buffer = NULL;
	right_buffer = NULL;

	redraw_all();
}

/**
 * Update the terminal title bar
 */
void update_title(void) {
	if (!global_config.can_title) return;

	char cwd[1024] = {'/',0};
	getcwd(cwd, 1024);

	for (int i = 1; i < 3; ++i) {
		printf("\033]%d;%s%s (%s) - Bim\007", i, env->file_name ? env->file_name : "[No Name]", env->modified ? " +" : "", cwd);
	}
}

/**
 * Mark this buffer as modified and
 * redraw the status and tabbar if needed.
 */
void set_modified(void) {
	/* If it was already marked modified, no need to do anything */
	if (env->modified) return;

	/* Mark as modified */
	env->modified = 1;

	/* Redraw some things */
	update_title();
	redraw_tabbar();
	redraw_statusbar();
}

/**
 * Draw a message on the status line
 */
void render_status_message(char * message, ...) {
	if (!env) return; /* Don't print when there's no active environment; this usually means a bimrc command tried to print something */
	/* varargs setup */
	va_list args;
	va_start(args, message);

	/* Hide cursor while rendering */
	hide_cursor();

	/* Move cursor to the status bar line (second from bottom */
	place_cursor(1, global_config.term_height - 1);

	/* Set background colors for status line */
	paint_line(COLOR_STATUS_BG);
	set_colors(COLOR_STATUS_FG, COLOR_STATUS_BG);

	/* Process format string */
	vprintf(message, args);
	va_end(args);

	/* Clear the rest of the status bar */
	clear_to_end();
}

/**
 * Draw an error message to the command line.
 */
void render_error(char * message, ...) {
	/* varargs setup */
	va_list args;
	va_start(args, message);

	if (env) {
		/* Hide cursor while rendering */
		hide_cursor();

		/* Move cursor to the command line */
		place_cursor(1, global_config.term_height);

		/* Set appropriate error message colors */
		set_colors(COLOR_ERROR_FG, COLOR_ERROR_BG);

		/* Draw the message */
		vprintf(message, args);
		va_end(args);
		global_config.had_error = 1;
	} else {
		printf("bim: error during startup: ");
		vprintf(message, args);
		va_end(args);
		printf("\n");
	}

}

char * paren_pairs = "()[]{}<>";

int is_paren(int c) {
	char * p = paren_pairs;
	while (*p) {
		if (c == *p) return 1;
		p++;
	}
	return 0;
}

#define _rehighlight_parens() do { \
	if (i < 0 || i >= env->line_count) break; \
	for (int j = 0; j < env->lines[i]->actual; ++j) { \
		if (i == line-1 && j == col-1) { \
			env->lines[line-1]->text[col-1].flags |= FLAG_SELECT; \
			continue; \
		} else { \
			env->lines[i]->text[j].flags &= (~FLAG_SELECT); \
		} \
	} \
	redraw_line(i); \
} while (0)

/**
 * If the config option is enabled, find the matching
 * paren character and highlight it with the SELECT
 * colors, clearing out other SELECT values. As we
 * co-opt the SELECT flag, don't do this in selection
 * modes - only in normal and insert modes.
 */
void highlight_matching_paren(void) {
	if (env->mode == MODE_LINE_SELECTION || env->mode == MODE_CHAR_SELECTION) return;
	if (!global_config.highlight_parens) return;
	int line = -1, col = -1;
	if (env->line_no <= env->line_count && env->col_no <= env->lines[env->line_no-1]->actual &&
		is_paren(env->lines[env->line_no-1]->text[env->col_no-1].codepoint)) {
		find_matching_paren(&line, &col, 1);
	} else if (env->line_no <= env->line_count && env->col_no > 1 && is_paren(env->lines[env->line_no-1]->text[env->col_no-2].codepoint)) {
		find_matching_paren(&line, &col, 2);
	}
	if (env->highlighting_paren == -1 && line == -1) return;
	if (env->highlighting_paren > 0) {
		int i = env->highlighting_paren - 1;
		_rehighlight_parens();
	}
	if (env->highlighting_paren != line && line != -1) {
		int i = line - 1;
		_rehighlight_parens();
	}
	env->highlighting_paren = line;
}

/**
 * Recalculate syntax for the matched paren.
 * Useful when entering selection modes.
 */
void unhighlight_matching_paren(void) {
	if (env->highlighting_paren > 0 && env->highlighting_paren <= env->line_count) {
		for (int i = 0; i < env->line_count; i++) {
			for (int j = 0; j < env->lines[i]->actual; ++j) {
				env->lines[i]->text[j].flags &= ~(FLAG_SELECT);
			}
		}
		env->highlighting_paren = -1;
	}
}

/**
 * Move the cursor to the appropriate location based
 * on where it is in the text region.
 *
 * This does some additional math to set the text
 * region horizontal offset.
 */
void place_cursor_actual(void) {

	/* Invalid positions */
	if (env->line_no < 1) env->line_no = 1;
	if (env->col_no  < 1) env->col_no  = 1;

	/* Account for the left hand gutter */
	int num_size = num_width() + gutter_width();
	int x = num_size + 1 - env->coffset;

	/* Determine where the cursor is physically */
	for (int i = 0; i < env->col_no - 1; ++i) {
		char_t * c = &env->lines[env->line_no-1]->text[i];
		x += c->display_width;
	}

	/* y is a bit easier to calculate */
	int y = env->line_no - env->offset + 1;

	int needs_redraw = 0;

	while (y < 2 + global_config.cursor_padding && env->offset > 0) {
		y++;
		env->offset--;
		needs_redraw = 1;
	}

	while (y > 1 + global_config.term_height - global_config.bottom_size - global_config.cursor_padding - global_config.tabs_visible) {
		y--;
		env->offset++;
		needs_redraw = 1;
	}

	if (needs_redraw) {
		redraw_text();
		redraw_tabbar();
		redraw_statusbar();
		redraw_commandline();
	}

	/* If the cursor has gone off screen to the right... */
	if (x > env->width - 1) {
		/* Adjust the offset appropriately to scroll horizontally */
		int diff = x - (env->width - 1);
		env->coffset += diff;
		x -= diff;
		redraw_text();
	}

	/* Same for scrolling horizontally to the left */
	if (x < num_size + 1) {
		int diff = (num_size + 1) - x;
		env->coffset -= diff;
		x += diff;
		redraw_text();
	}

	highlight_matching_paren();
	recalculate_current_line();

	/* Move the actual terminal cursor */
	place_cursor(x+env->left,y - !global_config.tabs_visible);

	/* Show the cursor */
	show_cursor();
}

/**
 * If the screen is split, update the split sizes based
 * on the new terminal width and the user's split_percent setting.
 */
void update_split_size(void) {
	if (!left_buffer) return;
	if (left_buffer == right_buffer) {
		if (left_buffer->left == 0) {
			left_buffer->width = global_config.term_width * global_config.split_percent / 100;
		} else {
			right_buffer->left = global_config.term_width * global_config.split_percent / 100;
			right_buffer->width = global_config.term_width - right_buffer->left;
		}
		return;
	}
	left_buffer->left = 0;
	left_buffer->width = global_config.term_width * global_config.split_percent / 100;
	right_buffer->left = left_buffer->width;
	right_buffer->width = global_config.term_width - left_buffer->width;
}

/**
 * Update screen size
 */
void update_screen_size(void) {
	struct winsize w;
	ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
	global_config.term_width = w.ws_col;
	global_config.term_height = w.ws_row;
	if (env) {
		if (left_buffer) {
			update_split_size();
		} else if (env != left_buffer && env != right_buffer) {
			env->width = w.ws_col;
		}
	}
	for (int i = 0; i < buffers_len; ++i) {
		if (buffers[i] != left_buffer && buffers[i] != right_buffer) {
			buffers[i]->width = w.ws_col;
		}
	}
}

/**
 * Handle terminal size changes
 */
void SIGWINCH_handler(int sig) {
	(void)sig;
	update_screen_size();
	redraw_all();

	signal(SIGWINCH, SIGWINCH_handler);
}

/**
 * Handle suspend
 */
void SIGTSTP_handler(int sig) {
	(void)sig;
	mouse_disable();
	set_buffered();
	reset();
	clear_screen();
	show_cursor();
	unset_bracketed_paste();
	unset_alternate_screen();
	fflush(stdout);

	signal(SIGTSTP, SIG_DFL);
	raise(SIGTSTP);
}

void SIGCONT_handler(int sig) {
	(void)sig;
	set_alternate_screen();
	set_bracketed_paste();
	set_unbuffered();
	update_screen_size();
	mouse_enable();
	redraw_all();
	update_title();
	signal(SIGCONT, SIGCONT_handler);
	signal(SIGTSTP, SIGTSTP_handler);
}

void SIGINT_handler(int sig) {
	krk_currentThread.flags |= KRK_THREAD_SIGNALLED;
	signal(SIGINT,   SIGINT_handler);
}

void try_to_center(void) {
	int half_a_screen = (global_config.term_height - 3) / 2;
	if (half_a_screen < env->line_no) {
		env->offset = env->line_no - half_a_screen;
	} else {
		env->offset = 0;
	}
}

BIM_ACTION(suspend, 0,
	"Suspend bim and the rest of the job it was run in."
,void) {
	kill(0, SIGTSTP);
}

/**
 * Move the cursor to a specific line.
 */
BIM_ACTION(goto_line, ARG_IS_CUSTOM,
	"Jump to the requested line."
,int line) {

	if (line == -1) line = env->line_count;

	/* Respect file bounds */
	if (line < 1) line = 1;
	if (line > env->line_count) line = env->line_count;

	/* Move the cursor / text region offsets */
	env->coffset = 0;
	env->line_no = line;
	env->col_no  = 1;

	if (!env->loading) {
		if (line > env->offset && line < env->offset + global_config.term_height - global_config.bottom_size) {
			place_cursor_actual();
		} else {
			try_to_center();
		}
		redraw_most();
	} else {
		try_to_center();
	}
}


/**
 * Process (part of) a file and add it to a buffer.
 */
void add_buffer(uint8_t * buf, int size) {
	for (int i = 0; i < size; ++i) {
		if (!decode(&state, &codepoint_r, buf[i])) {
			uint32_t c = codepoint_r;
			if (c == '\n') {
				if (!env->crnl && env->lines[env->line_no-1]->actual && env->lines[env->line_no-1]->text[env->lines[env->line_no-1]->actual-1].codepoint == '\r') {
					env->lines[env->line_no-1]->actual--;
					env->crnl = 1;
				}
				env->lines = add_line(env->lines, env->line_no);
				env->col_no = 1;
				env->line_no += 1;
			} else if (env->crnl && c == '\r') {
				continue;
			} else {
				char_t _c;
				_c.codepoint = (uint32_t)c;
				_c.flags = 0;
				_c.display_width = codepoint_width((wchar_t)c);
				line_t * line  = env->lines[env->line_no - 1];
				line_t * nline = line_insert(line, _c, env->col_no - 1, env->line_no-1);
				if (line != nline) {
					env->lines[env->line_no - 1] = nline;
				}
				env->col_no += 1;
			}
		} else if (state == UTF8_REJECT) {
			state = 0;
		}
	}
}

/**
 * Add a raw string to a buffer. Convenience wrapper
 * for add_buffer for nil-terminated strings.
 */
void add_string(char * string) {
	add_buffer((uint8_t*)string,strlen(string));
}

int str_ends_with(const char * haystack, const char * needle) {
	int i = strlen(haystack);
	int j = strlen(needle);

	do {
		if (haystack[i] != needle[j]) return 0;
		if (j == 0) return 1;
		if (i == 0) return 0;
		i--;
		j--;
	} while (1);
}

/**
 * Find a syntax highlighter for the given filename.
 */
struct syntax_definition * match_syntax(char * file) {
	for (struct syntax_definition * s = syntaxes; syntaxes && s->name; ++s) {
		for (char ** ext = s->ext; *ext; ++ext) {
			if (str_ends_with(file, *ext)) return s;
		}
	}

	return NULL;
}

/**
 * Set the syntax configuration by the name of the syntax highlighter.
 */
void set_syntax_by_name(const char * name) {
	if (!strcmp(name,"none")) {
		for (int i = 0; i < env->line_count; ++i) {
			env->lines[i]->istate = -1;
			for (int j = 0; j < env->lines[i]->actual; ++j) {
				env->lines[i]->text[j].flags &= (3 << 5);
			}
		}
		env->syntax = NULL;
		redraw_all();
		return;
	}
	for (struct syntax_definition * s = syntaxes; syntaxes && s->name; ++s) {
		if (!strcmp(name,s->name)) {
			env->syntax = s;
			for (int i = 0; i < env->line_count; ++i) {
				env->lines[i]->istate = -1;
			}
			schedule_complete_recalc();
			redraw_all();
			return;
		}
	}
	render_error("unrecognized syntax type");
}


/**
 * Check if a string is all numbers.
 */
int is_all_numbers(const char * c) {
	while (*c) {
		if (!isdigit(*c)) return 0;
		c++;
	}
	return 1;
}

struct file_listing {
	int type;
	char * filename;
};

int sort_files(const void * a, const void * b) {
	struct file_listing * _a = (struct file_listing *)a;
	struct file_listing * _b = (struct file_listing *)b;

	if (_a->type == _b->type) {
		return strcmp(_a->filename, _b->filename);
	} else {
		return _a->type - _b->type;
	}
}

void read_directory_into_buffer(char * file) {
	DIR * dirp = opendir(file);
	if (!dirp) {
		env->loading = 0;
		return;
	}

	add_string("# Directory listing for `");
	add_string(file);
	add_string("`\n");

	/* Flexible array to hold directory contents */
	int available = 32;
	int count = 0;
	struct file_listing * files = calloc(sizeof(struct file_listing), available);

	/* Read directory */
	struct dirent * ent = readdir(dirp);
	while (ent) {
		struct stat statbuf;
		char * tmp = malloc(strlen(file) + 1 + strlen(ent->d_name) + 1);
		snprintf(tmp, strlen(file) + 1 + strlen(ent->d_name) + 1, "%s/%s", file, ent->d_name);
		stat(tmp, &statbuf);
		int type = (S_ISDIR(statbuf.st_mode)) ? 'd' : 'f';
		if (count + 1 == available) {
			available *= 2; \
			files = realloc(files, sizeof(struct file_listing) * available); \
		} \
		files[count].type = type;
		files[count].filename = strdup(ent->d_name);
		count++;
		ent = readdir(dirp);
		free(tmp);
	}
	closedir(dirp);

	/* Sort directory entries */
	qsort(files, count, sizeof(struct file_listing), sort_files);

	for (int i = 0; i < count; ++i) {
		add_string(files[i].type == 'd' ? "d" : "f");
		add_string(" ");
		add_string(files[i].filename);
		add_string("\n");

		free(files[i].filename);
	}

	free(files);

	env->file_name = strdup(file);
	env->syntax = find_syntax_calculator("dirent");
	schedule_complete_recalc();
	env->readonly = 1;
	env->loading = 0;
	env->mode = MODE_DIRECTORY_BROWSE;
	env->line_no = 1;
	redraw_all();
}

BIM_ACTION(open_file_from_line, 0,
	"When browsing a directory, open the file under the cursor."
,void) {
	if (env->lines[env->line_no-1]->actual < 1) return;
	if (env->lines[env->line_no-1]->text[0].codepoint != 'd' &&
	    env->lines[env->line_no-1]->text[0].codepoint != 'f') return;
	/* Collect file name */
	char * tmp = malloc(strlen(env->file_name) + 1 + env->lines[env->line_no-1]->actual * 7); /* Should be enough */
	memset(tmp, 0, strlen(env->file_name) + 1 + env->lines[env->line_no-1]->actual * 7);
	char * t = tmp;
	/* Start by copying the filename */
	t += sprintf(t, "%s/", env->file_name);
	/* Start from character 2 to skip d/f and space */
	for (int i = 2; i < env->lines[env->line_no-1]->actual; ++i) {
		t += to_eight(env->lines[env->line_no-1]->text[i].codepoint, t);
	}
	*t = '\0';
	/* Normalize */
	char tmp_path[PATH_MAX+1];
	if (!realpath(tmp, tmp_path)) {
		free(tmp);
		return;
	}
	free(tmp);
	/* Open file */
	buffer_t * old_buffer = env;
	open_file(tmp_path);
	buffer_close(old_buffer);
	update_title();
	redraw_all();
}

int line_matches(line_t * line, char * string) {
	uint32_t c = 0, state = 0;
	int i = 0;
	while (*string) {
		if (!decode(&state, &c, *string)) {
			if (i >= line->actual) return 0;
			if (line->text[i].codepoint != c) return 0;
			string++;
			i++;
		} else if (state == UTF8_REJECT) {
			state = 0;
		}
	}
	return 1;
}

void run_onload(buffer_t * env) {
	/* TODO */
	KrkValue onLoad;
	if (krk_tableGet_fast(&krk_currentThread.module->fields, S("onload"), &onLoad)) {
		krk_push(onLoad);
		krk_push(krk_dict_of(0,NULL,0));

		if (env->file_name) {
			krk_attachNamedObject(AS_DICT(krk_peek(0)), "filename",
				(KrkObj*)krk_copyString(env->file_name,strlen(env->file_name)));
		}

		if (env->syntax && env->syntax->krkClass) {
			krk_attachNamedObject(AS_DICT(krk_peek(0)), "highlighter",
				(KrkObj*)env->syntax->krkClass);
		}

		krk_callStack(1);
		krk_resetStack();
	}
}

static void render_syntax_async(background_task_t * task) {
	buffer_t * old_env = env;
	env = task->env;
	int line_no = task->_private_i;

	if (env->line_count && line_no < env->line_count) {
		int tmp = env->loading;
		env->loading = 1;
		recalculate_syntax(env->lines[line_no], line_no);
		env->loading = tmp;
		if (env == old_env) {
			redraw_line(line_no);
		}
	}
	env = old_env;
}

static void schedule_complete_recalc(void) {
	if (env->line_count < 1000) {
		for (int i = 0; i < env->line_count; ++i) {
			recalculate_syntax(env->lines[i], i);
		}
		return;
	}

	/* TODO see if there's already a redraw scheduled */
	for (int i = 0; i < env->line_count; ++i) {
		background_task_t * task = malloc(sizeof(background_task_t));
		task->env  = env;
		task->_private_i = i;
		task->func = render_syntax_async;
		task->next = NULL;
		if (global_config.tail_task) {
			global_config.tail_task->next = task;
		}
		global_config.tail_task = task;
		if (!global_config.background_task) {
			global_config.background_task = task;
		}
	}
	redraw_statusbar();
}

/**
 * Create a new buffer from a file.
 */
void open_file(char * file) {
	env = buffer_new();
	env->width = global_config.term_width;
	env->left = 0;
	env->loading = 1;

	setup_buffer(env);

	FILE * f;
	int init_line = -1;

	if (!strcmp(file,"-")) {
		/**
		 * Read file from stdin. stderr provides terminal input.
		 */
		if (isatty(STDIN_FILENO)) {
			if (buffers_len == 1) {
				quit("stdin is a terminal and you tried to open -; not letting you do that");
			}
			close_buffer();
			render_error("stdin is a terminal and you tried to open -; not letting you do that");
			return;
		}
		f = stdin;
		env->modified = 1;
	} else {
		char * l = strrchr(file, ':');
		if (l && is_all_numbers(l+1)) {
			*l = '\0';
			l++;
			init_line = atoi(l);
		}

		char * _file = file;

		if (file[0] == '~') {
			char * home = getenv("HOME");
			if (home) {
				_file = malloc(strlen(file) + strlen(home) + 4); /* Paranoia */
				sprintf(_file, "%s%s", home, file+1);
			}
		}

		if (file_is_open(_file)) {
			if (file != _file) free(_file);
			close_buffer();
			return;
		}

		struct stat statbuf;
		if (!stat(_file, &statbuf) && S_ISDIR(statbuf.st_mode)) {
			read_directory_into_buffer(_file);
			if (file != _file) free(_file);
			return;
		}
		f = fopen(_file, "r");
		if (file != _file) free(_file);
		if (!f && errno != ENOENT) {
			render_error("%s: %s", file, strerror(errno));
			pause_for_key();
			close_buffer();
			return;
		}
		env->file_name = strdup(file);
	}

	if (!f) {
		if (global_config.highlight_on_open) {
			env->syntax = match_syntax(file);
		}
		env->loading = 0;
		if (global_config.go_to_line) {
			goto_line(1);
		}
		if (env->syntax && env->syntax->prefers_spaces) {
			env->tabs = 0;
		}
		update_biminfo(env, 1);
		run_onload(env);
		return;
	}

	uint8_t buf[BLOCK_SIZE];

	state = 0;

	while (!feof(f) && !ferror(f)) {
		size_t r = fread(buf, 1, BLOCK_SIZE, f);
		add_buffer(buf, r);
	}

	if (ferror(f)) {
		env->loading = 0;
		return;
	}

	if (env->line_no && env->lines[env->line_no-1] && env->lines[env->line_no-1]->actual == 0) {
		/* Remove blank line from end */
		env->lines = remove_line(env->lines, env->line_no-1);
	}

	if (global_config.highlight_on_open) {
		env->syntax = match_syntax(file);
		if (!env->syntax) {
			if (line_matches(env->lines[0], "<?xml")) set_syntax_by_name("xml");
			else if (line_matches(env->lines[0], "<!doctype")) set_syntax_by_name("xml");
			else if (line_matches(env->lines[0], "#!/usr/bin/env bash")) set_syntax_by_name("bash");
			else if (line_matches(env->lines[0], "#!/bin/bash")) set_syntax_by_name("bash");
			else if (line_matches(env->lines[0], "#!/bin/sh")) set_syntax_by_name("bash");
			else if (line_matches(env->lines[0], "#!/usr/bin/env python")) set_syntax_by_name("py");
			else if (line_matches(env->lines[0], "#!/usr/bin/env groovy")) set_syntax_by_name("groovy");
		}
		if (!env->syntax && global_config.syntax_fallback) {
			set_syntax_by_name(global_config.syntax_fallback);
		}
		schedule_complete_recalc();
	}

	/* Try to automatically figure out tabs vs. spaces */
	int tabs = 0, spaces = 0;
	for (int i = 0; i < env->line_count; ++i) {
		if (env->lines[i]->actual > 1) { /* Make sure line has at least some text on it */
			if (env->lines[i]->text[0].codepoint == '\t') tabs++;
			if (env->lines[i]->text[0].codepoint == ' ' &&
				env->lines[i]->text[1].codepoint == ' ') /* Ignore spaces at the start of asterisky C comments */
				spaces++;
		}
	}
	if (spaces > tabs) {
		env->tabs = 0;
	} else if (spaces == tabs && env->syntax) {
		env->tabs = env->syntax->prefers_spaces;
	}

	if (spaces > tabs) {
		int one = 0, two = 0, three = 0, four = 0; /* If you use more than that, I don't like you. */
		int lastCount = 0;
		for (int i = 0; i < env->line_count; ++i) {
			if (env->lines[i]->actual > 1 && !line_is_comment(env->lines[i])) {
				/* Count spaces at beginning */
				int c = 0, diff = 0;
				while (c < env->lines[i]->actual && env->lines[i]->text[c].codepoint == ' ') c++;
				if (c > lastCount) {
					diff = c - lastCount;
				} else if (c < lastCount) {
					diff = lastCount - c;
				}
				if (diff == 1) one++;
				if (diff == 2) two++;
				if (diff == 3) three++;
				if (diff == 4) four++;
				lastCount = c;
			}
		}
		if (four > three && four > two && four > one) {
			env->tabstop = 4;
		} else if (three > two && three > one) {
			env->tabstop = 3;
		} else if (two > one) {
			env->tabstop = 2;
		} else {
			env->tabstop = 1;
		}
	}

	env->loading = 0;

	if (global_config.check_git) {
		env->checkgitstatusonwrite = 1;
		git_examine(file);
	}

	for (int i = 0; i < env->line_count; ++i) {
		recalculate_tabs(env->lines[i]);
	}

	if (global_config.go_to_line) {
		if (init_line != -1) {
			goto_line(init_line);
		} else {
			env->line_no = 1;
			env->col_no = 1;
			fetch_from_biminfo(env);
			place_cursor_actual();
			redraw_all();
			set_preferred_column();
		}
	}

	update_biminfo(env, 1);

	fclose(f);

	run_onload(env);
}

/**
 * Clean up the terminal and exit the editor.
 */
void quit(const char * message) {
	mouse_disable();
	set_buffered();
	reset();
	clear_screen();
	show_cursor();
	unset_bracketed_paste();
	unset_alternate_screen();
	krk_freeVM();
	if (message) {
		fprintf(stdout, "%s\n", message);
	}
	exit(0);
}

/**
 * Try to quit, but don't continue if there are
 * modified buffers open.
 */
void try_quit(void) {
	for (int i = 0; i < buffers_len; i++ ) {
		buffer_t * _env = buffers[i];
		if (_env->modified) {
			if (_env->file_name) {
				render_error("Modifications made to file `%s` in tab %d. Aborting.", _env->file_name, i+1);
			} else {
				render_error("Unsaved new file in tab %d. Aborting.", i+1);
			}
			return;
		}
	}
	/* Close all buffers */
	while (buffers_len) {
		buffer_close(buffers[0]);
	}
	quit(NULL);
}

/**
 * Switch to the previous buffer
 */
BIM_ACTION(previous_tab, 0,
	"Switch the previous tab"
,void) {
	buffer_t * last = NULL;
	for (int i = 0; i < buffers_len; i++) {
		buffer_t * _env = buffers[i];
		if (_env == env) {
			if (last) {
				/* Wrap around */
				env = last;
				if (left_buffer && (left_buffer != env && right_buffer != env)) unsplit();
				redraw_all();
				update_title();
				return;
			} else {
				env = buffers[buffers_len-1];
				if (left_buffer && (left_buffer != env && right_buffer != env)) unsplit();
				redraw_all();
				update_title();
				return;
			}
		}
		last = _env;
	}
}

/**
 * Switch to the next buffer
 */
BIM_ACTION(next_tab, 0,
	"Switch to the next tab"
,void) {
	for (int i = 0; i < buffers_len; i++) {
		buffer_t * _env = buffers[i];
		if (_env == env) {
			if (i != buffers_len - 1) {
				env = buffers[i+1];
				if (left_buffer && (left_buffer != env && right_buffer != env)) unsplit();
				redraw_all();
				update_title();
				return;
			} else {
				/* Wrap around */
				env = buffers[0];
				if (left_buffer && (left_buffer != env && right_buffer != env)) unsplit();
				redraw_all();
				update_title();
				return;
			}
		}
	}
}

/**
 * Check for modified lines in a file by examining `git diff` output.
 * This can be enabled globally in bimrc or per environment with the 'git' option.
 */
int git_examine(char * filename) {
	if (env->modified) return 1;
	int fds[2];
	pipe(fds);
	int child = fork();
	if (child == 0) {
		FILE * dev_null = fopen("/dev/null","w");
		close(fds[0]);
		dup2(fds[1], STDOUT_FILENO);
		dup2(fileno(dev_null), STDERR_FILENO);
		char * args[] = {"git","--no-pager","diff","-U0","--no-color","--",filename,NULL};
		exit(execvp("git",args));
	} else if (child < 0) {
		return 1;
	}

	close(fds[1]);
	FILE * f = fdopen(fds[0],"r");

	int line_offset = 0;
	while (!feof(f)) {
		int c = fgetc(f);
		if (c < 0) break;

		if (c == '@' && line_offset == 0) {
			/* Read line offset, count */
			if (fgetc(f) == '@' && fgetc(f) == ' ' && fgetc(f) == '-') {
				/* This algorithm is borrowed from Kakoune and only requires us to parse the @@ line */
				int from_line = 0;
				int from_count = 0;
				int to_line = 0;
				int to_count = 0;
				fscanf(f,"%d",&from_line);
				if (fgetc(f) == ',') {
					fscanf(f,"%d",&from_count);
				} else {
					from_count = 1;
				}
				fscanf(f,"%d",&to_line);
				if (fgetc(f) == ',') {
					fscanf(f,"%d",&to_count);
				} else {
					to_count = 1;
				}

				if (to_line > env->line_count) continue;

				if (from_count == 0 && to_count > 0) {
					/* No -, all + means all of to_count is green */
					for (int i = 0; i < to_count; ++i) {
						env->lines[to_line+i-1]->rev_status = 1; /* Green */
					}
				} else if (from_count > 0 && to_count == 0) {
					/*
					 * No +, all - means we have a deletion. We mark the next line such that it has a red bar at the top
					 * Note that to_line is one lower than the affected line, so we don't need to mess with indexes.
					 */
					if (to_line >= env->line_count) continue;
					env->lines[to_line]->rev_status = 4; /* Red */
				} else if (from_count > 0 && from_count == to_count) {
					/* from = to, all modified */
					for (int i = 0; i < to_count; ++i) {
						env->lines[to_line+i-1]->rev_status = 3; /* Blue */
					}
				} else if (from_count > 0 && from_count < to_count) {
					/* from < to, some modified, some added */
					for (int i = 0; i < from_count; ++i) {
						env->lines[to_line+i-1]->rev_status = 3; /* Blue */
					}
					for (int i = from_count; i < to_count; ++i) {
						env->lines[to_line+i-1]->rev_status = 1; /* Green */
					}
				} else if (to_count > 0 && from_count > to_count) {
					/* from > to, we deleted but also modified some lines */
					env->lines[to_line-1]->rev_status = 5; /* Red + Blue */
					for (int i = 1; i < to_count; ++i) {
						env->lines[to_line+i-1]->rev_status = 3; /* Blue */
					}
				}
			}
		}

		if (c == '\n') {
			line_offset = 0;
			continue;
		}

		line_offset++;
	}

	fclose(f);
	waitpid(-1,NULL,WNOHANG);
	return 0;
}

/**
 * Write file contents to FILE
 */
void output_file(buffer_t * env, FILE * f) {
	int i, j;
	for (i = 0; i < env->line_count; ++i) {
		line_t * line = env->lines[i];
		line->rev_status = 0;
		for (j = 0; j < line->actual; j++) {
			char_t c = line->text[j];
			if (c.codepoint == 0) {
				char buf[1] = {0};
				fwrite(buf, 1, 1, f);
			} else {
				char tmp[8] = {0};
				int i = to_eight(c.codepoint, tmp);
				fwrite(tmp, i, 1, f);
			}
		}
		if (env->crnl) fputc('\r', f);
		fputc('\n', f);
	}
}

/**
 * Write active buffer to file
 */
void write_file(char * file) {
	if (!file) {
		render_error("Need a file to write to.");
		return;
	}

	char * _file = file;

	if (file[0] == '~') {
		char * home = getenv("HOME");
		if (home) {
			_file = malloc(strlen(file) + strlen(home) + 4); /* Paranoia */
			sprintf(_file, "%s%s", home, file+1);
		}
	}


	FILE * f = fopen(_file, "w+");
	if (file != _file) free(_file);

	if (!f) {
		render_error("Failed to open file for writing.");
		return;
	}

	/* Go through each line and convert it back to UTF-8 */
	output_file(env, f);

	fclose(f);

	/* Mark it no longer modified */
	env->modified = 0;
	env->last_save_history = env->history;

	/* If there was no file name set, set one */
	if (!env->file_name) {
		env->file_name = malloc(strlen(file) + 1);
		memcpy(env->file_name, file, strlen(file) + 1);
	}

	if (env->checkgitstatusonwrite) {
		git_examine(file);
	}

	update_title();
	redraw_all();
}

/**
 * Close the active buffer
 */
void close_buffer(void) {
	buffer_t * previous_env = env;
	buffer_t * new_env = buffer_close(env);
	if (new_env == previous_env) {
		/* ?? Failed to close buffer */
		render_error("lolwat");
	}
	if (left_buffer && env == left_buffer) {
		left_buffer = NULL;
		right_buffer->left = 0;
		right_buffer->width = global_config.term_width;
		right_buffer = NULL;
	} else if (left_buffer && env == right_buffer) {
		right_buffer = NULL;
		left_buffer->left = 0;
		left_buffer->width = global_config.term_width;
		left_buffer = NULL;
	}
	/* No more buffers, exit */
	if (!new_env) {
		quit(NULL);
	}

	/* Set the new active buffer */
	env = new_env;

	/* Redraw the screen */
	redraw_all();
	update_title();
}

/**
 * Set the visual column the cursor should attempt to keep
 * when moving up and down based on where the cursor currently is.
 * This should happen whenever the user intentionally changes
 * the cursor's horizontal positioning, such as with left/right
 * arrow keys, word-move, search, mouse, etc.
 */
void set_preferred_column(void) {
	int c = 0;
	for (int i = 0; i < env->lines[env->line_no-1]->actual && i < env->col_no-1; ++i) {
		c += env->lines[env->line_no-1]->text[i].display_width;
	}
	env->preferred_column = c;
}

BIM_ACTION(cursor_down, 0,
	"Move the cursor one line down."
,void) {
	/* If this isn't already the last line... */
	if (env->line_no < env->line_count) {

		/* Move the cursor down */
		env->line_no += 1;

		/* Try to place the cursor horizontally at the preferred column */
		int _x = 0;
		for (int i = 0; i < env->lines[env->line_no-1]->actual; ++i) {
			char_t * c = &env->lines[env->line_no-1]->text[i];
			_x += c->display_width;
			env->col_no = i+1;
			if (_x > env->preferred_column) {
				break;
			}
		}

		if (env->mode == MODE_INSERT && _x <= env->preferred_column) {
			env->col_no = env->lines[env->line_no-1]->actual + 1;
		}

		/*
		 * If the horizontal cursor position exceeds the width the new line,
		 * then move the cursor left to the extent of the new line.
		 *
		 * If we're in insert mode, we can go one cell beyond the end of the line
		 */
		if (env->col_no > env->lines[env->line_no-1]->actual + (env->mode == MODE_INSERT)) {
			env->col_no = env->lines[env->line_no-1]->actual + (env->mode == MODE_INSERT);
			if (env->col_no == 0) env->col_no = 1;
		}

		if (env->loading) return;

		/*
		 * If the screen was scrolled horizontally, unscroll it;
		 * if it will be scrolled on this line as well, that will
		 * be handled by place_cursor_actual
		 */
		int redraw = 0;
		if (env->coffset != 0) {
			env->coffset = 0;
			redraw = 1;
		}

		/* If we've scrolled past the bottom of the screen, scroll the screen */
		if (env->line_no > env->offset + global_config.term_height - global_config.bottom_size - global_config.tabs_visible - global_config.cursor_padding) {
			env->offset += 1;

			/* Tell terminal to scroll */
			if (global_config.can_scroll && !left_buffer) {
				if (!global_config.can_insert) {
					shift_up(1);
					redraw_tabbar();
				} else {
					delete_lines_at(global_config.tabs_visible ? 2 : 1, 1);
				}

				/* A new line appears on screen at the bottom, draw it */
				int l = global_config.term_height - global_config.bottom_size - global_config.tabs_visible;
				if (env->offset + l < env->line_count + 1) {
					redraw_line(env->offset + l-1);
				} else {
					draw_excess_line(l - 1);
				}
			} else {
				redraw_text();
			}
			redraw_statusbar();
			redraw_commandline();
			place_cursor_actual();
			return;
		} else if (redraw) {
			/* Otherwise, if we need to redraw because of coffset change, do that */
			redraw_text();
		}

		set_history_break();

		/* Update the status bar */
		redraw_statusbar();

		/* Place the terminal cursor again */
		place_cursor_actual();
	}
}

BIM_ACTION(cursor_up, 0,
	"Move the cursor up one line."
,void) {
	/* If this isn't the first line already */
	if (env->line_no > 1) {

		/* Move the cursor down */
		env->line_no -= 1;

		/* Try to place the cursor horizontally at the preferred column */
		int _x = 0;
		for (int i = 0; i < env->lines[env->line_no-1]->actual; ++i) {
			char_t * c = &env->lines[env->line_no-1]->text[i];
			_x += c->display_width;
			env->col_no = i+1;
			if (_x > env->preferred_column) {
				break;
			}
		}

		if (env->mode == MODE_INSERT && _x <= env->preferred_column) {
			env->col_no = env->lines[env->line_no-1]->actual + 1;
		}

		/*
		 * If the horizontal cursor position exceeds the width the new line,
		 * then move the cursor left to the extent of the new line.
		 *
		 * If we're in insert mode, we can go one cell beyond the end of the line
		 */
		if (env->col_no > env->lines[env->line_no-1]->actual + (env->mode == MODE_INSERT)) {
			env->col_no = env->lines[env->line_no-1]->actual + (env->mode == MODE_INSERT);
			if (env->col_no == 0) env->col_no = 1;
		}

		if (env->loading) return;

		/*
		 * If the screen was scrolled horizontally, unscroll it;
		 * if it will be scrolled on this line as well, that will
		 * be handled by place_cursor_actual
		 */
		int redraw = 0;
		if (env->coffset != 0) {
			env->coffset = 0;
			redraw = 1;
		}

		int e = (env->offset == 0) ? env->offset : env->offset + global_config.cursor_padding;
		if (env->line_no <= e) {
			env->offset -= 1;

			/* Tell terminal to scroll */
			if (global_config.can_scroll && !left_buffer) {
				if (!global_config.can_insert) {
					shift_down(1);
					redraw_tabbar();
				} else {
					insert_lines_at(global_config.tabs_visible ? 2 : 1, 1);
				}

				/*
				 * The line at the top of the screen should always be real
				 * so we can just call redraw_line here
				 */
				redraw_line(env->offset);
			} else {
				redraw_tabbar();
				redraw_text();
			}
			redraw_statusbar();
			redraw_commandline();
			place_cursor_actual();
			return;
		} else if (redraw) {
			/* Otherwise, if we need to redraw because of coffset change, do that */
			redraw_text();
		}

		set_history_break();

		/* Update the status bar */
		redraw_statusbar();

		/* Place the terminal cursor again */
		place_cursor_actual();
	}
}

BIM_ACTION(cursor_left, 0,
	"Move the cursor one character to the left."
,void) {
	if (env->col_no > 1) {
		env->col_no -= 1;

		/* Update the status bar */
		redraw_statusbar();

		/* Place the terminal cursor again */
		place_cursor_actual();
	}
	set_history_break();
	set_preferred_column();
}

BIM_ACTION(cursor_right, 0,
	"Move the cursor one character to the right."
,void) {

	/* If this isn't already the rightmost column we can reach on this line in this mode... */
	if (env->col_no < env->lines[env->line_no-1]->actual + !!(env->mode == MODE_INSERT)) {
		env->col_no += 1;

		/* Update the status bar */
		redraw_statusbar();

		/* Place the terminal cursor again */
		place_cursor_actual();
	}
	set_history_break();
	set_preferred_column();
}

BIM_ACTION(cursor_home, 0,
	"Move the cursor to the beginning of the line."
,void) {
	env->col_no = 1;
	set_history_break();
	set_preferred_column();

	/* Update the status bar */
	redraw_statusbar();

	/* Place the terminal cursor again */
	place_cursor_actual();
}

BIM_ACTION(cursor_end, 0,
	"Move the cursor to the end of the line, or past the end in insert mode."
,void) {
	env->col_no = env->lines[env->line_no-1]->actual+!!(env->mode == MODE_INSERT);
	set_history_break();
	set_preferred_column();

	/* Update the status bar */
	redraw_statusbar();

	/* Place the terminal cursor again */
	place_cursor_actual();
}

BIM_ACTION(leave_insert, 0,
	"Leave insert modes and return to normal mode."
,void) {
	if (env->col_no > env->lines[env->line_no-1]->actual) {
		env->col_no = env->lines[env->line_no-1]->actual;
		if (env->col_no == 0) env->col_no = 1;
		set_preferred_column();
	}
		set_history_break();
		env->mode = MODE_NORMAL;
		redraw_commandline();
}

struct MatchQualifier {
	int (*matchFunc)(struct MatchQualifier*,uint32_t,int);
	union {
		uint32_t matchChar;
		struct {
			uint32_t * start;
			uint32_t * end;
		} matchSquares;
	};
};

/**
 * Helper for handling smart case sensitivity.
 */
int match_char(struct MatchQualifier * self, uint32_t b, int mode) {
	if (mode == 0) {
		return self->matchChar == b;
	} else if (mode == 1) {
		return tolower(self->matchChar) == tolower(b);
	}
	return 0;
}

int match_squares(struct MatchQualifier * self, uint32_t c, int mode) {
	uint32_t * start = self->matchSquares.start;
	uint32_t * end = self->matchSquares.end;
	uint32_t * t = start;
	int good = 1;
	if (*t == '^') { t++; good = 0; }
	while (t != end) {
		uint32_t test = *t++;
		if (test == '\\' && *t && strchr("\\]",*t)) {
			test = *t++;
		} else if (test == '\\' && *t == 't') {
			test = '\t'; t++;
		}

		if (*t == '-') {
			t++;
			if (t == end) return 0;
			uint32_t right = *t++;
			if (right == '\\' && *t && strchr("\\]",*t)) {
				right = *t++;
			} else if (right == '\\' && *t == 't') {
				right = '\t'; t++;
			}
			if (mode ? (tolower(c) >= tolower(test) && tolower(c) <= tolower(right)) : (c >= test && c <= right)) return good;
		} else {
			if (mode ? (tolower(c) == tolower(test)) : (c == test)) return good;
		}
	}
	return !good;
}

int match_dot(struct MatchQualifier * self, uint32_t c, int mode) {
	return 1;
}

struct BackRef {
	int start;
	int len;
	uint32_t * copy;
};

#define MAX_REFS 10
int regex_matches(line_t * line, int j, uint32_t * needle, int ignorecase, int *len, uint32_t **needleout, int refindex, struct BackRef * refs) {
	int k = j;
	uint32_t * match = needle;
	if (*match == '^') {
		if (j != 0) return 0;
		match++;
	}
	while (k < line->actual + 1) {
		if (needleout && *match == ')') {
			*needleout = match + 1;
			if (len) *len = k - j;
			return 1;
		}
		if (*match == '\0') {
			if (needleout) return 0;
			if (len) *len = k - j;
			return 1;
		}
		if (*match == '$') {
			if (k != line->actual) return 0;
			match++;
			continue;
		}
		if (k == line->actual) break;

		struct MatchQualifier matcher = {match_char, .matchChar=*match};
		if (*match == '.') {
			matcher.matchFunc = match_dot;
			match++;
		} else if (*match == '\\' && strchr("$^/\\.[?]*+()",match[1]) != NULL) {
			matcher.matchChar = match[1];
			match += 2;
		} else if (*match == '\\' && match[1] == 't') {
			matcher.matchChar = '\t';
			match += 2;
		} else if (*match == '[') {
			uint32_t * s = match+1;
			uint32_t * e = s;
			while (*e && *e != ']') {
				if (*e == '\\' && e[1] == ']') e++;
				e++;
			}
			if (!*e) break; /* fail match on unterminated [] sequence */
			match = e + 1;
			matcher.matchFunc = match_squares;
			matcher.matchSquares.start = s;
			matcher.matchSquares.end = e;
		} else if (*match == '(') {
			match++;
			int _len;
			uint32_t * newmatch;
			if (!regex_matches(line, k, match, ignorecase, &_len, &newmatch, 0, NULL)) break;
			match = newmatch;
			if (refindex && refindex < MAX_REFS) {
				refs[refindex].start = k;
				refs[refindex].len = _len;
				refindex++;
			}
			k += _len;
			continue;
		} else {
			match++;
		}
		if (*match == '?') {
			/* Optional */
			match++;
			if (matcher.matchFunc(&matcher, line->text[k].codepoint, ignorecase)) {
				int _len;
				if (regex_matches(line,k+1,match,ignorecase,&_len, needleout, refindex, refs)) {
					if (len) *len = _len + k + 1 - j;
					return 1;
				}
			}
			continue;
		} else if (*match == '+' || *match == '*') {
			/* Must match at least one */
			if (*match == '+') {
				if (!matcher.matchFunc(&matcher, line->text[k].codepoint, ignorecase)) break;
				k++;
			}
			/* Match any */
			match++;
			int greedy = 1;
			if (*match == '?') {
				/* non-greedy */
				match++;
				greedy = 0;
			}

			int _j = k;
			while (_j < line->actual + 1) {
				int _len;
				if (!greedy && regex_matches(line, _j, match, ignorecase, &_len, needleout, refindex, refs)) {
					if (len) *len = _len + _j - j;
					return 1;
				}
				if (_j < line->actual && !matcher.matchFunc(&matcher, line->text[_j].codepoint, ignorecase)) break;
				_j++;
			}
			if (!greedy) return 0;
			while (_j >= k) {
				int _len;
				if (regex_matches(line, _j, match, ignorecase, &_len, needleout, refindex, refs)) {
					if (len) *len = _len + _j - j;
					return 1;
				}
				_j--;
			}
			return 0;
		} else {
			if (!matcher.matchFunc(&matcher, line->text[k].codepoint, ignorecase)) break;
			k++;
		}
	}
	return 0;
}

int subsearch_matches(line_t * line, int j, uint32_t * needle, int ignorecase, int *len) {
	return regex_matches(line, j, needle, ignorecase, len, NULL, 0, NULL);
}

/**
 * Replace text on a given line with other text.
 */
void perform_replacement(int line_no, uint32_t * needle, uint32_t * replacement, int col, int ignorecase, int *out_col, int *line_out) {
	line_t * line = env->lines[line_no-1];
	int j = col;
	while (j < line->actual + 1) {
		int match_len;
		struct BackRef refs[MAX_REFS] = {0};
		if (regex_matches(line,j,needle,ignorecase,&match_len,NULL,1,refs)) {
			refs[0].start = j;
			refs[0].len = match_len;
			for (int i = 0; i < MAX_REFS; ++i) {
				refs[i].copy = malloc(sizeof(uint32_t) * refs[i].len);
				for (int j = 0; j < refs[i].len; ++j) {
					refs[i].copy[j] = line->text[j+refs[i].start].codepoint;
				}
			}

			/* Perform replacement */
			for (int i = 0; i < match_len; ++i) {
				line_delete(line, j+1, line_no-1);
			}
			int t = 0;
			for (uint32_t * r = replacement; *r; ++r) {
				uint32_t rep = *r;
				char_t _c;
				_c.flags = 0;
				line_t * nline = line;
				if (*r == '\\' && r[1] == 't') {
					rep = '\t';
					++r;
				} else if (*r == '\\' && (r[1] == '\\')) {
					rep = r[1];
					++r;
				} else if (*r == '\\' && (r[1] >= '0' && r[1] <= '9')) {
					int i = r[1] - '0';
					++r;
					nline = line;
					for (int k = 0; k < refs[i].len; ++k) {
						_c.codepoint = refs[i].copy[k];
						_c.display_width = codepoint_width(refs[i].copy[k]);
						nline = line_insert(nline, _c, j + t + k, line_no -1);
					}
					t += refs[i].len;
					rep = 0;
				} else if (*r == '\\' && (r[1] == 'n')) {
					++r;
					env->lines = split_line(env->lines, line_no - 1, j + t);
					line_no++;
					line = env->lines[line_no-1];
					j = 0;
					t = 0;
					continue;
				}
				if (rep) {
					_c.codepoint = rep;
					_c.display_width = codepoint_width(rep);
					nline = line_insert(nline, _c, j + t, line_no -1);
					t++;
				}
				if (line != nline) {
					env->lines[line_no-1] = nline;
					line = nline;
				}
			}
			*out_col = j + t;
			*line_out = line_no;
			set_modified();

			for (int i = 0; i < MAX_REFS; ++i) {
				free(refs[i].copy);
			}

			return;
		}
		j++;
	}
	*out_col = -1;
}

#define COMMAND_HISTORY_MAX 255
unsigned char * command_history[COMMAND_HISTORY_MAX] = {NULL};
unsigned char * search_history[COMMAND_HISTORY_MAX] = {NULL};

/**
 * Add a command to the history. If that command was
 * already in history, it is moved to the front of the list;
 * otherwise, the whole list is shifted backwards and
 * overflow is freed up.
 */
void insert_command_history(unsigned char ** which_history, char * cmd) {
	/* See if this is already in the history. */
	size_t amount_to_shift = COMMAND_HISTORY_MAX - 1;
	for (int i = 0; i < COMMAND_HISTORY_MAX && which_history[i]; ++i) {
		if (!strcmp((char*)which_history[i], cmd)) {
			free(which_history[i]);
			amount_to_shift = i;
			break;
		}
	}

	/* Remove last entry that will roll off the stack */
	if (amount_to_shift == COMMAND_HISTORY_MAX - 1) {
		if (which_history[COMMAND_HISTORY_MAX-1]) free(which_history[COMMAND_HISTORY_MAX-1]);
	}

	/* Roll the history */
	memmove(&which_history[1], &which_history[0], sizeof(char *) * (amount_to_shift));

	which_history[0] = (unsigned char*)strdup(cmd);
}

static uint32_t term_colors[] = {
 0x000000, 0xcc0000, 0x3e9a06, 0xc4a000, 0x3465a4, 0x75507b, 0x06989a, 0xeeeeec, 0x555753, 0xef2929, 0x8ae234, 0xfce94f, 0x729fcf, 0xad7fa8, 0x34e2e2,
 0xFFFFFF, 0x000000, 0x00005f, 0x000087, 0x0000af, 0x0000d7, 0x0000ff, 0x005f00, 0x005f5f, 0x005f87, 0x005faf, 0x005fd7, 0x005fff, 0x008700, 0x00875f,
 0x008787, 0x0087af, 0x0087d7, 0x0087ff, 0x00af00, 0x00af5f, 0x00af87, 0x00afaf, 0x00afd7, 0x00afff, 0x00d700, 0x00d75f, 0x00d787, 0x00d7af, 0x00d7d7,
 0x00d7ff, 0x00ff00, 0x00ff5f, 0x00ff87, 0x00ffaf, 0x00ffd7, 0x00ffff, 0x5f0000, 0x5f005f, 0x5f0087, 0x5f00af, 0x5f00d7, 0x5f00ff, 0x5f5f00, 0x5f5f5f,
 0x5f5f87, 0x5f5faf, 0x5f5fd7, 0x5f5fff, 0x5f8700, 0x5f875f, 0x5f8787, 0x5f87af, 0x5f87d7, 0x5f87ff, 0x5faf00, 0x5faf5f, 0x5faf87, 0x5fafaf, 0x5fafd7,
 0x5fafff, 0x5fd700, 0x5fd75f, 0x5fd787, 0x5fd7af, 0x5fd7d7, 0x5fd7ff, 0x5fff00, 0x5fff5f, 0x5fff87, 0x5fffaf, 0x5fffd7, 0x5fffff, 0x870000, 0x87005f,
 0x870087, 0x8700af, 0x8700d7, 0x8700ff, 0x875f00, 0x875f5f, 0x875f87, 0x875faf, 0x875fd7, 0x875fff, 0x878700, 0x87875f, 0x878787, 0x8787af, 0x8787d7,
 0x8787ff, 0x87af00, 0x87af5f, 0x87af87, 0x87afaf, 0x87afd7, 0x87afff, 0x87d700, 0x87d75f, 0x87d787, 0x87d7af, 0x87d7d7, 0x87d7ff, 0x87ff00, 0x87ff5f,
 0x87ff87, 0x87ffaf, 0x87ffd7, 0x87ffff, 0xaf0000, 0xaf005f, 0xaf0087, 0xaf00af, 0xaf00d7, 0xaf00ff, 0xaf5f00, 0xaf5f5f, 0xaf5f87, 0xaf5faf, 0xaf5fd7,
 0xaf5fff, 0xaf8700, 0xaf875f, 0xaf8787, 0xaf87af, 0xaf87d7, 0xaf87ff, 0xafaf00, 0xafaf5f, 0xafaf87, 0xafafaf, 0xafafd7, 0xafafff, 0xafd700, 0xafd75f,
 0xafd787, 0xafd7af, 0xafd7d7, 0xafd7ff, 0xafff00, 0xafff5f, 0xafff87, 0xafffaf, 0xafffd7, 0xafffff, 0xd70000, 0xd7005f, 0xd70087, 0xd700af, 0xd700d7,
 0xd700ff, 0xd75f00, 0xd75f5f, 0xd75f87, 0xd75faf, 0xd75fd7, 0xd75fff, 0xd78700, 0xd7875f, 0xd78787, 0xd787af, 0xd787d7, 0xd787ff, 0xd7af00, 0xd7af5f,
 0xd7af87, 0xd7afaf, 0xd7afd7, 0xd7afff, 0xd7d700, 0xd7d75f, 0xd7d787, 0xd7d7af, 0xd7d7d7, 0xd7d7ff, 0xd7ff00, 0xd7ff5f, 0xd7ff87, 0xd7ffaf, 0xd7ffd7,
 0xd7ffff, 0xff0000, 0xff005f, 0xff0087, 0xff00af, 0xff00d7, 0xff00ff, 0xff5f00, 0xff5f5f, 0xff5f87, 0xff5faf, 0xff5fd7, 0xff5fff, 0xff8700, 0xff875f,
 0xff8787, 0xff87af, 0xff87d7, 0xff87ff, 0xffaf00, 0xffaf5f, 0xffaf87, 0xffafaf, 0xffafd7, 0xffafff, 0xffd700, 0xffd75f, 0xffd787, 0xffd7af, 0xffd7d7,
 0xffd7ff, 0xffff00, 0xffff5f, 0xffff87, 0xffffaf, 0xffffd7, 0xffffff, 0x080808, 0x121212, 0x1c1c1c, 0x262626, 0x303030, 0x3a3a3a, 0x444444, 0x4e4e4e,
 0x585858, 0x626262, 0x6c6c6c, 0x767676, 0x808080, 0x8a8a8a, 0x949494, 0x9e9e9e, 0xa8a8a8, 0xb2b2b2, 0xbcbcbc, 0xc6c6c6, 0xd0d0d0, 0xdadada, 0xe4e4e4,
 0xeeeeee,
};

/**
 * Convert a color setting from terminal format
 * to a hexadecimal color code and add it to the current
 * buffer. This is used for HTML conversion, but could
 * possibly be used for other purposes.
 */
static void html_convert_color(const char * color_string) {
	char tmp[100];
	if (!strncmp(color_string,"2;",2)) {
		/* 24-bit color */
		int red, green, blue;
		sscanf(color_string+2,"%d;%d;%d",&red,&green,&blue);
		sprintf(tmp, "#%02x%02x%02x;", red,green,blue);
	} else if (!strncmp(color_string,"5;",2)) {
		/* 256 colors; needs lookup table */
		int index;
		sscanf(color_string+2,"%d",&index);
		sprintf(tmp,"#%06x;", (unsigned int)term_colors[(index >= 0 && index <= 255) ? index : 0]);
	} else {
		/* 16 colors; needs lookup table */
		int index;
		uint32_t color;
		sscanf(color_string+1,"%d",&index);
		if (index >= 10) {
			index -= 10;
			color = term_colors[index+8];
		} else if (index == 9) {
			color = term_colors[0];
		} else {
			color = term_colors[index];
		}
		sprintf(tmp,"#%06x;", (unsigned int)color);
	}
	add_string(tmp);
	char * italic = strstr(color_string,";3");
	if (italic && italic[2] == '\0') {
		add_string(" font-style: oblique;");
	}
	char * bold = strstr(color_string,";1");
	if (bold && bold[2] == '\0') {
		add_string(" font-weight: bold;");
	}
	char * underline = strstr(color_string,";4");
	if (underline && underline[2] == '\0') {
		add_string(" font-decoration: underline;");
	}
}

int convert_to_html(void) {
	buffer_t * old = env;
	env = buffer_new();
	setup_buffer(env);
	env->loading = 1;

	add_string("<!doctype html>\n");
	add_string("<html>\n");
	add_string("	<head>\n");
	add_string("		<meta charset=\"UTF-8\">\n");
	if (old->file_name) {
		add_string("		<title>");
		add_string(file_basename(old->file_name));
		add_string("</title>\n");
	}
	add_string("		<style>\n");
	add_string("			body {\n");
	add_string("				margin: 0;\n");
	add_string("				-webkit-text-size-adjust: none;\n");
	add_string("				counter-reset: line-no;\n");
	add_string("				background-color: ");
	/* Convert color */
	html_convert_color(COLOR_BG);
	add_string("\n");
	add_string("			}\n");
	add_string("			.ul { text-decoration: underline; }\n");
	for (int i = 0; i < 15; ++i) {
		/* For each of the relevant flags... */
		char tmp[20];
		sprintf(tmp,"			.s%d { color: ", i);
		add_string(tmp);
		/* These are special */
		if (i == FLAG_NOTICE) {
			html_convert_color(COLOR_SEARCH_FG);
			add_string(" background-color: ");
			html_convert_color(COLOR_SEARCH_BG);
		} else if (i == FLAG_ERROR) {
			html_convert_color(COLOR_ERROR_FG);
			add_string(" background-color: ");
			html_convert_color(COLOR_ERROR_BG);
		} else {
			html_convert_color(flag_to_color(i));
		}
		add_string("}\n");
	}
	add_string("			pre {\n");
	add_string("				margin: 0;\n");
	add_string("				white-space: pre-wrap;\n");
	add_string("				font-family: \"DejaVu Sans Mono\", Courier, monospace;\n");
	add_string("				font-size: 10pt;\n");
	add_string("			}\n");
	add_string("			pre>span {\n");
	add_string("				display: inline-block;\n");
	add_string("				width: 100%;\n");
	add_string("			}\n");
	add_string("			pre>span>a::before {\n");
	add_string("				counter-increment: line-no;\n");
	add_string("				content: counter(line-no);\n");
	add_string("				padding-right: 1em;\n");
	add_string("				width: 3em;\n");
	add_string("				display: inline-block;\n");
	add_string("				text-align: right;\n");
	add_string("				background-color: ");
	html_convert_color(COLOR_NUMBER_BG);
	add_string("\n");
	add_string("				color: ");
	html_convert_color(COLOR_NUMBER_FG);
	add_string("\n");
	add_string("			}\n");
	add_string("			pre>span:target {\n");
	add_string("				background-color: ");
	html_convert_color(COLOR_ALT_BG);
	add_string("\n");
	add_string("			}\n");
	add_string("			pre>span:target>a::before {\n");
	add_string("				background-color: ");
	html_convert_color(COLOR_NUMBER_FG);
	add_string("\n");
	add_string("				color: ");
	html_convert_color(COLOR_NUMBER_BG);
	add_string("\n");
	add_string("			}\n");
	for (int i = 1; i <= env->tabstop; ++i) {
		char tmp[20];
		sprintf(tmp, ".tab%d", i);
		add_string("			");
		add_string(tmp);
		add_string(">span {\n");
		add_string("				display: inline-block;\n");
		add_string("				overflow: hidden;\n");
		add_string("				width: 0;\n");
		add_string("				height: 0;\n");
		add_string("			}\n");
		add_string("			");
		add_string(tmp);
		add_string("::after {\n");
		add_string("				content: '»");
		for (int j = 1; j < i; ++j) {
			add_string("·");
		}
		add_string("';\n");
		add_string("				background-color: ");
		html_convert_color(COLOR_ALT_BG);
		add_string("\n");
		add_string("				color: ");
		html_convert_color(COLOR_ALT_FG);
		add_string("\n");
		add_string("			}\n");
	}
	add_string("			.space {\n");
	add_string("				border-left: 1px solid ");
	html_convert_color(COLOR_ALT_FG);
	add_string("\n");
	add_string("				margin-left: -1px;\n");
	add_string("			}\n");
	add_string("		</style>\n");
	add_string("	</head>\n");
	add_string("	<body><pre>\n");

	for (int i = 0; i < old->line_count; ++i) {
		char tmp[100];
		sprintf(tmp, "<span id=\"L%d\"><a href=\"#L%d\"></a>", i+1, i+1);
		add_string(tmp);
		int last_flag = -1;
		int opened = 0;
		int all_spaces = 1;
		for (int j = 0; j < old->lines[i]->actual; ++j) {
			char_t c = old->lines[i]->text[j];

			if (c.codepoint != ' ') all_spaces = 0;

			if (last_flag == -1 || last_flag != (c.flags & 0x1F)) {
				if (opened) add_string("</span>");
				opened = 1;
				char tmp[100];
				sprintf(tmp, "<span class=\"s%d%s\">",
					c.flags & FLAG_MASK_COLORS,
					(c.flags & FLAG_UNDERLINE) ? " ul" : "");
				add_string(tmp);
				last_flag = (c.flags & 0x1F);
			}

			if (c.codepoint == '<') {
				add_string("&lt;");
			} else if (c.codepoint == '>') {
				add_string("&gt;");
			} else if (c.codepoint == '&') {
				add_string("&amp;");
			} else if (c.codepoint == '\t') {
				char tmp[100];
				sprintf(tmp, "<span class=\"tab%d\"><span>	</span></span>",c.display_width);
				add_string(tmp);
			} else if (j > 0 && c.codepoint == ' ' && all_spaces && !(j % old->tabstop)) {
				add_string("<span class=\"space\"> </span>");
			} else {
				char tmp[7] = {0}; /* Max six bytes, use 7 to ensure last is always nil */
				to_eight(c.codepoint, tmp);
				add_string(tmp);
			}
		}
		if (opened) {
			add_string("</span>");
		} else {
			add_string("<wbr>");
		}
		add_string("</span>\n");
	}

	add_string("</pre></body>\n");
	add_string("</html>\n");

	env->loading = 0;
	env->modified = 1;
	if (old->file_name) {
		char * base = file_basename(old->file_name);
		char * tmp = malloc(strlen(base) + 5);
		*tmp = '\0';
		strcat(tmp, base);
		strcat(tmp, ".htm");
		env->file_name = tmp;
	}
	for (int i = 0; i < env->line_count; ++i) {
		recalculate_tabs(env->lines[i]);
	}
	env->syntax = match_syntax(".htm");
	schedule_complete_recalc();

	return 0;
}

/**
 * Based on vim's :TOhtml
 * Convert syntax-highlighted buffer contents to HTML.
 */
BIM_COMMAND(tohtml,"tohtml","Convert the document to an HTML representation with syntax highlighting.") {
	convert_to_html();

	redraw_all();
	return 0;
}

BIM_ALIAS("TOhtml",tohtml,tohtml)

int _prefix_command_run_script(char * cmd) {
	if (env->mode == MODE_LINE_SELECTION) {
		int range_top, range_bot;
		range_top = env->start_line < env->line_no ? env->start_line : env->line_no;
		range_bot = env->start_line < env->line_no ? env->line_no : env->start_line;

		int in[2];
		pipe(in);
		int out[2];
		pipe(out);
		int child = fork();

		/* Open child process and set up pipes */
		if (child == 0) {
			FILE * dev_null = fopen("/dev/null","w"); /* for stderr */
			close(out[0]);
			close(in[1]);
			dup2(out[1], STDOUT_FILENO);
			dup2(in[0], STDIN_FILENO);
			dup2(fileno(dev_null), STDERR_FILENO);
			system(&cmd[1]); /* Yes we can just do this */
			exit(1);
		} else if (child < 0) {
			render_error("Failed to fork");
			return 1;
		}
		close(out[1]);
		close(in[0]);

		/* Write lines to child process */
		FILE * f = fdopen(in[1],"w");
		for (int i = range_top; i <= range_bot; ++i) {
			line_t * line = env->lines[i-1];
			for (int j = 0; j < line->actual; j++) {
				char_t c = line->text[j];
				if (c.codepoint == 0) {
					char buf[1] = {0};
					fwrite(buf, 1, 1, f);
				} else {
					char tmp[8] = {0};
					int i = to_eight(c.codepoint, tmp);
					fwrite(tmp, i, 1, f);
				}
			}
			fputc('\n', f);
		}
		fclose(f);
		close(in[1]);

		/* Read results from child process into a new buffer */
		FILE * result = fdopen(out[0],"r");
		buffer_t * old = env;
		env = buffer_new();
		setup_buffer(env);
		env->loading = 1;
		uint8_t buf[BLOCK_SIZE];
		state = 0;
		while (!feof(result) && !ferror(result)) {
			size_t r = fread(buf, 1, BLOCK_SIZE, result);
			add_buffer(buf, r);
		}
		if (env->line_no && env->lines[env->line_no-1] && env->lines[env->line_no-1]->actual == 0) {
			env->lines = remove_line(env->lines, env->line_no-1);
		}
		fclose(result);
		waitpid(-1,NULL,WNOHANG);
		env->loading = 0;

		/* Return to the original buffer and replace the selected lines with the output */
		buffer_t * new = env;
		env = old;
		for (int i = range_top; i <= range_bot; ++i) {
			/* Remove the existing lines */
			env->lines = remove_line(env->lines, range_top-1);
		}
		for (int i = 0; i < new->line_count; ++i) {
			/* Add the new lines */
			env->lines = add_line(env->lines, range_top + i - 1);
			replace_line(env->lines, range_top + i - 1, new->lines[i]);
			recalculate_tabs(env->lines[range_top+i-1]);
		}

		env->modified = 1;

		/* Close the temporary buffer */
		buffer_close(new);
	} else {
		/* Reset and draw some line feeds */
		reset();
		printf("\n\n");

		/* Set buffered for shell application */
		set_buffered();

		/* Call the shell and wait for completion */
		system(&cmd[1]);

		/* Return to the editor, wait for user to press enter. */
		set_unbuffered();
		printf("\n\nPress ENTER to continue.");
		int c;
		while ((c = bim_getch(), c != ENTER_KEY && c != LINE_FEED));

		/* Redraw the screen */
		redraw_all();
	}

	/* Done processing command */
	return 0;
}

int replace_text(int range_top, int range_bot, char divider, char * needle) {
	char * c = needle;
	char * replacement = NULL;
	char * options = "";

	while (*c) {
		if (*c == divider) {
			*c = '\0';
			replacement = c + 1;
			break;
		}
		c++;
	}

	if (!replacement) {
		render_error("nothing to replace with");
		return 1;
	}

	c = replacement;
	while (*c) {
		if (*c == divider) {
			*c = '\0';
			options = c + 1;
			break;
		}
		c++;
	}

	int global = 0;
	int case_insensitive = 0;

	/* Parse options */
	while (*options) {
		switch (*options) {
			case 'g':
				global = 1;
				break;
			case 'i':
				case_insensitive = 1;
				break;
		}
		options++;
	}

	uint32_t * needle_c = malloc(sizeof(uint32_t) * (strlen(needle) + 1));
	uint32_t * replacement_c = malloc(sizeof(uint32_t) * (strlen(replacement) + 1));

	{
		int i = 0;
		uint32_t c, state = 0;
		for (char * cin = needle; *cin; cin++) {
			if (!decode(&state, &c, *cin)) {
				needle_c[i] = c;
				i++;
			} else if (state == UTF8_REJECT) {
				state = 0;
			}
		}
		needle_c[i] = 0;
		i = 0;
		c = 0;
		state = 0;
		for (char * cin = replacement; *cin; cin++) {
			if (!decode(&state, &c, *cin)) {
				replacement_c[i] = c;
				i++;
			} else if (state == UTF8_REJECT) {
				state = 0;
			}
		}
		replacement_c[i] = 0;
	}

	int replacements = 0;
	for (int line = range_top; line <= range_bot; ++line) {
		int col = 0;
		while (col != -1) {
			int _line = line;
			perform_replacement(line, needle_c, replacement_c, col, case_insensitive, &col, &_line);
			if (col != -1) replacements++;
			if (_line > line) {
				range_bot += _line - line;
				line = _line;
			}
			if (!global) break;
		}
	}
	if (env->mode == MODE_LINE_SELECTION) {
		env->start_line = env->start_line < env->line_no ? range_top : range_bot;
		env->line_no    = env->start_line < env->line_no ? range_bot : range_top;
	}
	free(needle_c);
	free(replacement_c);
	if (replacements) {
		render_status_message("replaced %d instance%s of %s", replacements, replacements == 1 ? "" : "s", needle);
		set_history_break();
		redraw_text();
	} else {
		render_error("Pattern not found: %s", needle);
	}

	return 0;
}

BIM_PREFIX_COMMAND(repsome,"s","Perform a replacement over selected lines") {
	int range_top, range_bot;
	if (env->mode == MODE_LINE_SELECTION) {
		range_top = env->start_line < env->line_no ? env->start_line : env->line_no;
		range_bot = env->start_line < env->line_no ? env->line_no : env->start_line;
	} else {
		range_top = env->line_no;
		range_bot = env->line_no;
	}
	return replace_text(range_top, range_bot, cmd[1], &cmd[2]);
}

BIM_PREFIX_COMMAND(repall,"%s","Perform a replacement over the entire file.") {
	return replace_text(1, env->line_count, cmd[2], &cmd[3]);
}

BIM_COMMAND(e,"e","Open a file") {
	if (argc > 1) {
		/* This actually opens a new tab */
		open_file(argv[1]);
		update_title();
	} else {
		if (env->modified) {
			render_error("File is modified, can not reload.");
			return 1;
		}

		buffer_t * old_env = env;
		open_file(env->file_name);
		buffer_t * new_env = env;
		env = old_env;

#define SWAP(T,a,b) do { T x = a; a = b; b = x; } while (0)
		SWAP(line_t **, env->lines, new_env->lines);
		SWAP(int, env->line_count, new_env->line_count);
		SWAP(int, env->line_avail, new_env->line_avail);
		SWAP(history_t *, env->history, new_env->history);

		buffer_close(new_env); /* Should probably also free, this needs editing. */
		schedule_complete_recalc();
		redraw_all();
	}
	return 0;
}

BIM_COMMAND(tabnew,"tabnew","Open a new tab") {
	if (argc > 1) {
		open_file(argv[1]);
		update_title();
	} else {
		env = buffer_new();
		setup_buffer(env);
		redraw_all();
		update_title();
	}
	return 0;
}

BIM_COMMAND(w,"w","Write a file") {
	/* w: write file */
	if (argc > 1) {
		write_file(argv[1]);
	} else {
		write_file(env->file_name);
	}
	return 0;
}

BIM_COMMAND(wq,"wq","Write and close buffer") {
	write_file(env->file_name);
	close_buffer();
	return 0;
}

BIM_COMMAND(history,"history","Display command history") {
	render_commandline_message(""); /* To clear command line */
	for (int i = COMMAND_HISTORY_MAX; i > 1; --i) {
		if (command_history[i-1]) render_commandline_message("%d:%s\n", i-1, command_history[i-1]);
	}
	render_commandline_message("\n");
	redraw_tabbar();
	redraw_commandline();
	pause_for_key();
	return 0;
}

BIM_COMMAND(q,"q","Close buffer") {
	if (left_buffer && left_buffer == right_buffer) {
		unsplit();
		return 0;
	}
	if (env->modified) {
		render_error("No write since last change. Use :q! to force exit.");
	} else {
		close_buffer();
	}
	update_title();
	return 0;
}

BIM_COMMAND(qbang,"q!","Force close buffer") {
	close_buffer();
	update_title();
	return 0;
}

BIM_COMMAND(qa,"qa","Try to close all buffers") {
	try_quit();
	return 0;
}

BIM_ALIAS("qall",qall,qa)

BIM_COMMAND(qabang,"qa!","Force exit") {
	/* Forcefully exit editor */
	while (buffers_len) {
		buffer_close(buffers[0]);
	}
	quit(NULL);
	return 1; /* doesn't return */
}

BIM_COMMAND(tabp,"tabp","Previous tab") {
	previous_tab();
	update_title();
	return 0;
}

BIM_COMMAND(tabn,"tabn","Next tab") {
	next_tab();
	update_title();
	return 0;
}

BIM_COMMAND(tabm,"tabm","Move the current tab to a new index") {
	/* Figure out the current index */
	int i = 0;
	for (; i < buffers_len; i++) {
		if (buffers[i] == env) break;
	}

	if (i == buffers_len) {
		render_status_message("(invalid state?)");
		return 1;
	}

	if (argc < 2) {
		render_status_message("tab = %d", i);
		return 1;
	}

	int newIndex = atoi(argv[1]);

	if (newIndex == i) {
		return 0;
	}

	/* Okay, this is stupid, but, remove the buffer */
	memmove(&buffers[i], &buffers[i+1], sizeof(*buffers) * (buffers_len - i -1));
	/* Then make space at the destination */
	memmove(&buffers[newIndex+1], &buffers[newIndex], sizeof(*buffers) * (buffers_len - newIndex -1));

	buffers[newIndex] = env;

	redraw_tabbar();
	update_title();
	return 0;
}

BIM_COMMAND(tab,"tab", "Open a specific tab") {
	if (argc < 2) return bim_command_tabm("tabm", argc, argv);
	int i = atoi(argv[1]);

	if (i < 0 || i > buffers_len) {
		render_error("Invalid tab index");
		return 1;
	}

	env = buffers[i];
	if (left_buffer && (left_buffer != env && right_buffer != env)) unsplit();
	redraw_all();
	update_title();
	return 0;
}

BIM_COMMAND(tabindicator,"tabindicator","Set the tab indicator") {
	if (argc < 2) {
		render_status_message("tabindicator=%s", global_config.tab_indicator);
		return 0;
	}
	if (!global_config.can_unicode && strlen(argv[1]) != 1) return 0;
	if (display_width_of_string(argv[1]) != 1) {
		render_error("Can't set '%s' as indicator, must be one cell wide.", argv[1]);
		return 1;
	}
	if (global_config.tab_indicator) free(global_config.tab_indicator);
	global_config.tab_indicator = strdup(argv[1]);
	return 0;
}

BIM_COMMAND(spaceindicator,"spaceindicator","Set the space indicator") {
	if (argc < 2) {
		render_status_message("spaceindicator=%s", global_config.space_indicator);
		return 0;
	}
	if (!global_config.can_unicode && strlen(argv[1]) != 1) return 0;
	if (display_width_of_string(argv[1]) != 1) {
		render_error("Can't set '%s' as indicator, must be one cell wide.", argv[1]);
		return 1;
	}
	if (global_config.space_indicator) free(global_config.space_indicator);
	global_config.space_indicator = strdup(argv[1]);
	return 0;
}

BIM_COMMAND(global_git,"global.git","Show or change the default status of git integration") {
	if (argc < 2) {
		render_status_message("global.git=%d", global_config.check_git);
	} else {
		global_config.check_git = !!atoi(argv[1]);
	}
	return 0;
}

BIM_COMMAND(git,"git","Show or change status of git integration") {
	if (!env) {
		render_error("requires environment (did you mean global.git?)");
		return 1;
	}
	if (argc < 2) {
		render_status_message("git=%d", env->checkgitstatusonwrite);
	} else {
		env->checkgitstatusonwrite = !!atoi(argv[1]);
		if (env->checkgitstatusonwrite && !env->modified && env->file_name) {
			git_examine(env->file_name);
			redraw_text();
		}
	}
	return 0;
}

BIM_COMMAND(colorgutter,"colorgutter","Show or change status of gutter colorization for unsaved modifications") {
	if (argc < 2) {
		render_status_message("colorgutter=%d", global_config.color_gutter);
	} else {
		global_config.color_gutter = !!atoi(argv[1]);
		redraw_text();
	}
	return 0;
}

BIM_COMMAND(indent,"indent","Enable smart indentation") {
	env->indent = 1;
	redraw_statusbar();
	return 0;
}

BIM_COMMAND(noindent,"noindent","Disable smart indentation") {
	env->indent = 0;
	redraw_statusbar();
	return 0;
}

/* TODO: global.maxcolumn */
BIM_COMMAND(maxcolumn,"maxcolumn","Highlight past the given column to indicate maximum desired line length") {
	if (argc < 2) {
		render_status_message("maxcolumn=%d",env->maxcolumn);
		return 0;
	}
	env->maxcolumn = atoi(argv[1]);
	redraw_text();
	return 0;
}

BIM_COMMAND(cursorcolumn,"cursorcolumn","Show the visual column offset of the cursor.") {
	render_status_message("cursorcolumn=%d", env->preferred_column);
	return 0;
}

BIM_COMMAND(noh,"noh","Clear search term") {
	if (global_config.search) {
		free(global_config.search);
		global_config.search = NULL;
		for (int i = 0; i < env->line_count; ++i) {
			for (int j = 0; j < env->lines[i]->actual; ++j) {
				env->lines[i]->text[j].flags &= ~(FLAG_SEARCH);
			}
		}
		redraw_text();
	}
	return 0;
}

BIM_COMMAND(help,"help","Show help text.") {
	if (argc < 2) {
		render_commandline_message(""); /* To clear command line */
		render_commandline_message("\n");
		render_commandline_message(" \033[1mbim - a text editor \033[22m\n");
		render_commandline_message("\n");
		render_commandline_message(" Available commands:\n");
		render_commandline_message("   Quit with \033[3m:q\033[23m, \033[3m:qa\033[23m, \033[3m:q!\033[23m, \033[3m:qa!\033[23m\n");
		render_commandline_message("   Write out with \033[3m:w \033[4mfile\033[24;23m\n");
		render_commandline_message("   Set syntax with \033[3m:syntax \033[4mlanguage\033[24;23m\n");
		render_commandline_message("   Open a new tab with \033[3m:e \033[4mpath/to/file\033[24;23m\n");
		render_commandline_message("   \033[3m:tabn\033[23m and \033[3m:tabp\033[23m can be used to switch tabs\n");
		render_commandline_message("   Set the color scheme with \033[3m:theme \033[4mtheme\033[24;23m\n");
		render_commandline_message("   Set the behavior of the tab key with \033[3m:tabs\033[23m or \033[3m:spaces\033[23m\n");
		render_commandline_message("   Set tabstop with \033[3m:tabstop \033[4mwidth\033[24;23m\n");
		render_commandline_message("\n");
		render_commandline_message(" Bim %s%s\n", BIM_VERSION, BIM_BUILD_DATE);
		render_commandline_message(" %s\n", BIM_COPYRIGHT);
		render_commandline_message("\n");
	} else {
		int found = 0;
		for (struct command_def * c = regular_commands; !found && regular_commands && c->name; ++c) {
			if (!strcmp(c->name, argv[1])) {
				render_commandline_message(""); /* To clear command line */
				render_commandline_message("Help description for `%s`:\n", c->name);
				render_commandline_message("  %s\n", c->description);
				found = 1;
				break;
			}
		}
		for (struct command_def * c = prefix_commands; !found && prefix_commands && c->name; ++c) {
			if (!strcmp(c->name, argv[1])) {
				render_commandline_message(""); /* To clear command line */
				render_commandline_message("Help description for `%s`:\n", c->name);
				render_commandline_message("  %s\n", c->description);
				found = 1;
				break;
			}
		}
		if (!found) {
			render_error("Unknown command: %s", argv[1]);
			return 1;
		}
	}
	/* Redrawing the tabbar makes it look like we just shifted the whole view up */
	redraw_tabbar();
	redraw_commandline();
	/* Wait for a character so we can redraw the screen before continuing */
	pause_for_key();
	return 0;
}

BIM_COMMAND(version,"version","Show version information.") {
	render_status_message("Bim %s%s", BIM_VERSION, BIM_BUILD_DATE);
	return 0;
}

BIM_COMMAND(theme,"theme","Set color theme") {
	if (argc < 2) {
		render_status_message("theme=%s", current_theme);
	} else {
		for (struct theme_def * d = themes; themes && d->name; ++d) {
			if (!strcmp(argv[1], d->name)) {
				ptrdiff_t before = krk_currentThread.stackTop - krk_currentThread.stack;
				krk_push(OBJECT_VAL(d->callable));
				KrkValue result = krk_callStack(0);
				krk_currentThread.stackTop = krk_currentThread.stack + before;
				if (IS_NONE(result) && (krk_currentThread.flags & KRK_THREAD_HAS_EXCEPTION)) {
					render_error("Exception occurred in theme: %s", AS_INSTANCE(krk_currentThread.currentException)->_class->name->chars);
					krk_dumpTraceback();
					int key = 0;
					while ((key = bim_getkey(DEFAULT_KEY_WAIT)) == KEY_TIMEOUT);
				}
				redraw_all();
				return 0;
			}
		}
	}
	return 0;
}

BIM_ALIAS("colorscheme",colorscheme,theme)

BIM_COMMAND(splitpercent,"splitpercent","Display or change view split") {
	if (argc < 2) {
		render_status_message("splitpercent=%d", global_config.split_percent);
		return 0;
	} else {
		global_config.split_percent = atoi(argv[1]);
		if (left_buffer) {
			update_split_size();
			redraw_all();
		}
	}
	return 0;
}

BIM_COMMAND(split,"split","Split the current view.") {
	buffer_t * original = env;
	if (argc > 1) {
		int is_not_number = 0;
		for (char * c = argv[1]; *c; ++c) is_not_number |= !isdigit(*c);
		if (is_not_number) {
			/* Open a file for the new split */
			open_file(argv[1]);
			right_buffer = buffers[buffers_len-1];
		} else {
			/* Use an existing buffer for the new split */
			int other = atoi(argv[1]);
			if (other >= buffers_len || other < 0) {
				render_error("Invalid buffer number: %d", other);
				return 1;
			}
			right_buffer = buffers[other];
		}
	} else {
		/* Use the current buffer for the new split */
		right_buffer = original;
	}
	left_buffer = original;
	update_split_size();
	redraw_all();
	return 0;
}

BIM_COMMAND(unsplit,"unsplit","Show only one buffer on screen") {
	unsplit();
	return 0;
}

BIM_COMMAND(horizontalscrolling,"horizontalscrolling","Set the horizontal scrolling mode") {
	if (argc < 2) {
		render_status_message("horizontalscrolling=%d", global_config.horizontal_shift_scrolling);
		return 0;
	} else {
		global_config.horizontal_shift_scrolling = !!atoi(argv[1]);
		redraw_all();
	}
	return 0;
}

BIM_COMMAND(syntax,"syntax","Show or set the active syntax highlighter") {
	if (argc < 2) {
		render_status_message("syntax=%s", env->syntax ? env->syntax->name : "none");
	} else {
		set_syntax_by_name(argv[1]);
	}
	return 0;
}

BIM_COMMAND(recalc,"recalc","Recalculate syntax for the entire file.") {
	schedule_complete_recalc();
	redraw_all();
	return 0;
}

BIM_COMMAND(tabstop,"tabstop","Show or set the tabstop (width of an indentation unit)") {
	if (argc < 2) {
		render_status_message("tabstop=%d", env->tabstop);
	} else {
		int t = atoi(argv[1]);
		if (t > 0 && t < 12) {
			env->tabstop = t;
			for (int i = 0; i < env->line_count; ++i) {
				recalculate_tabs(env->lines[i]);
			}
			redraw_all();
		} else {
			render_error("Invalid tabstop: %s", argv[1]);
		}
	}
	return 0;
}

BIM_COMMAND(spaces,"spaces","Use spaces for indentation") {
	env->tabs = 0;
	if (argc > 1) {
		bim_command_tabstop("tabstop", argc, argv);
	}
	redraw_statusbar();
	return 0;
}

BIM_COMMAND(tabs,"tabs","Use tabs for indentation") {
	env->tabs = 1;
	if (argc > 1) {
		bim_command_tabstop("tabstop", argc, argv);
	}
	redraw_statusbar();
	return 0;
}

BIM_COMMAND(clearyear,"clearyank","Clear the yank buffer") {
	if (global_config.yanks) {
		for (unsigned int i = 0; i < global_config.yank_count; ++i) {
			free(global_config.yanks[i]);
		}
		free(global_config.yanks);
		global_config.yanks = NULL;
		global_config.yank_count = 0;
		redraw_statusbar();
	}
	return 0;
}

BIM_COMMAND(padding,"padding","Show or set cursor padding when scrolling vertically") {
	if (argc < 2) {
		render_status_message("padding=%d", global_config.cursor_padding);
	} else {
		global_config.cursor_padding = atoi(argv[1]);
		if (env) {
			place_cursor_actual();
		}
	}
	return 0;
}

BIM_COMMAND(smartcase,"smartcase","Show or set the status of the smartcase search option") {
	if (argc < 2) {
		render_status_message("smartcase=%d", global_config.smart_case);
	} else {
		global_config.smart_case = atoi(argv[1]);
		if (env) place_cursor_actual();
	}
	return 0;
}

BIM_COMMAND(hlparen,"hlparen","Show or set the configuration option to highlight matching braces") {
	if (argc < 2) {
		render_status_message("hlparen=%d", global_config.highlight_parens);
	} else {
		global_config.highlight_parens = atoi(argv[1]);
		if (env) {
			for (int i = 0; i < env->line_count; ++i) {
				for (int j = 0; j < env->lines[i]->actual; ++j) {
					env->lines[i]->text[j].flags &= (~FLAG_SELECT);
				}
			}
			redraw_text();
			place_cursor_actual();
		}
	}
	return 0;
}

BIM_COMMAND(hlcurrent,"hlcurrent","Show or set the configuration option to highlight the current line") {
	if (argc < 2) {
		render_status_message("hlcurrent=%d", global_config.highlight_current_line);
	} else {
		global_config.highlight_current_line = atoi(argv[1]);
		if (env) {
			if (!global_config.highlight_current_line) {
				for (int i = 0; i < env->line_count; ++i) {
					env->lines[i]->is_current = 0;
				}
			}
			redraw_text();
			place_cursor_actual();
		}
	}
	return 0;
}

BIM_COMMAND(crnl,"crnl","Show or set the line ending mode") {
	if (argc < 2) {
		render_status_message("crnl=%d", env->crnl);
	} else {
		env->crnl = !!atoi(argv[1]);
		redraw_statusbar();
	}
	return 0;
}

BIM_COMMAND(global_numbers,"global.numbers","Set whether numbers are displayed by default") {
	if (argc < 2) {
		render_status_message("global.numbers=%d", global_config.numbers);
	} else {
		global_config.numbers = !!atoi(argv[1]);
		redraw_all();
	}
	return 0;
}

BIM_COMMAND(global_statusbar,"global.statusbar","Show or set whether to display the statusbar") {
	if (argc < 2) {
		render_status_message("global.statusbar=%d",!global_config.hide_statusbar);
	} else {
		global_config.hide_statusbar = !atoi(argv[1]);
		global_config.bottom_size = global_config.hide_statusbar ? 1 : 2;
		redraw_all();
	}
	return 0;
}

BIM_COMMAND(global_search_wraps,"wrapsearch","Enable search wrapping around from top or bottom") {
	if (argc < 2) {
		render_status_message("wrapsearch=%d",global_config.search_wraps);
	} else {
		global_config.search_wraps = !!atoi(argv[1]);
	}
	return 0;
}

BIM_COMMAND(smartcomplete,"smartcomplete","Enable autocompletion while typing") {
	if (argc < 2) {
		render_status_message("smartcomplete=%d",global_config.smart_complete);
	} else {
		global_config.smart_complete = !!atoi(argv[1]);
	}
	return 0;
}

BIM_COMMAND(global_autohide_tabs,"global.autohidetabs","Whether to show the tab bar when there is only one tab") {
	if (argc < 2) {
		render_status_message("global.autohidetabs=%d", global_config.autohide_tabs);
	} else {
		global_config.autohide_tabs = !!atoi(argv[1]);
		global_config.tabs_visible = (!global_config.autohide_tabs) || (buffers_len > 1);
		redraw_all();
	}
	return 0;
}

BIM_COMMAND(numbers,"numbers","Show or set the display of line numbers") {
	if (argc < 2) {
		render_status_message("numbers=%d", env->numbers);
	} else {
		env->numbers = !!atoi(argv[1]);
		redraw_all();
	}
	return 0;
}

BIM_COMMAND(relativenumbers,"relativenumbers","Show or set the display of relative line numbers") {
	if (argc < 2) {
		render_status_message("relativenumber=%d", global_config.relative_lines);
	} else {
		global_config.relative_lines = atoi(argv[1]);
		if (env) {
			if (!global_config.relative_lines) {
				for (int i = 0; i < env->line_count; ++i) {
					env->lines[i]->is_current = 0;
				}
			}
			redraw_text();
			place_cursor_actual();
		}
	}
	return 0;
}

BIM_COMMAND(buffers,"buffers","Show the open buffers") {
	for (int i = 0; i < buffers_len; ++i) {
		render_commandline_message("%d: %s\n", i, buffers[i]->file_name ? buffers[i]->file_name : "(no name)");
	}
	redraw_tabbar();
	redraw_commandline();
	pause_for_key();
	return 0;
}

BIM_COMMAND(keyname,"keyname","Press and key and get its name.") {
	int c;
	render_commandline_message("(press a key)");
	while ((c = bim_getkey(DEFAULT_KEY_WAIT)) == KEY_TIMEOUT);
	render_commandline_message("%d = %s", c, name_from_key(c));
	return 0;
}

int isSubstitutionSymbol(int c) {
	if (c >= '!' && c <= '/') return 1;
	if (c >= ':' && c <= '@') return 1;
	if (c >= '[' && c <= '`') return 1;
	if (c >= '{' && c <= '~') return 1;
	return 0;
}

int alldigits(const char * c) {
	while (*c) {
		if (!isdigit(*c)) return 0;
		c++;
	}
	return 1;
}

/**
 * Process a user command.
 */
int process_krk_command(const char * cmd, KrkValue * outVal);
int process_command(char * cmd) {
	if (cmd[0] == '-' && alldigits(&cmd[1])) {
		goto_line(env->line_no-atoi(&cmd[1]));
		return 0;
	} else if (cmd[0] == '+' && alldigits(&cmd[1])) {
		goto_line(env->line_no+atoi(&cmd[1]));
		return 0;
	} else if (alldigits(cmd)) {
		goto_line(atoi(cmd));
		return 0;
	} else if (cmd[0] == '!') {
		return _prefix_command_run_script(cmd);
	} else if (cmd[0] == 's' && isSubstitutionSymbol(cmd[1])) {
		return bim_command_repsome(cmd, 0, NULL);
	} else if (cmd[0] == '%' && cmd[1] == 's') {
		return bim_command_repall(cmd, 0, NULL);
	}

	/* See if it's a bim command in the classic format */
	{
		char * argv[3] = {NULL, NULL, NULL};
		int argc = !!(*cmd);
		char cmd_name[512] = {0};
		for (char * c = (char*)cmd; *c; ++c) {
			if (c-cmd == 511) break;
			if (*c == ' ') {
				cmd_name[c-cmd] = '\0';
				while (*c == ' ') c++;
				argv[1] = c;
				if (*argv[1]) argc++;
				break;
			}
			cmd_name[c-cmd] = *c;
		}

		argv[0] = cmd_name;
		argv[argc] = NULL;
		for (struct command_def * c = regular_commands; regular_commands && c->name; ++c) {
			if (!strcmp(argv[0], c->name)) {
				krk_resetStack();
				return c->command((char*)cmd, argc, argv);
			}
		}
	}

	int retval = process_krk_command(cmd, NULL);

	return retval;
}

struct Candidate {
	char * text;
	int type;
};

/**
 * Wrap strcmp for use with qsort.
 */
int compare_candidate(const void * a, const void * b) {
	const struct Candidate *_a = a;
	const struct Candidate *_b = b;
	return strcmp(_a->text, _b->text);
}

/**
 * List of file extensions to ignore when tab completing.
 */
const char * tab_complete_ignore[] = {".o",".lo",NULL};

/**
 * Wrapper around krk_valueGetAttribute...
 */
static KrkValue findFromProperty(KrkValue current, KrkToken next) {
	KrkValue member = OBJECT_VAL(krk_copyString(next.start, next.literalWidth));
	krk_push(member);
	KrkValue value = krk_valueGetAttribute_default(current, AS_CSTRING(member), NONE_VAL());
	krk_pop();
	return value;
}

/**
 * Tab completion for command mode.
 */
void command_tab_complete(char * buffer) {
	/* Figure out which argument this is and where it starts */
	int arg = 0;
	char * buf = strdup(buffer);
	char * b = buf;

	int args_count = 0;
	int args_space = 4;
	char ** args = malloc(sizeof(char*)*args_space);
#define add_arg(argument) \
	do { \
		if (args_count == args_space) { \
			args_space *= 2; \
			args = realloc(args, sizeof(char*) * args_space); \
		} \
		args[args_count++] = argument; \
	} while (0)

	int candidate_count= 0;
	int candidate_space = 4;
	struct Candidate * candidates = malloc(sizeof(struct Candidate)*candidate_space);

	/* Accept whitespace before first argument */
	while (*b == ' ') b++;
	char * start = b;
	add_arg(start);
	while (*b && *b != ' ') b++;
	while (*b) {
		while (*b == ' ') {
			*b = '\0';
			b++;
		}
		start = b;
		arg++;
		add_arg(start);
		break;
	}

	/**
	 * Check a possible candidate and add it to the
	 * candidates list, expanding as necessary,
	 * if it matches for the current argument.
	 */
#define add_candidate(candidate,candtype) \
	do { \
		char * _arg = args[arg]; \
		int r = strncmp(_arg, candidate, strlen(_arg)); \
		if (!r) { \
			int skip = 0; \
			for (int i = 0; i < candidate_count; ++i) { \
				if (!strcmp(candidates[i].text,candidate)) { skip = 1; break; } \
			} \
			if (skip) break; \
			if (candidate_count == candidate_space) { \
				candidate_space *= 2; \
				candidates = realloc(candidates,sizeof(struct Candidate) * candidate_space); \
			} \
			candidates[candidate_count].text = strdup(candidate); \
			candidates[candidate_count].type = candtype; \
			candidate_count++; \
		} \
	} while (0)
#define Candidate_Normal 0
#define Candidate_Command 1
#define Candidate_Builtin 2

	int _candidates_are_files = 0;

	if (arg == 0 || (arg == 1 && !strcmp(args[0], "help"))) {
		/* Complete command names */
		for (struct command_def * c = regular_commands; regular_commands && c->name; ++c) {
			add_candidate(c->name,Candidate_Command);
		}
		for (struct command_def * c = prefix_commands; prefix_commands && c->name; ++c) {
			add_candidate(c->name,Candidate_Command);
		}

		goto _try_kuroko;
	}

	if (arg == 1 && !strcmp(args[0], "syntax")) {
		/* Complete syntax options */
		add_candidate("none", Candidate_Builtin);
		for (struct syntax_definition * s = syntaxes; syntaxes && s->name; ++s) {
			add_candidate(s->name, Candidate_Builtin);
		}
		goto _accept_candidate;
	}

	if (arg == 1 && (!strcmp(args[0], "theme") || !strcmp(args[0], "colorscheme"))) {
		/* Complete color theme names */
		for (struct theme_def * s = themes; themes && s->name; ++s) {
			add_candidate(s->name, Candidate_Builtin);
		}
		goto _accept_candidate;
	}

	if (arg == 1 && (!strcmp(args[0], "setcolor"))) {
		for (struct ColorName * c = color_names; c->name; ++c) {
			add_candidate(c->name, Candidate_Builtin);
		}
		goto _accept_candidate;
	}

	if (arg == 1 && (!strcmp(args[0], "action"))) {
		for (struct action_def * a = mappable_actions; a->name; ++a) {
			add_candidate(a->name, Candidate_Builtin);
		}
		goto _accept_candidate;
	}

	if (arg == 1 && (!strcmp(args[0], "mapkey"))) {
		for (int i = 0; args[arg][i]; ++i) {
			if (args[arg][i] == ' ') {
				while (args[arg][i] == ' ') {
					args[arg][i] = '\0';
					i++;
				}
				start = &args[arg][i];
				arg++;
				add_arg(start);
				i = 0;
			}
		}

		if (arg == 1) {
			for (struct mode_names * m = mode_names; m->name; ++m) {
				add_candidate(m->name, Candidate_Builtin);
			}
		} else if (arg == 2) {
			for (unsigned int i = 0;  i < sizeof(KeyNames)/sizeof(KeyNames[0]); ++i) {
				add_candidate(KeyNames[i].name, Candidate_Builtin);
			}
		} else if (arg == 3) {
			for (struct action_def * a = mappable_actions; a->name; ++a) {
				add_candidate(a->name, Candidate_Builtin);
			}
			add_candidate("none", Candidate_Builtin);
		} else if (arg == 4) {
			for (char * c = "racnwmb"; *c; ++c) {
				char tmp[] = {*c,'\0'};
				add_candidate(tmp, Candidate_Builtin);
			}
		}
		goto _accept_candidate;
	}

	if (arg == 1 && (!strcmp(args[0], "e") || !strcmp(args[0], "tabnew") ||
	    !strcmp(args[0],"split") || !strcmp(args[0],"w") || !strcmp(args[0],"runscript") ||
	    !strcmp(args[0],"rundir") || args[0][0] == '!')) {
		/* Complete file paths */

		/* First, find the deepest directory match */
		char * tmp = strdup(args[arg]);
		char * last_slash = strrchr(tmp, '/');
		DIR * dirp;
		if (last_slash) {
			*last_slash = '\0';
			if (last_slash == tmp) {
				/* Started with slash, and it was the only slash */
				dirp = opendir("/");
			} else {
				char * home;
				if (*tmp == '~' && (home = getenv("HOME"))) {
					char * t = malloc(strlen(tmp) + strlen(home) + 4);
					sprintf(t, "%s%s",home,tmp+1);
					dirp = opendir(t);
					free(t);
				} else {
					dirp = opendir(tmp);
				}
			}
		} else {
			/* No directory match, completing from current directory */
			dirp = opendir(".");
			tmp[0] = '\0';
		}

		if (!dirp) {
			/* Directory match doesn't exist, no candidates to populate */
			free(tmp);
			goto done;
		}

		_candidates_are_files = 1;

		struct dirent * ent = readdir(dirp);
		while (ent != NULL) {
			if (ent->d_name[0] != '.' || (last_slash ? (last_slash[1] == '.') : (tmp[0] == '.'))) {
				struct stat statbuf;
				/* Figure out if this file is a directory */
				if (last_slash) {
					char * x;
					char * home;
					if (tmp[0] == '~' && (home = getenv("HOME"))) {
						x = malloc(strlen(tmp) + 1 + strlen(ent->d_name) + 1 + strlen(home) + 1);
						snprintf(x, strlen(tmp) + 1 + strlen(ent->d_name) + 1 + strlen(home) + 1, "%s%s/%s",home,tmp+1,ent->d_name);
					} else {
						x = malloc(strlen(tmp) + 1 + strlen(ent->d_name) + 1);
						snprintf(x, strlen(tmp) + 1 + strlen(ent->d_name) + 1, "%s/%s",tmp,ent->d_name);
					}
					stat(x, &statbuf);
					free(x);
				} else {
					stat(ent->d_name, &statbuf);
				}

				/* Build the complete argument name to tab complete */
				int type = Candidate_Normal;
				char s[1024] = {0};
				if (last_slash == tmp) {
					strcat(s,"/");
				} else if (*tmp) {
					strcat(s,tmp);
					strcat(s,"/");
				}
				strcat(s,ent->d_name);
				/*
				 * If it is a directory, add a / to the end so the next completion
				 * attempt will complete the directory's contents.
				 */
				if (S_ISDIR(statbuf.st_mode)) {
					strcat(s,"/");
					type = Candidate_Command;
				}

				int skip = 0;
				for (const char ** c = tab_complete_ignore; *c; ++c) {
					if (str_ends_with(s, *c)) {
						skip = 1;
						break;
					}
				}
				if (!skip) {
					add_candidate(s, type);
				}
			}
			ent = readdir(dirp);
		}
		closedir(dirp);
		free(tmp);
		goto _accept_candidate;
	}

	/* Hacky port of the kuroko repl completer */
_try_kuroko:
	{
		KrkScanner scanner = krk_initScanner(buffer);
		KrkToken * space = malloc(sizeof(KrkToken) * (strlen(buffer) + 2));
		int count = 0;
		do {
			space[count++] = krk_scanToken(&scanner);
		} while (space[count-1].type != TOKEN_EOF && space[count-1].type != TOKEN_ERROR);

		if (count == 1) {
			goto _cleanup;
		}

		int base = 2;
		int n = base;
		if (space[count-base].type == TOKEN_DOT) {
			/* Dots we need to look back at the previous tokens for */
			n--;
			base--;
		} else if (space[count-base].type >= TOKEN_IDENTIFIER && space[count-base].type <= TOKEN_WITH) {
			/* Something alphanumeric; only for the last element */
		} else {
			/* Some other symbol */
			goto _cleanup;
		}

		while (n < count) {
			if (space[count-n-1].type != TOKEN_DOT) break;
			n++;
			if (n == count) break;
			if (space[count-n-1].type != TOKEN_IDENTIFIER) break;
			n++;
		}

		if (n <= count) {
			/* Now work forwards, starting from the current globals. */
			KrkValue root = OBJECT_VAL(krk_currentThread.module);
			int isGlobal = 1;
			while (n > base) {
				/* And look at the potential fields for instances/classes */
				KrkValue next = findFromProperty(root, space[count-n]);
				if (IS_NONE(next)) {
					/* If we hit None, we found something invalid (or literally hit a None
					 * object, but really the difference is minimal in this case: Nothing
					 * useful to tab complete from here. */
					if (!isGlobal) goto _cleanup;
					/* Does this match a builtin? */
					if (!krk_tableGet_fast(&vm.builtins->fields,
						krk_copyString(space[count-n].start,space[count-n].literalWidth), &next) || IS_NONE(next)) {
						goto _cleanup;
					}
				}
				isGlobal = 0;
				root = next;
				n -= 2; /* To skip every other dot. */
			}

			if (isGlobal && n < count && (space[count-n-1].type == TOKEN_IMPORT || space[count-n-1].type == TOKEN_FROM)) {
				KrkInstance * modules = krk_newInstance(vm.baseClasses->objectClass);
				root = OBJECT_VAL(modules);
				krk_push(root);
				for (size_t i = 0; i < vm.modules.capacity; ++i) {
					KrkTableEntry * entry = &vm.modules.entries[i];
					if (IS_KWARGS(entry->key)) continue;
					krk_attachNamedValue(&modules->fields, AS_CSTRING(entry->key), NONE_VAL());
				}
			}

			/* Now figure out what we're completing - did we already have a partial symbol name? */
			int length = (space[count-base].type == TOKEN_DOT) ? 0 : (space[count-base].length);
			isGlobal = isGlobal && (length != 0);

			/* Take the last symbol name from the chain and get its member list from dir() */
			static char * syn_krk_keywords[] = {
				"and","class","def","else","for","if","in","import","del",
				"let","not","or","return","while","try","except","raise",
				"continue","break","as","from","elif","lambda","with","is",
				"pass","assert","yield","finally","async","await",
				NULL
			};

			KrkInstance * fakeKeywordsObject = NULL;

			for (;;) {
				KrkValue dirList = krk_dirObject(1,(KrkValue[]){root},0);
				krk_push(dirList);
				if (!IS_INSTANCE(dirList)) {
					render_error("Internal error while tab completing.");
					goto _cleanup;
				}

				for (size_t i = 0; i < AS_LIST(dirList)->count; ++i) {
					KrkString * s = AS_STRING(AS_LIST(dirList)->values[i]);
					krk_push(OBJECT_VAL(s));
					KrkToken asToken = {.start = s->chars, .literalWidth = s->length};
					KrkValue thisValue = findFromProperty(root, asToken);
					krk_push(thisValue);
					if (IS_CLOSURE(thisValue) || IS_BOUND_METHOD(thisValue) || IS_NATIVE(thisValue)) {
						size_t allocSize = s->length + 2;
						char * tmp = malloc(allocSize);
						size_t len = snprintf(tmp, allocSize, "%s(", s->chars);
						s = krk_takeString(tmp, len);
						krk_pop();
						krk_push(OBJECT_VAL(s));
					}

					/* If this symbol is shorter than the current submatch, skip it. */
					if (length && (int)s->length < length) continue;
					if (!memcmp(s->chars, space[count-base].start, length)) {
						char * tmp = malloc(strlen(args[arg]) + s->length + 1);
						sprintf(tmp,"%s%s", args[arg], s->chars + length);
						int type = Candidate_Normal;
						if (IS_OBJECT(root) && AS_OBJECT(root) == (KrkObj*)vm.builtins) {
							type = Candidate_Builtin;
						} else if (IS_OBJECT(root) && AS_OBJECT(root) == (KrkObj*)fakeKeywordsObject) {
							type = Candidate_Command;
						}
						add_candidate(tmp, type);
						free(tmp);
					}
				}

				/*
				 * If the object we were scanning was the current module,
				 * then we should also throw the builtins into the ring.
				 */
				if (isGlobal && AS_OBJECT(root) == (KrkObj*)krk_currentThread.module) {
					root = OBJECT_VAL(vm.builtins);
					continue;
				} else if (isGlobal && AS_OBJECT(root) == (KrkObj*)vm.builtins) {
					fakeKeywordsObject = krk_newInstance(vm.baseClasses->objectClass);
					root = OBJECT_VAL(fakeKeywordsObject);
					krk_push(root);
					for (char ** keyword = syn_krk_keywords; *keyword; keyword++) {
						krk_attachNamedValue(&fakeKeywordsObject->fields, *keyword, NONE_VAL());
					}
					continue;
				} else {
					break;
				}
			}
		}

_cleanup:
		free(space);
		krk_resetStack();
	}

_accept_candidate:
	if (candidate_count == 0) {
		redraw_statusbar();
		goto done;
	}

	if (candidate_count == 1) {
		/* Only one completion possibility */
		redraw_statusbar();

		/* Fill out the rest of the command */
		char * cstart = (buffer) + (start - buf);
		for (unsigned int i = 0; i < strlen(candidates[0].text); ++i) {
			*cstart = candidates[0].text[i];
			cstart++;
		}
		*cstart = '\0';
	} else {
		/* Sort candidates */
		qsort(candidates, candidate_count, sizeof(candidates[0]), compare_candidate);
		/* Print candidates in status bar */
		char * tmp = malloc(global_config.term_width+1 + candidate_count * 100);
		memset(tmp, 0, global_config.term_width+1);
		int offset = 0;
		for (int i = 0; i < candidate_count; ++i) {
			char * printed_candidate = candidates[i].text;
			if (_candidates_are_files) {
				for (char * c = printed_candidate; *c; ++c) {
					if (c[0] == '/' && c[1] != '\0') {
						printed_candidate = c+1;
					}
				}
			} else {
				for (char * c = printed_candidate; *c; ++c) {
					if ((c[0] == '.' || c[0] == '(') && c[1] != '\0') {
						printed_candidate = c+1;
					}
				}
			}
			if (offset + 1 + (signed)strlen(printed_candidate) > global_config.term_width - 5) {
				strcat(tmp, "...");
				break;
			}
			if (offset > 0) {
				strcat(tmp, " ");
				offset++;
			}
			const char * colorString = color_string(
				candidates[i].type == Candidate_Normal ? COLOR_STATUS_FG :
					candidates[i].type == Candidate_Command ? COLOR_KEYWORD :
						candidates[i].type == Candidate_Builtin ? COLOR_TYPE : COLOR_STATUS_FG,
				COLOR_STATUS_BG);
			/* Does not affect offset */
			strcat(tmp, colorString);
			strcat(tmp, printed_candidate);
			offset += strlen(printed_candidate);
		}
		render_status_message("%s", tmp);
		free(tmp);

		/* Complete to longest common substring */
		char * cstart = (buffer) + (start - buf);
		for (int i = 0; i < 1023 /* max length of command */; i++) {
			for (int j = 1; j < candidate_count; ++j) {
				if (candidates[0].text[i] != candidates[j].text[i]) goto _reject;
			}
			*cstart = candidates[0].text[i];
			cstart++;
		}
		/* End of longest common substring */
_reject:
		*cstart = '\0';
		/* Just make sure the buffer doesn't end on an incomplete multibyte sequence */
		if (start > buf) { /* Start point needs to be something other than first byte */
			char * tmp = cstart - 1;
			if ((*tmp & 0xC0) == 0x80) {
				/* Count back until we find the start byte and make sure we have the right number */
				int count = 1;
				int x = 0;
				while (tmp >= start) {
					x++;
					tmp--;
					if ((*tmp & 0xC0) == 0x80) {
						count++;
					} else if ((*tmp & 0xC0) == 0xC0) {
						/* How many should we have? */
						int i = 1;
						int j = *tmp;
						while (j & 0x20) {
							i += 1;
							j <<= 1;
						}
						if (count != i) {
							*tmp = '\0';
							break;
						}
						break;
					} else {
						/* This isn't right, we had a bad multibyte sequence? Or someone is typing Latin-1. */
						tmp++;
						*tmp = '\0';
						break;
					}
				}
			} else if ((*tmp & 0xC0) == 0xC0) {
				*tmp = '\0';
			}
		}
	}

	/* Free candidates */
	for (int i = 0; i < candidate_count; ++i) {
		free(candidates[i].text);
	}

done:
	free(candidates);
	free(buf);
}

/**
 * Macros for use in command mode.
 */
#define _syn_command() do { env->syntax = global_config.command_syn; } while (0)
#define _syn_restore() do { env->syntax = global_config.command_syn_back; } while (0)

/**
 * Draw the command buffer and any prefix.
 */
void render_command_input_buffer(void) {

	if (!global_config.command_buffer) return;

	/* Place the cursor at the bottom of the screen */
	place_cursor(1, global_config.term_height);
	paint_line(COLOR_BG);
	set_colors(COLOR_ALT_FG, COLOR_BG);

	/* If there's a mode name to render, draw it first */
	int _left_gutter = 0;
	if (env->mode == MODE_LINE_SELECTION) {
		_left_gutter = printf("(LINE %d:%d)",
			(env->start_line < env->line_no) ? env->start_line : env->line_no,
			(env->start_line < env->line_no) ? env->line_no : env->start_line);
	} else if (env->mode == MODE_COL_SELECTION) {
		_left_gutter = printf("(COL %d:%d %d)",
			(env->start_line < env->line_no) ? env->start_line : env->line_no,
			(env->start_line < env->line_no) ? env->line_no : env->start_line,
			(env->sel_col));
	} else if (env->mode == MODE_CHAR_SELECTION) {
		_left_gutter = printf("(CHAR)");
	}

	/* Figure out the cursor position and adjust the offset if necessary */
	int x = 2 + _left_gutter - global_config.command_offset;
	for (int i = 0; i < global_config.command_col_no - 1; ++i) {
		char_t * c = &global_config.command_buffer->text[i];
		x += c->display_width;
	}
	if (x > global_config.term_width - 1) {
		int diff = x - (global_config.term_width - 1);
		global_config.command_offset += diff;
		x -= diff;
	}
	if (x < 2 + _left_gutter) {
		int diff = (2 + _left_gutter) - x;
		global_config.command_offset -= diff;
		x += diff;
	}

	/* If the input buffer is horizontally shifted because it's too long, indicate that. */
	if (global_config.command_offset) {
		set_colors(COLOR_ALT_FG, COLOR_ALT_BG);
		printf("<");
	} else {
		/* Otherwise indicate buffer mode (search / ?, or command :) */
		set_colors(COLOR_FG, COLOR_BG);
		if (global_config.overlay_mode == OVERLAY_MODE_SEARCH) {
			printf(global_config.search_direction == 0 ? "?" : "/");
		} else if (global_config.overlay_mode == OVERLAY_MODE_FILESEARCH) {
			printf("_");
		} else {
			printf(":");
		}
	}

	/* Render the buffer */
	render_line(global_config.command_buffer, global_config.term_width-1-_left_gutter, global_config.command_offset, -1);

	/* Place and display the cursor */
	place_cursor(x, global_config.term_height);
	show_cursor();
}

BIM_ACTION(command_discard, 0,
	"Discard the input buffer and cancel command or search."
,void) {
	free(global_config.command_buffer);
	global_config.command_buffer = NULL;
	if (global_config.overlay_mode == OVERLAY_MODE_SEARCH) {
		env->line_no = global_config.prev_line;
		env->col_no  = global_config.prev_col;
		/* Unhighlight search matches */
		for (int i = 0; i < env->line_count; ++i) {
			for (int j = 0; j < env->lines[i]->actual; ++j) {
				env->lines[i]->text[j].flags &= (~FLAG_SEARCH);
			}
			rehighlight_search(env->lines[i]);
		}
	}
	global_config.overlay_mode = OVERLAY_MODE_NONE;
	redraw_all();
	/* TODO exit some other modes? */
}

BIM_ACTION(enter_command, 0,
	"Enter command input mode."
,void) {
	global_config.overlay_mode = OVERLAY_MODE_COMMAND;

	global_config.command_offset = 0;
	global_config.command_col_no = 1;

	if (global_config.command_buffer) {
		free(global_config.command_buffer);
	}

	global_config.command_buffer = calloc(sizeof(line_t)+sizeof(char_t)*32,1);
	global_config.command_buffer->available = 32;

	global_config.command_syn_back = env->syntax;
	global_config.command_syn = find_syntax_calculator("bimcmd");

	global_config.history_point = -1;

	render_command_input_buffer();
}

static char * command_buffer_to_utf8(void) {
	size_t size = 0;
	for (int i = 0; i < global_config.command_buffer->actual; ++i) {
		char tmp[8] = {0};
		size += to_eight(global_config.command_buffer->text[i].codepoint, tmp);
	}
	char * tmp = malloc(size + 8); /* for overflow from to_eight */
	char * t = tmp;
	for (int i = 0; i < global_config.command_buffer->actual; ++i) {
		t += to_eight(global_config.command_buffer->text[i].codepoint, t);
	}
	*t = '\0';
	return tmp;
}

BIM_ACTION(command_accept, 0,
	"Accept the command input and run the requested command."
,void) {
	/* Convert command buffer to UTF-8 char-array string */
	char * tmp = command_buffer_to_utf8();

	/* Free the original editing buffer */
	free(global_config.command_buffer);
	global_config.command_buffer = NULL;

	/* Run the converted command */
	global_config.break_from_selection = 0;
	insert_command_history(command_history, tmp);
	global_config.overlay_mode = OVERLAY_MODE_NONE;
	process_command(tmp);
	free(tmp);

	if (!global_config.break_from_selection && env->mode != MODE_DIRECTORY_BROWSE) {
		if (env->mode == MODE_LINE_SELECTION || env->mode == MODE_CHAR_SELECTION || env->mode == MODE_COL_SELECTION) {
			recalculate_selected_lines();
		}
		env->mode = MODE_NORMAL;
	}

	/* Leave command mode */
}

BIM_ACTION(command_word_delete, 0,
	"Delete the previous word from the input buffer."
,void) {
	_syn_command();
	while (global_config.command_col_no > 1 &&
	       (global_config.command_buffer->text[global_config.command_col_no-2].codepoint == ' ' ||
	        global_config.command_buffer->text[global_config.command_col_no-2].codepoint == '/')) {
		line_delete(global_config.command_buffer, global_config.command_col_no - 1, -1);
		global_config.command_col_no--;
	}
	while (global_config.command_col_no > 1 &&
	       global_config.command_buffer->text[global_config.command_col_no-2].codepoint != ' ' &&
	       global_config.command_buffer->text[global_config.command_col_no-2].codepoint != '/') {
		line_delete(global_config.command_buffer, global_config.command_col_no - 1, -1);
		global_config.command_col_no--;
	}
	_syn_restore();
}

BIM_ACTION(command_tab_complete_buffer, 0,
	"Complete command names and arguments in the input buffer."
,void) {
	/* command_tab_complete should probably just be adjusted to deal with the buffer... */
	char * tmp = calloc(1024,1);
	char * t = tmp;
	for (int i = 0; i < global_config.command_col_no-1; ++i) {
		t += to_eight(global_config.command_buffer->text[i].codepoint, t);
	}
	*t = '\0';
	_syn_command();
	while (global_config.command_col_no > 1) {
		line_delete(global_config.command_buffer, global_config.command_col_no - 1, -1);
		global_config.command_col_no--;
	}
	_syn_restore();
	command_tab_complete(tmp);
	_syn_command();
	uint32_t state = 0, c= 0;
	t = tmp;
	while (*t) {
		if (!decode(&state, &c, *t)) {
			char_t _c = {codepoint_width(c), 0, c};
			global_config.command_buffer = line_insert(global_config.command_buffer, _c, global_config.command_col_no - 1, -1);
			global_config.command_col_no++;
		}
		t++;
	}
	_syn_restore();
	free(tmp);
}

BIM_ACTION(command_backspace, 0,
	"Erase the character before the cursor in the input buffer."
,void) {
	if (global_config.command_col_no <= 1) {
		if (global_config.command_buffer->actual == 0) {
			command_discard();
		}
		return;
	}
	_syn_command();
	line_delete(global_config.command_buffer, global_config.command_col_no - 1, -1);
	_syn_restore();
	global_config.command_col_no--;
	global_config.command_offset = 0;
}

static void _restore_history(unsigned char **which_history, int point) {
	unsigned char * t = which_history[point];
	global_config.command_col_no = 1;
	global_config.command_buffer->actual = 0;
	_syn_command();
	uint32_t state = 0;
	uint32_t c = 0;
	while (*t) {
		if (!decode(&state, &c, *t)) {
			char_t _c = {codepoint_width(c), 0, c};
			global_config.command_buffer = line_insert(global_config.command_buffer, _c, global_config.command_col_no - 1, -1);
			global_config.command_col_no++;
		} else if (state == UTF8_REJECT) state = 0;
		t++;
	}
	_syn_restore();
}

static void _scroll_history(int direction, unsigned char **which_history, int * which_point) {
	if (direction == -1) {
		if (which_history[*which_point+1]) {
			_restore_history(which_history, *which_point+1);
			(*which_point)++;
		}
	} else {
		if (*which_point > 0) {
			(*which_point)--;
			_restore_history(which_history, *which_point);
		} else {
			*which_point = -1;
			global_config.command_col_no = 1;
			global_config.command_buffer->actual = 0;
		}
	}
}

BIM_ACTION(command_scroll_history, ARG_IS_CUSTOM,
	"Scroll through command input history."
,int direction) {
	_scroll_history(direction, command_history, &global_config.history_point);
}

BIM_ACTION(command_scroll_search_history, ARG_IS_CUSTOM,
	"Scroll through search input history."
,int direction) {
	_scroll_history(direction, search_history, &global_config.search_point);
}

BIM_ACTION(command_word_left, 0,
	"Move to the start of the previous word in the input buffer."
,void) {
	if (global_config.command_col_no > 1) {
		do {
			global_config.command_col_no--;
		} while (isspace(global_config.command_buffer->text[global_config.command_col_no-1].codepoint) && global_config.command_col_no > 1);
		if (global_config.command_col_no == 1) return;
		do {
			global_config.command_col_no--;
		} while (!isspace(global_config.command_buffer->text[global_config.command_col_no-1].codepoint) && global_config.command_col_no > 1);
		if (isspace(global_config.command_buffer->text[global_config.command_col_no-1].codepoint) && global_config.command_col_no < global_config.command_buffer->actual) global_config.command_col_no++;
	}
}

BIM_ACTION(command_word_right, 0,
	"Move to the start of the next word in the input buffer."
,void) {
	if (global_config.command_col_no < global_config.command_buffer->actual) {
		do {
			global_config.command_col_no++;
			if (global_config.command_col_no > global_config.command_buffer->actual) { global_config.command_col_no = global_config.command_buffer->actual+1; break; }
		} while (!isspace(global_config.command_buffer->text[global_config.command_col_no-1].codepoint) && global_config.command_col_no <= global_config.command_buffer->actual);
		do {
			global_config.command_col_no++;
			if (global_config.command_col_no > global_config.command_buffer->actual) { global_config.command_col_no = global_config.command_buffer->actual+1; break; }
		} while (isspace(global_config.command_buffer->text[global_config.command_col_no-1].codepoint) && global_config.command_col_no <= global_config.command_buffer->actual);
		if (global_config.command_col_no > global_config.command_buffer->actual) { global_config.command_col_no = global_config.command_buffer->actual+1; }
	}
}

BIM_ACTION(command_cursor_left, 0,
	"Move the cursor one character left in the input buffer."
,void) {
	if (global_config.command_col_no > 1) global_config.command_col_no--;
}

BIM_ACTION(command_cursor_right, 0,
	"Move the cursor one character right in the input buffer."
,void) {
	if (global_config.command_col_no < global_config.command_buffer->actual+1) global_config.command_col_no++;
}

BIM_ACTION(command_cursor_home, 0,
	"Move the cursor to the start of the input buffer."
,void) {
	global_config.command_col_no = 1;
}

BIM_ACTION(command_cursor_end, 0,
	"Move the cursor to the end of the input buffer."
,void) {
	global_config.command_col_no = global_config.command_buffer->actual + 1;
}

BIM_ACTION(eat_mouse, 0,
	"(temporary) Read, but ignore mouse input."
,void) {
	bim_getch();
	bim_getch();
	bim_getch();
}

BIM_ACTION(command_insert_char, ARG_IS_INPUT,
	"Insert one character into the input buffer."
,int c) {
	char_t _c = {codepoint_width(c), 0, c};
	_syn_command();
	global_config.command_buffer = line_insert(global_config.command_buffer, _c, global_config.command_col_no - 1, -1);
	_syn_restore();
	global_config.command_col_no++;
}

/**
 * Determine whether a string should be searched
 * case-sensitive or not based on whether it contains
 * any upper-case letters.
 */
int smart_case(uint32_t * str) {
	if (!global_config.smart_case) return 0;

	for (uint32_t * s = str; *s; ++s) {
		if (tolower(*s) != (int)*s) {
			return 0;
		}
	}
	return 1;
}

/**
 * Search forward from the given cursor position
 * to find a basic search match.
 *
 * This could be more complicated...
 */
void find_match(int from_line, int from_col, int * out_line, int * out_col, uint32_t * str, int * matchlen) {
	int col = from_col;

	int ignorecase = smart_case(str);

	for (int i = from_line; i <= env->line_count; ++i) {
		line_t * line = env->lines[i - 1];

		int j = col - 1;
		while (j < line->actual + 1) {
			if (subsearch_matches(line, j, str, ignorecase, matchlen)) {
				*out_line = i;
				*out_col = j + 1;
				return;
			}
			j++;
		}
		col = 0;
	}
}

/**
 * Search backwards for matching string.
 */
void find_match_backwards(int from_line, int from_col, int * out_line, int * out_col, uint32_t * str) {
	int col = from_col;

	int ignorecase = smart_case(str);

	for (int i = from_line; i >= 1; --i) {
		line_t * line = env->lines[i-1];

		int j = col - 1;
		while (j > -1) {
			if (subsearch_matches(line, j, str, ignorecase, NULL)) {
				*out_line = i;
				*out_col = j + 1;
				return;
			}
			j--;
		}
		col = (i > 1) ? (env->lines[i-2]->actual + 1) : -1;
	}
}

/**
 * Re-mark search matches while editing text.
 * This gets called after recalculate_syntax, so it works as we're typing or
 * whenever syntax calculation would redraw other lines.
 * XXX There's a bunch of code duplication between the (now three) search match functions.
 *     and search should be improved to support regex anyway, so this needs to be fixed.
 */
void rehighlight_search(line_t * line) {
	if (!global_config.search) return;
	int j = 0;
	while (j < line->actual) {
		line->text[j].flags &= ~(FLAG_SEARCH);
		j++;
	}
	int ignorecase = smart_case(global_config.search);
	j = 0;
	while (j < line->actual) {
		int matchlen = 0;
		if (subsearch_matches(line, j, global_config.search, ignorecase, &matchlen)) {
			for (int i = j; matchlen > 0; ++i, matchlen--) {
				line->text[i].flags |= FLAG_SEARCH;
			}
		}
		j++;
	}
}

/**
 * Draw the matched search result.
 */
void draw_search_match(uint32_t * buffer, int redraw_buffer) {
	for (int i = 0; i < env->line_count; ++i) {
		for (int j = 0; j < env->lines[i]->actual; ++j) {
			env->lines[i]->text[j].flags &= (~FLAG_SEARCH);
		}
	}
	int my_index = 0, match_count = 0;
	int line = -1, col = -1, _line = 1, _col = 1;
	do {
		int matchlen;
		find_match(_line, _col, &line, &col, buffer, &matchlen);
		if (line != -1) {
			line_t * l = env->lines[line-1];
			for (int i = col; matchlen > 0; ++i, --matchlen) {
				l->text[i-1].flags |= FLAG_SEARCH;
			}
			match_count += 1;
			if (col == env->col_no && line == env->line_no) my_index = match_count;
		}
		_line = line;
		_col  = col+1;
		line  = -1;
		col   = -1;
	} while (_line != -1);
	redraw_text();
	place_cursor_actual();
	redraw_statusbar();
	redraw_commandline();
	set_fg_color(COLOR_ALT_FG);
	printf("[%d/%d] ", my_index, match_count);
	set_fg_color(COLOR_KEYWORD);
	printf(redraw_buffer == 1 ? "/" : "?");
	set_fg_color(COLOR_FG);
	uint32_t * c = buffer;
	while (*c) {
		char tmp[7] = {0}; /* Max six bytes, use 7 to ensure last is always nil */
		to_eight(*c, tmp);
		printf("%s", tmp);
		c++;
	}
}

BIM_ACTION(start_file_search, 0, "Search for open files and switch tabs quickly.",void) {
	global_config.overlay_mode = OVERLAY_MODE_FILESEARCH;

	global_config.command_offset = 0;
	global_config.command_col_no = 1;

	if (global_config.command_buffer) {
		free(global_config.command_buffer);
	}

	global_config.command_buffer = calloc(sizeof(line_t)+sizeof(char_t)*32,1);
	global_config.command_buffer->available = 32;

	global_config.command_syn_back = env->syntax;
	global_config.command_syn = NULL; /* uh, dunno for now */

	render_command_input_buffer();
}

BIM_ACTION(file_search_accept, 0, "Open the requested tab",void) {
	if (!global_config.command_buffer->actual) {
		goto _finish;
	}

	/* See if the command buffer matches any open buffers */
	buffer_t * match = NULL;
	for (int i = global_config.tab_offset; i < buffers_len; i++) {
		buffer_t * _env = buffers[i];

		if (global_config.overlay_mode == OVERLAY_MODE_FILESEARCH) {
			if (global_config.command_buffer->actual) {
				char * f = _env->file_name ? file_basename(_env->file_name) : "";
				/* TODO: Support unicode input here; needs conversion */
				int i = 0;
				for (; i < global_config.command_buffer->actual &&
				      f[i] == global_config.command_buffer->text[i].codepoint; ++i);
				if (global_config.command_buffer->actual == i) {
					match = _env;
					break;
				}
			}
		}
	}

	if (match) {
		env = match;
		if (left_buffer && (left_buffer != env && right_buffer != env)) unsplit();
	}

_finish:
	/* Free the original editing buffer */
	free(global_config.command_buffer);
	global_config.command_buffer = NULL;

	/* Leave command mode */
	global_config.overlay_mode = OVERLAY_MODE_NONE;

	redraw_all();
}

BIM_ACTION(enter_search, ARG_IS_CUSTOM,
	"Enter search mode."
,int direction) {
	global_config.overlay_mode = OVERLAY_MODE_SEARCH;

	global_config.command_offset = 0;
	global_config.command_col_no = 1;

	global_config.prev_line = env->line_no;
	global_config.prev_col  = env->col_no;
	global_config.prev_coffset = env->coffset;
	global_config.prev_offset = env->offset;
	global_config.search_direction = direction;

	if (global_config.command_buffer) {
		free(global_config.command_buffer);
	}

	global_config.command_buffer = calloc(sizeof(line_t)+sizeof(char_t)*32,1);
	global_config.command_buffer->available = 32;

	global_config.command_syn_back = env->syntax;
	global_config.command_syn = NULL; /* Disable syntax highlighting in search; maybe use buffer's mode instead? */

	render_command_input_buffer();
}

BIM_ACTION(search_accept, 0,
	"Accept the search term and return to the previous mode."
,void) {
	/* Store the accepted search */
	if (!global_config.command_buffer->actual) {
		if (global_config.search) {
			search_next();
		}
		goto _finish;
	}
	if (global_config.search) {
		free(global_config.search);
	}

	global_config.search = malloc((global_config.command_buffer->actual + 1) * sizeof(uint32_t));
	for (int i = 0; i < global_config.command_buffer->actual; ++i) {
		global_config.search[i] = global_config.command_buffer->text[i].codepoint;
	}
	global_config.search[global_config.command_buffer->actual] = 0;

	char * tmp = command_buffer_to_utf8();
	insert_command_history(search_history, tmp);
	free(tmp);

_finish:
	/* Free the original editing buffer */
	free(global_config.command_buffer);
	global_config.command_buffer = NULL;

	/* Leave command mode */
	global_config.overlay_mode = OVERLAY_MODE_NONE;

	if (global_config.search) draw_search_match(global_config.search, global_config.search_direction);
}

/**
 * Find the next search result, or loop back around if at the end.
 */
BIM_ACTION(search_next, 0,
	"Jump to the next search match."
,void) {
	if (!global_config.search) return;
	if (env->coffset) env->coffset = 0;
	int line = -1, col = -1, wrapped = 0;
	find_match(env->line_no, env->col_no+1, &line, &col, global_config.search, NULL);

	if (line == -1) {
		if (!global_config.search_wraps) return;
		find_match(1,1, &line, &col, global_config.search, NULL);
		if (line == -1) return;
		wrapped = 1;
	}

	env->col_no = col;
	env->line_no = line;
	set_preferred_column();
	draw_search_match(global_config.search, 1);
	if (wrapped) {
		set_fg_color(COLOR_ALT_FG);
		printf(" (search wrapped to top)");
	}
}

/**
 * Find the previous search result, or loop to the end of the file.
 */
BIM_ACTION(search_prev, 0,
	"Jump to the preceding search match."
,void) {
	if (!global_config.search) return;
	if (env->coffset) env->coffset = 0;
	int line = -1, col = -1, wrapped = 0;
	find_match_backwards(env->line_no, env->col_no-1, &line, &col, global_config.search);

	if (line == -1) {
		if (!global_config.search_wraps) return;
		find_match_backwards(env->line_count, env->lines[env->line_count-1]->actual, &line, &col, global_config.search);
		if (line == -1) return;
		wrapped = 1;
	}

	env->col_no = col;
	env->line_no = line;
	set_preferred_column();
	draw_search_match(global_config.search, 0);
	if (wrapped) {
		set_fg_color(COLOR_ALT_FG);
		printf(" (search wrapped to bottom)");
	}
}

/**
 * Find the matching paren for this one.
 *
 * This approach skips having to do its own syntax parsing
 * to deal with, eg., erroneous parens in comments. It does
 * this by finding the matching paren with the same flag
 * value, thus parens in strings will match, parens outside
 * of strings will match, but parens in strings won't
 * match parens outside of strings and so on.
 */
void find_matching_paren(int * out_line, int * out_col, int in_col) {
	if (env->col_no - in_col + 1 > env->lines[env->line_no-1]->actual) {
		return; /* Invalid cursor position */
	}

	/* TODO: vim can find the nearest paren to start searching from, we need to be on one right now */

	int paren_match = 0;
	int direction = 0;
	int start = env->lines[env->line_no-1]->text[env->col_no-in_col].codepoint;
	int flags = env->lines[env->line_no-1]->text[env->col_no-in_col].flags & 0x1F;
	int count = 0;

	/* TODO what about unicode parens? */
	for (int i = 0; paren_pairs[i]; ++i) {
		if (start == paren_pairs[i]) {
			direction = (i % 2 == 0) ? 1 : -1;
			paren_match = paren_pairs[(i % 2 == 0) ? (i+1) : (i-1)];
			break;
		}
	}

	if (!paren_match) return;

	/* Scan for match */
	int line = env->line_no;
	int col  = env->col_no - in_col + 1;

	do {
		while (col > 0 && col < env->lines[line-1]->actual + 1) {
			/* Only match on same syntax */
			if ((env->lines[line-1]->text[col-1].flags & 0x1F) == flags) {
				/* Count up on same direction */
				if (env->lines[line-1]->text[col-1].codepoint == start) count++;
				/* Count down on opposite direction */
				if (env->lines[line-1]->text[col-1].codepoint == paren_match) {
					count--;
					/* When count == 0 we have a match */
					if (count == 0) goto _match_found;
				}
			}
			col += direction;
		}

		line += direction;

		/* Reached first/last line with no match */
		if (line == 0 || line == env->line_count + 1) {
			return;
		}

		/* Reset column to start/end of line, depending on direction */
		if (direction > 0) {
			col = 1;
		} else {
			col = env->lines[line-1]->actual;
		}
	} while (1);

_match_found:
	*out_line = line;
	*out_col  = col;
}

/**
 * Switch to the left split view
 * (Primarily to handle cases where the left and right are the same buffer)
 */
BIM_ACTION(use_left_buffer, 0,
	"Switch to the left split view."
,void) {
	if (left_buffer == right_buffer && env->left != 0) {
		view_right_offset = env->offset;
		env->width = env->left;
		env->left = 0;
		env->offset = view_left_offset;
	}
	env = left_buffer;
	update_title();
}

/**
 * Switch to the right split view
 * (Primarily to handle cases where the left and right are the same buffer)
 */
BIM_ACTION(use_right_buffer, 0,
	"Switch to the right split view."
,void) {
	if (left_buffer == right_buffer && env->left == 0) {
		view_left_offset = env->offset;
		env->left = env->width;
		env->width = global_config.term_width - env->width;
		env->offset = view_right_offset;
	}
	env = right_buffer;
	update_title();
}

void handle_common_mouse(int buttons, int x, int y) {
	if (buttons == 64) {
		/* Scroll up */
		if (global_config.shift_scrolling) {
			env->loading = 1;
			int shifted = 0;
			for (int i = 0; i < global_config.scroll_amount; ++i) {
				if (env->offset > 0) {
					env->offset--;
					if (env->line_no > env->offset + global_config.term_height - global_config.bottom_size - global_config.tabs_visible - global_config.cursor_padding) {
						cursor_up();
					}
					shifted++;
				}
			}
			env->loading = 0;
			if (!shifted) return;
			if (global_config.can_scroll && !left_buffer) {
				if (!global_config.can_insert) {
					shift_down(shifted);
					redraw_tabbar();
				} else {
					insert_lines_at(global_config.tabs_visible ? 2 : 1, shifted);
				}
				for (int i = 0; i < shifted; ++i) {
					redraw_line(env->offset+i);
				}
			} else {
				redraw_tabbar();
				redraw_text();
			}
			redraw_statusbar();
			redraw_commandline();
			place_cursor_actual();
		} else {
			for (int i = 0; i < global_config.scroll_amount; ++i) {
				cursor_up();
			}
		}
		return;
	} else if (buttons == 65) {
		/* Scroll down */
		if (global_config.shift_scrolling) {
			env->loading = 1;
			int shifted = 0;
			for (int i = 0; i < global_config.scroll_amount; ++i) {
				if (env->offset < env->line_count-1) {
					env->offset++;
					int e = (env->offset == 0) ? env->offset : env->offset + global_config.cursor_padding;
					if (env->line_no <= e) {
						cursor_down();
					}
					shifted++;
				}
			}
			env->loading = 0;
			if (!shifted) return;
			if (global_config.can_scroll && !left_buffer) {
				if (!global_config.can_insert) {
					shift_up(shifted);
					redraw_tabbar();
				} else {
					delete_lines_at(global_config.tabs_visible ? 2 : 1, shifted);
				}
				int l = global_config.term_height - global_config.bottom_size - global_config.tabs_visible;
				for (int i = 0; i < shifted; ++i) {
					if (env->offset + l - i < env->line_count + 1) {
						redraw_line(env->offset + l-1-i);
					} else {
						draw_excess_line(l - 1 - i);
					}
				}
			} else {
				redraw_tabbar();
				redraw_text();
			}
			redraw_statusbar();
			redraw_commandline();
			place_cursor_actual();
		} else {
			for (int i = 0; i < global_config.scroll_amount; ++i) {
				cursor_down();
			}
		}
		return;
	} else if (buttons == 3) {
		/* Move cursor to position */

		if (x < 0) return;
		if (y < 0) return;

		if (y == 1 && global_config.tabs_visible) {
			/* Pick from tabs */
			if (env->mode != MODE_NORMAL && env->mode != MODE_INSERT) return; /* Don't let the tab be switched in other modes for now */
			int _x = 0;
			if (global_config.tab_offset) _x = 1;
			if (global_config.tab_offset && _x >= x) {
				global_config.tab_offset--;
				redraw_tabbar();
				return;
			}
			for (int i = global_config.tab_offset; i < buffers_len; i++) {
				buffer_t * _env = buffers[i];
				char tmp[64];
				int size = 0;
				int filled = draw_tab_name(_env, tmp, global_config.term_width - _x, &size);
				_x += size;
				if (_x >= x) {
					if (left_buffer && buffers[i] != left_buffer && buffers[i] != right_buffer) unsplit();
					env = buffers[i];
					redraw_all();
					update_title();
					return;
				}
				if (filled) break;
			}
			if (x > _x && global_config.tab_offset < buffers_len - 1) {
				global_config.tab_offset++;
				redraw_tabbar();
			}
			return;
		}

		if (env->mode == MODE_NORMAL || env->mode == MODE_INSERT) {
			int current_mode = env->mode;
			if (x < env->left && env == right_buffer) {
				use_left_buffer();
			} else if (x > env->width && env == left_buffer) {
				use_right_buffer();
			}
			env->mode = current_mode;
			redraw_all();
		}

		if (env->left) {
			x -= env->left;
		}

		/* Figure out y coordinate */
		int line_no = y + env->offset - global_config.tabs_visible;
		int col_no = -1;

		if (line_no > env->line_count) {
			line_no = env->line_count;
		}

		if (line_no != env->line_no) {
			env->coffset = 0;
		}

		/* Account for the left hand gutter */
		int num_size = num_width() + gutter_width();
		int _x = num_size - (line_no == env->line_no ? env->coffset : 0);

		/* Determine where the cursor is physically */
		for (int i = 0; i < env->lines[line_no-1]->actual; ++i) {
			char_t * c = &env->lines[line_no-1]->text[i];
			_x += c->display_width;
			if (_x > x-1) {
				col_no = i+1;
				break;
			}
		}

		if (col_no == -1 || col_no > env->lines[line_no-1]->actual) {
			col_no = env->lines[line_no-1]->actual;
		}

		env->line_no = line_no;
		env->col_no = col_no;
		set_history_break();
		set_preferred_column();
		redraw_statusbar();
		place_cursor_actual();
	}
	return;
}

/**
 * Handle mouse event
 */
BIM_ACTION(handle_mouse, 0,
	"Process mouse actions."
,void) {
	int buttons = bim_getch() - 32;
	int x = bim_getch() - 32;
	int y = bim_getch() - 32;

	handle_common_mouse(buttons, x, y);
}

BIM_ACTION(eat_mouse_sgr, 0,
	"Receive, but do not process, mouse actions."
,void) {
	do {
		int _c = bim_getch();
		if (_c == -1 || _c == 'm' || _c == 'M') break;
	} while (1);
}

BIM_ACTION(handle_mouse_sgr, 0,
	"Process SGR-style mouse actions."
,void) {
	int values[3] = {0};
	char tmp[512] = {0};
	char * c = tmp;
	int buttons = 0;
	do {
		int _c = bim_getch();
		if (_c == -1) {
			break;
		}
		if (_c == 'm') {
			buttons = 3;
			break;
		} else if (_c == 'M') {
			buttons = 0;
			break;
		}
		*c = _c;
		++c;
	} while (1);
	char * j = tmp;
	char * last = tmp;
	int i = 0;
	while (*j) {
		if (*j == ';') {
			*j = '\0';
			values[i] = atoi(last);
			last = j+1;
			i++;
			if (i == 3) break;
		}
		j++;
	}
	if (last && i < 3) values[i] = atoi(last);
	if (buttons != 3) {
		buttons = values[0];
	}
	int x = values[1];
	int y = values[2];

	handle_common_mouse(buttons, x, y);
}


BIM_ACTION(insert_char, ARG_IS_INPUT | ACTION_IS_RW,
	"Insert one character."
,unsigned int c) {
	if (!c) {
		render_error("Inserted nil byte?");
		return;
	}
	char_t _c;
	_c.codepoint = c;
	_c.flags = 0;
	_c.display_width = codepoint_width(c);
	line_t * line  = env->lines[env->line_no - 1];
	line_t * nline = line_insert(line, _c, env->col_no - 1, env->line_no - 1);
	if (line != nline) {
		env->lines[env->line_no - 1] = nline;
	}
	env->col_no += 1;
	set_preferred_column();
	set_modified();
}

BIM_ACTION(replace_char, ARG_IS_PROMPT | ACTION_IS_RW,
	"Replace a single character."
,unsigned int c) {
	if (env->col_no < 1 || env->col_no > env->lines[env->line_no-1]->actual) return;

	if (c >= KEY_ESCAPE) {
		render_error("Invalid key for replacement");
		return;
	}

	char_t _c;
	_c.codepoint = c;
	_c.flags = 0;
	_c.display_width = codepoint_width(c);

	line_replace(env->lines[env->line_no-1], _c, env->col_no-1, env->line_no-1);

	redraw_line(env->line_no-1);
	set_modified();
}

BIM_ACTION(undo_history, ACTION_IS_RW,
	"Undo history until the last breakpoint."
,void) {
	if (!global_config.history_enabled) return;

	env->loading = 1;
	history_t * e = env->history;

	if (e->type == HISTORY_SENTINEL) {
		env->loading = 0;
		render_commandline_message("Already at oldest change");
		return;
	}

	int count_chars = 0;
	int count_lines = 0;

	do {
		if (e->type == HISTORY_SENTINEL) break;

		switch (e->type) {
			case HISTORY_INSERT:
				/* Delete */
				line_delete(
						env->lines[e->contents.insert_delete_replace.lineno],
						e->contents.insert_delete_replace.offset+1,
						e->contents.insert_delete_replace.lineno
				);
				count_chars++;
				break;
			case HISTORY_DELETE:
				{
					char_t _c = {codepoint_width(e->contents.insert_delete_replace.old_codepoint),0,e->contents.insert_delete_replace.old_codepoint};
					env->lines[e->contents.insert_delete_replace.lineno] = line_insert(
							env->lines[e->contents.insert_delete_replace.lineno],
							_c,
							e->contents.insert_delete_replace.offset-1,
							e->contents.insert_delete_replace.lineno
					);
				}
				count_chars++;
				break;
			case HISTORY_REPLACE:
				{
					char_t _o = {codepoint_width(e->contents.insert_delete_replace.old_codepoint),0,e->contents.insert_delete_replace.old_codepoint};
					line_replace(
							env->lines[e->contents.insert_delete_replace.lineno],
							_o,
							e->contents.insert_delete_replace.offset,
							e->contents.insert_delete_replace.lineno
					);
				}
				count_chars++;
				break;
			case HISTORY_REMOVE_LINE:
				env->lines = add_line(env->lines, e->contents.remove_replace_line.lineno);
				replace_line(env->lines, e->contents.remove_replace_line.lineno, e->contents.remove_replace_line.old_contents);
				count_lines++;
				break;
			case HISTORY_ADD_LINE:
				env->lines = remove_line(env->lines, e->contents.add_merge_split_lines.lineno);
				count_lines++;
				break;
			case HISTORY_REPLACE_LINE:
				replace_line(env->lines, e->contents.remove_replace_line.lineno, e->contents.remove_replace_line.old_contents);
				count_lines++;
				break;
			case HISTORY_SPLIT_LINE:
				env->lines = merge_lines(env->lines, e->contents.add_merge_split_lines.lineno+1);
				count_lines++;
				break;
			case HISTORY_MERGE_LINES:
				env->lines = split_line(env->lines, e->contents.add_merge_split_lines.lineno-1, e->contents.add_merge_split_lines.split);
				count_lines++;
				break;
			case HISTORY_BREAK:
				/* Ignore break */
				break;
			default:
				render_error("Unknown type %d!\n", e->type);
				break;
		}

		env->line_no = env->history->line;
		env->col_no = env->history->col;

		env->history = e->previous;
		e = env->history;
	} while (e->type != HISTORY_BREAK);

	if (env->line_no > env->line_count) env->line_no = env->line_count;
	if (env->line_no < 1) env->line_no = 1;
	if (env->col_no > env->lines[env->line_no-1]->actual) env->col_no = env->lines[env->line_no-1]->actual;
	if (env->col_no < 1) env->col_no = 1;

	env->modified = (env->history != env->last_save_history);

	env->loading = 0;

	for (int i = 0; i < env->line_count; ++i) {
		env->lines[i]->istate = 0;
		recalculate_tabs(env->lines[i]);
	}
	schedule_complete_recalc();
	place_cursor_actual();
	update_title();
	redraw_all();
	render_commandline_message("%d character%s, %d line%s changed",
			count_chars, (count_chars == 1) ? "" : "s",
			count_lines, (count_lines == 1) ? "" : "s");
}

BIM_ACTION(redo_history, ACTION_IS_RW,
	"Redo history until the next breakpoint."
,void) {
	if (!global_config.history_enabled) return;

	env->loading = 1;
	history_t * e = env->history->next;

	if (!e) {
		env->loading = 0;
		render_commandline_message("Already at newest change");
		return;
	}

	int count_chars = 0;
	int count_lines = 0;

	while (e) {
		if (e->type == HISTORY_BREAK) {
			env->history = e;
			break;
		}

		switch (e->type) {
			case HISTORY_INSERT:
				{
					char_t _c = {codepoint_width(e->contents.insert_delete_replace.codepoint),0,e->contents.insert_delete_replace.codepoint};
					env->lines[e->contents.insert_delete_replace.lineno] = line_insert(
							env->lines[e->contents.insert_delete_replace.lineno],
							_c,
							e->contents.insert_delete_replace.offset,
							e->contents.insert_delete_replace.lineno
					);
				}
				count_chars++;
				break;
			case HISTORY_DELETE:
				/* Delete */
				line_delete(
						env->lines[e->contents.insert_delete_replace.lineno],
						e->contents.insert_delete_replace.offset,
						e->contents.insert_delete_replace.lineno
				);
				count_chars++;
				break;
			case HISTORY_REPLACE:
				{
					char_t _o = {codepoint_width(e->contents.insert_delete_replace.codepoint),0,e->contents.insert_delete_replace.codepoint};
					line_replace(
							env->lines[e->contents.insert_delete_replace.lineno],
							_o,
							e->contents.insert_delete_replace.offset,
							e->contents.insert_delete_replace.lineno
					);
				}
				count_chars++;
				break;
			case HISTORY_ADD_LINE:
				env->lines = add_line(env->lines, e->contents.remove_replace_line.lineno);
				count_lines++;
				break;
			case HISTORY_REMOVE_LINE:
				env->lines = remove_line(env->lines, e->contents.remove_replace_line.lineno);
				count_lines++;
				break;
			case HISTORY_REPLACE_LINE:
				replace_line(env->lines, e->contents.remove_replace_line.lineno, e->contents.remove_replace_line.contents);
				count_lines++;
				break;
			case HISTORY_MERGE_LINES:
				env->lines = merge_lines(env->lines, e->contents.add_merge_split_lines.lineno);
				count_lines++;
				break;
			case HISTORY_SPLIT_LINE:
				env->lines = split_line(env->lines, e->contents.add_merge_split_lines.lineno, e->contents.add_merge_split_lines.split);
				count_lines++;
				break;
			case HISTORY_BREAK:
				/* Ignore break */
				break;
			default:
				render_error("Unknown type %d!\n", e->type);
				break;
		}

		env->history = e;
		e = e->next;
	}

	env->line_no = env->history->line;
	env->col_no = env->history->col;

	if (env->line_no > env->line_count) env->line_no = env->line_count;
	if (env->line_no < 1) env->line_no = 1;
	if (env->col_no > env->lines[env->line_no-1]->actual) env->col_no = env->lines[env->line_no-1]->actual;
	if (env->col_no < 1) env->col_no = 1;

	env->modified = (env->history != env->last_save_history);

	env->loading = 0;

	for (int i = 0; i < env->line_count; ++i) {
		env->lines[i]->istate = 0;
		recalculate_tabs(env->lines[i]);
	}
	schedule_complete_recalc();
	place_cursor_actual();
	update_title();
	redraw_all();
	render_commandline_message("%d character%s, %d line%s changed",
			count_chars, (count_chars == 1) ? "" : "s",
			count_lines, (count_lines == 1) ? "" : "s");
}

int is_whitespace(int codepoint) {
	return codepoint == ' ' || codepoint == '\t';
}

int is_normal(int codepoint) {
	return isalnum(codepoint) || codepoint == '_';
}

int is_special(int codepoint) {
	return !is_normal(codepoint) && !is_whitespace(codepoint);
}

BIM_ACTION(word_left, 0,
	"Move the cursor left to the previous word."
,void) {
	if (!env->lines[env->line_no-1]) return;

	while (env->col_no > 1 && is_whitespace(env->lines[env->line_no - 1]->text[env->col_no - 2].codepoint)) {
		env->col_no -= 1;
	}

	if (env->col_no == 1) {
		if (env->line_no == 1) goto _place;
		env->line_no--;
		env->col_no = env->lines[env->line_no-1]->actual;
		goto _place;
	}

	int (*inverse_comparator)(int) = is_special;
	if (env->col_no > 1 && is_special(env->lines[env->line_no - 1]->text[env->col_no - 2].codepoint)) {
		inverse_comparator = is_normal;
	}

	do {
		if (env->col_no > 1) {
			env->col_no -= 1;
		}
	} while (env->col_no > 1 && !is_whitespace(env->lines[env->line_no - 1]->text[env->col_no - 2].codepoint) && !inverse_comparator(env->lines[env->line_no - 1]->text[env->col_no - 2].codepoint));

_place:
	set_preferred_column();
	place_cursor_actual();
}

BIM_ACTION(big_word_left, 0,
	"Move the cursor left to the previous WORD."
,void) {
	int line_no = env->line_no;
	int col_no = env->col_no;

	do {
		col_no--;
		while (col_no == 0) {
			line_no--;
			if (line_no == 0) {
				goto_line(1);
				set_preferred_column();
				return;
			}
			col_no = env->lines[line_no-1]->actual;
		}
	} while (isspace(env->lines[line_no-1]->text[col_no-1].codepoint));

	do {
		col_no--;
		if (col_no == 0) {
			env->col_no = 1;
			env->line_no = line_no;
			set_preferred_column();
			redraw_statusbar();
			place_cursor_actual();
			return;
		}
	} while (!isspace(env->lines[line_no-1]->text[col_no-1].codepoint));

	env->col_no = col_no;
	env->line_no = line_no;
	set_preferred_column();
	cursor_right();
}

BIM_ACTION(word_right, 0,
	"Move the cursor right to the start of the next word."
,void) {
	if (!env->lines[env->line_no-1]) return;

	if (env->col_no >= env->lines[env->line_no-1]->actual) {
		/* next line */
		if (env->line_no == env->line_count) return;
		env->line_no++;
		env->col_no = 0;
		if (env->col_no >= env->lines[env->line_no-1]->actual) {
			goto _place;
		}
	}

	if (env->col_no < env->lines[env->line_no-1]->actual && is_whitespace(env->lines[env->line_no-1]->text[env->col_no - 1].codepoint)) {
		while (env->col_no < env->lines[env->line_no-1]->actual && is_whitespace(env->lines[env->line_no-1]->text[env->col_no - 1].codepoint)) {
			env->col_no++;
		}
		goto _place;
	}

	int (*inverse_comparator)(int) = is_special;
	if (is_special(env->lines[env->line_no - 1]->text[env->col_no - 1].codepoint)) {
		inverse_comparator = is_normal;
	}

	while (env->col_no < env->lines[env->line_no-1]->actual && !is_whitespace(env->lines[env->line_no - 1]->text[env->col_no - 1].codepoint) && !inverse_comparator(env->lines[env->line_no - 1]->text[env->col_no - 1].codepoint)) {
		env->col_no++;
	}

	while (env->col_no < env->lines[env->line_no-1]->actual && is_whitespace(env->lines[env->line_no - 1]->text[env->col_no - 1].codepoint)) {
		env->col_no++;
	}

_place:
	set_preferred_column();
	place_cursor_actual();
}

BIM_ACTION(big_word_right, 0,
	"Move the cursor right to the start of the next WORD."
,void) {
	int line_no = env->line_no;
	int col_no = env->col_no;

	do {
		col_no++;
		if (col_no > env->lines[line_no-1]->actual) {
			line_no++;
			if (line_no > env->line_count) {
				env->line_no = env->line_count;
				env->col_no  = env->lines[env->line_no-1]->actual;
				set_preferred_column();
				redraw_statusbar();
				place_cursor_actual();
				return;
			}
			col_no = 0;
			break;
		}
	} while (!isspace(env->lines[line_no-1]->text[col_no-1].codepoint));

	do {
		col_no++;
		while (col_no > env->lines[line_no-1]->actual) {
			line_no++;
			if (line_no >= env->line_count) {
				env->col_no = env->lines[env->line_count-1]->actual;
				env->line_no = env->line_count;
				set_preferred_column();
				redraw_statusbar();
				place_cursor_actual();
				return;
			}
			col_no = 1;
		}
	} while (isspace(env->lines[line_no-1]->text[col_no-1].codepoint));

	env->col_no = col_no;
	env->line_no = line_no;
	set_preferred_column();
	redraw_statusbar();
	place_cursor_actual();
	return;
}

BIM_ACTION(delete_at_cursor, ACTION_IS_RW,
	"Delete the character at the cursor, or merge with previous line."
,void) {
	if (env->col_no > 1) {
		line_delete(env->lines[env->line_no - 1], env->col_no - 1, env->line_no - 1);
		env->col_no -= 1;
		if (env->coffset > 0) env->coffset--;
		redraw_line(env->line_no-1);
		set_modified();
		redraw_statusbar();
		place_cursor_actual();
	} else if (env->line_no > 1) {
		int tmp = env->lines[env->line_no - 2]->actual;
		merge_lines(env->lines, env->line_no - 1);
		env->line_no -= 1;
		env->col_no = tmp+1;
		set_preferred_column();
		redraw_text();
		set_modified();
		redraw_statusbar();
		place_cursor_actual();
	}
}

BIM_ACTION(delete_word, ACTION_IS_RW,
	"Delete the previous word."
,void) {
	if (!env->lines[env->line_no-1]) return;
	if (env->col_no > 1) {

		/* Start by deleting whitespace */
		while (env->col_no > 1 && is_whitespace(env->lines[env->line_no - 1]->text[env->col_no - 2].codepoint)) {
			line_delete(env->lines[env->line_no - 1], env->col_no - 1, env->line_no - 1);
			env->col_no -= 1;
			if (env->coffset > 0) env->coffset--;
		}

		int (*inverse_comparator)(int) = is_special;
		if (env->col_no > 1 && is_special(env->lines[env->line_no - 1]->text[env->col_no - 2].codepoint)) {
			inverse_comparator = is_normal;
		}

		do {
			if (env->col_no > 1) {
				line_delete(env->lines[env->line_no - 1], env->col_no - 1, env->line_no - 1);
				env->col_no -= 1;
				if (env->coffset > 0) env->coffset--;
			}
		} while (env->col_no > 1 && !is_whitespace(env->lines[env->line_no - 1]->text[env->col_no - 2].codepoint) && !inverse_comparator(env->lines[env->line_no - 1]->text[env->col_no - 2].codepoint));

		set_preferred_column();
		redraw_text();
		set_modified();
		redraw_statusbar();
		place_cursor_actual();
	}
}

BIM_ACTION(insert_line_feed, ACTION_IS_RW,
	"Insert a line break, splitting the current line into two."
,void) {
	if (env->indent) {
		if ((env->lines[env->line_no-1]->text[env->col_no-2].flags & 0x1F) == FLAG_COMMENT &&
			(env->lines[env->line_no-1]->text[env->col_no-2].codepoint == ' ') &&
			(env->col_no > 3) &&
			(env->lines[env->line_no-1]->text[env->col_no-3].codepoint == '*')) {
			delete_at_cursor();
		}
	}
	if (env->col_no == env->lines[env->line_no - 1]->actual + 1) {
		env->lines = add_line(env->lines, env->line_no);
	} else {
		env->lines = split_line(env->lines, env->line_no-1, env->col_no - 1);
	}
	unhighlight_matching_paren();
	env->coffset = 0;
	env->col_no = 1;
	env->line_no += 1;
	set_preferred_column();
	add_indent(env->line_no-1,env->line_no-2,0);
	if (env->line_no > env->offset + global_config.term_height - global_config.bottom_size - 1) {
		env->offset += 1;
	}
	if (env->highlighting_paren && env->highlighting_paren > env->line_no) env->highlighting_paren++;
	set_modified();
}

BIM_ACTION(yank_lines, 0,
	"Copy lines into the paste buffer."
,void) {
	int start = env->start_line;
	int end = env->line_no;
	if (global_config.yanks) {
		for (unsigned int i = 0; i < global_config.yank_count; ++i) {
			free(global_config.yanks[i]);
		}
		free(global_config.yanks);
	}
	int lines_to_yank;
	int start_point;
	if (start <= end) {
		lines_to_yank = end - start + 1;
		start_point = start - 1;
	} else {
		lines_to_yank = start - end + 1;
		start_point = end - 1;
	}
	global_config.yanks = malloc(sizeof(line_t *) * lines_to_yank);
	global_config.yank_count = lines_to_yank;
	global_config.yank_is_full_lines = 1;
	for (int i = 0; i < lines_to_yank; ++i) {
		global_config.yanks[i] = malloc(sizeof(line_t) + sizeof(char_t) * (env->lines[start_point+i]->available));
		global_config.yanks[i]->available = env->lines[start_point+i]->available;
		global_config.yanks[i]->actual = env->lines[start_point+i]->actual;
		global_config.yanks[i]->istate = 0;
		memcpy(&global_config.yanks[i]->text, &env->lines[start_point+i]->text, sizeof(char_t) * (env->lines[start_point+i]->actual));

		for (int j = 0; j < global_config.yanks[i]->actual; ++j) {
			global_config.yanks[i]->text[j].flags = 0;
		}
	}
}

/**
 * Helper to yank part of a line into a new yank line.
 */
void yank_partial_line(int yank_no, int line_no, int start_off, int count) {
	if (start_off + count > env->lines[line_no]->actual) {
		if (start_off >= env->lines[line_no]->actual) {
			start_off = env->lines[line_no]->actual;
			count = 0;
		} else {
			count = env->lines[line_no]->actual - start_off;
		}
	}
	global_config.yanks[yank_no] = malloc(sizeof(line_t) + sizeof(char_t) * (count + 1));
	global_config.yanks[yank_no]->available = count + 1; /* ensure extra space */
	global_config.yanks[yank_no]->actual = count;
	global_config.yanks[yank_no]->istate = 0;
	memcpy(&global_config.yanks[yank_no]->text, &env->lines[line_no]->text[start_off], sizeof(char_t) * count);
	for (int i = 0; i < count; ++i) {
		global_config.yanks[yank_no]->text[i].flags = 0;
	}
}

/**
 * Yank text...
 */
void yank_text(int start_line, int start_col, int end_line, int end_col) {
	if (global_config.yanks) {
		for (unsigned int i = 0; i < global_config.yank_count; ++i) {
			free(global_config.yanks[i]);
		}
		free(global_config.yanks);
	}
	int lines_to_yank = end_line - start_line + 1;
	int start_point = start_line - 1;
	global_config.yanks = malloc(sizeof(line_t *) * lines_to_yank);
	global_config.yank_count = lines_to_yank;
	global_config.yank_is_full_lines = 0;
	if (lines_to_yank == 1) {
		yank_partial_line(0, start_point, start_col - 1, (end_col - start_col + 1));
	} else {
		yank_partial_line(0, start_point, start_col - 1, (env->lines[start_point]->actual - start_col + 1));
		/* Yank middle lines */
		for (int i = 1; i < lines_to_yank - 1; ++i) {
			global_config.yanks[i] = malloc(sizeof(line_t) + sizeof(char_t) * (env->lines[start_point+i]->available));
			global_config.yanks[i]->available = env->lines[start_point+i]->available;
			global_config.yanks[i]->actual = env->lines[start_point+i]->actual;
			global_config.yanks[i]->istate = 0;
			memcpy(&global_config.yanks[i]->text, &env->lines[start_point+i]->text, sizeof(char_t) * (env->lines[start_point+i]->actual));

			for (int j = 0; j < global_config.yanks[i]->actual; ++j) {
				global_config.yanks[i]->text[j].flags = 0;
			}
		}
		/* Yank end line */
		yank_partial_line(lines_to_yank-1, end_line - 1, 0, end_col);
	}
}

BIM_ACTION(delete_at_column, ARG_IS_CUSTOM | ACTION_IS_RW,
	"Delete from the current column backwards (`<backspace>`) or forwards (`<del>`)."
,int direction) {
	if (direction == -1 && env->sel_col <= 0) return;

	int prev_width = 0;
	int s = (env->line_no < env->start_line) ? env->line_no : env->start_line;
	int e = (env->line_no < env->start_line) ? env->start_line : env->line_no;
	for (int i = s; i <= e; i++) {
		line_t * line = env->lines[i - 1];

		int _x = 0;
		int col = 1;

		int j = 0;
		for (; j < line->actual; ++j) {
			char_t * c = &line->text[j];
			_x += c->display_width;
			col = j+1;
			if (_x > env->sel_col) break;
			prev_width = c->display_width;
		}

		if ((direction == -1) && (_x == env->sel_col && j == line->actual)) {
			line_delete(line, line->actual, i - 1);
			set_modified();
		} else if (_x > env->sel_col) {
			line_delete(line, col - (direction == -1 ? 1 : 0), i - 1);
			set_modified();
		}
	}

	if (direction == -1) {
		env->sel_col -= prev_width;
		env->col_no--;
	}
	redraw_text();
}

void realign_column_cursor(void) {
	line_t * line = env->lines[env->line_no - 1];
	int _x = 0, col = 1, j = 0;
	for (; j < line->actual; ++j) {
		char_t * c = &line->text[j];
		_x += c->display_width;
		col = j + 1;
		if (_x > env->sel_col) break;
	}
	env->col_no = col;
}

BIM_ACTION(column_left, 0, "Move the column cursor left.",void) {
	if (env->sel_col > 0) {
		env->sel_col -= 1;
		env->preferred_column = env->sel_col;
		/* Figure out where the cursor should be */
		realign_column_cursor();
		redraw_all();
	}
}

BIM_ACTION(column_right, 0, "Move the column cursor right.",void) {
	env->sel_col += 1;
	env->preferred_column = env->sel_col;
	/* Figure out where the cursor should be */
	realign_column_cursor();
	redraw_all();
}

BIM_ACTION(column_up, 0, "Move the column cursor up.",void) {
	if (env->line_no > 1 && env->start_line > 1) {
		env->line_no--;
		env->start_line--;
		/* Figure out where the cursor should be */
		realign_column_cursor();
		place_cursor_actual();
		redraw_all();
	}
}

BIM_ACTION(column_down, 0, "Move the column cursor down.",void) {
	if (env->line_no < env->line_count && env->start_line < env->line_count) {
		env->line_no++;
		env->start_line++;
		/* Figure out where the cursor should be */
		realign_column_cursor();
		place_cursor_actual();
		redraw_all();
	}
}

uint32_t * get_word_under_cursor(void) {
	/* Figure out size */
	int c_before = 0;
	int c_after = 0;
	int i = env->col_no;
	while (i > 0) {
		if (!simple_keyword_qualifier(env->lines[env->line_no-1]->text[i-1].codepoint)) break;
		c_before++;
		i--;
	}
	i = env->col_no+1;
	while (i < env->lines[env->line_no-1]->actual+1) {
		if (!simple_keyword_qualifier(env->lines[env->line_no-1]->text[i-1].codepoint)) break;
		c_after++;
		i++;
	}
	if (!c_before && !c_after) return NULL;

	/* Populate with characters */
	uint32_t * out = malloc(sizeof(uint32_t) * (c_before+c_after+1));
	int j = 0;
	while (c_before) {
		out[j] = env->lines[env->line_no-1]->text[env->col_no-c_before].codepoint;
		c_before--;
		j++;
	}
	int x = 0;
	while (c_after) {
		out[j] = env->lines[env->line_no-1]->text[env->col_no+x].codepoint;
		j++;
		x++;
		c_after--;
	}
	out[j] = 0;

	return out;
}

BIM_ACTION(search_under_cursor, 0,
	"Search for the word currently under the cursor."
,void) {
	if (global_config.search) free(global_config.search);
	global_config.search = get_word_under_cursor();

	/* Find it */
	search_next();
}

BIM_ACTION(find_character_forward, ARG_IS_PROMPT | ARG_IS_INPUT,
	"Find a character forward on the current line and place the cursor on (`f`) or before (`t`) it."
,int type, int c) {
	for (int i = env->col_no+1; i <= env->lines[env->line_no-1]->actual; ++i) {
		if (env->lines[env->line_no-1]->text[i-1].codepoint == c) {
			env->col_no = i - !!(type == 't');
			place_cursor_actual();
			set_preferred_column();
			return;
		}
	}
}

BIM_ACTION(find_character_backward, ARG_IS_PROMPT | ARG_IS_INPUT,
	"Find a character backward on the current line and place the cursor on (`F`) or after (`T`) it."
,int type, int c) {
	for (int i = env->col_no-1; i >= 1; --i) {
		if (env->lines[env->line_no-1]->text[i-1].codepoint == c) {
			env->col_no = i + !!(type == 'T');
			place_cursor_actual();
			set_preferred_column();
			return;
		}
	}
}

/**
 * Clear the navigation number buffer
 */
void reset_nav_buffer(int c) {
	if (c == KEY_TIMEOUT) return;
	if (nav_buffer && (c < '0' || c > '9')) {
		nav_buffer = 0;
		redraw_commandline();
	}
}

/**
 * Determine if a column + line number are within range of the
 * current character selection specified by start_line, etc.
 *
 * Used to determine how syntax flags should be set when redrawing
 * selected text in CHAR SELECTION mode.
 */
int point_in_range(int start_line, int end_line, int start_col, int end_col, int line, int col) {
	if (start_line == end_line) {
		if ( end_col < start_col) {
			int tmp = end_col;
			end_col = start_col;
			start_col = tmp;
		}
		return (col >= start_col && col <= end_col);
	}

	if (start_line > end_line) {
		int tmp = end_line;
		end_line = start_line;
		start_line = tmp;

		tmp = end_col;
		end_col = start_col;
		start_col = tmp;
	}

	if (line < start_line || line > end_line) return 0;

	if (line == start_line) {
		return col >= start_col;
	}

	if (line == end_line) {
		return col <= end_col;
	}

	return 1;
}

/**
 * Macro for redrawing selected lines with appropriate highlighting.
 */
#define _redraw_line(line, force_start_line) \
	do { \
		if (!(force_start_line) && (line) == env->start_line) break; \
		if ((line) > env->line_count + 1) { \
			if ((line) - env->offset - 1 < global_config.term_height - global_config.bottom_size - 1) { \
				draw_excess_line((line) - env->offset - 1); \
			} \
			break; \
		} \
		if ((env->line_no < env->start_line  && ((line) < env->line_no || (line) > env->start_line)) || \
			(env->line_no > env->start_line  && ((line) > env->line_no || (line) < env->start_line)) || \
			(env->line_no == env->start_line && (line) != env->start_line)) { \
			for (int j = 0; j < env->lines[(line)-1]->actual; ++j) { \
				env->lines[(line)-1]->text[j].flags &= ~(FLAG_SELECT); \
			} \
		} else { \
			for (int j = 0; j < env->lines[(line)-1]->actual; ++j) { \
				env->lines[(line)-1]->text[j].flags |= FLAG_SELECT; \
			} \
		} \
		redraw_line((line)-1); \
	} while (0)

#define _redraw_line_char(line, force_start_line) \
	do { \
		if (!(force_start_line) && (line) == env->start_line) break; \
		if ((line) > env->line_count + 1) { \
			if ((line) - env->offset - 1 < global_config.term_height - global_config.bottom_size - 1) { \
				draw_excess_line((line) - env->offset - 1); \
			} \
			break; \
		} \
		if ((env->line_no < env->start_line  && ((line) < env->line_no || (line) > env->start_line)) || \
			(env->line_no > env->start_line  && ((line) > env->line_no || (line) < env->start_line)) || \
			(env->line_no == env->start_line && (line) != env->start_line)) { \
			/* Line is completely outside selection */ \
			for (int j = 0; j < env->lines[(line)-1]->actual; ++j) { \
				env->lines[(line)-1]->text[j].flags &= ~(FLAG_SELECT); \
			} \
		} else { \
			if ((line) == env->start_line || (line) == env->line_no) { \
				for (int j = 0; j < env->lines[(line)-1]->actual; ++j) { \
					env->lines[(line)-1]->text[j].flags &= ~(FLAG_SELECT); \
				} \
			} \
			for (int j = 0; j < env->lines[(line)-1]->actual; ++j) { \
				if (point_in_range(env->start_line, env->line_no,env->start_col, env->col_no, (line), j+1)) { \
					env->lines[(line)-1]->text[j].flags |= FLAG_SELECT; \
				} \
			} \
		} \
		redraw_line((line)-1); \
	} while (0)

#define _redraw_line_col(line, force_start_line) \
	do {\
		if (!(force_start_line) && (line) == env->start_line) break; \
		if ((line) > env->line_count + 1) { \
			if ((line) - env->offset - 1 < global_config.term_height - global_config.bottom_size - 1) { \
				draw_excess_line((line) - env->offset - 1); \
			} \
			break; \
		} \
		redraw_line((line)-1); \
	} while (0)

BIM_ACTION(adjust_indent, ARG_IS_CUSTOM | ACTION_IS_RW,
	"Adjust the indentation on the selected lines (`<tab>` for deeper, `<shift-tab>` for shallower)."
,int direction) {
	int lines_to_cover = 0;
	int start_point = 0;
	if (env->start_line <= env->line_no) {
		start_point = env->start_line - 1;
		lines_to_cover = env->line_no - env->start_line + 1;
	} else {
		start_point = env->line_no - 1;
		lines_to_cover = env->start_line - env->line_no + 1;
	}
	for (int i = 0; i < lines_to_cover; ++i) {
		if (env->lines[start_point + i]->actual < 1) continue;
		if (direction == -1) {
			if (env->tabs) {
				if (env->lines[start_point + i]->text[0].codepoint == '\t') {
					line_delete(env->lines[start_point + i],1,start_point+i);
					_redraw_line(start_point+i+1,1);
				}
			} else {
				if (env->lines[start_point + i]->text[0].codepoint == '\t') {
					line_delete(env->lines[start_point + i],1,start_point+i);
					_redraw_line(start_point+i+1,1);
				} else {
					for (int j = 0; j < env->tabstop; ++j) {
						if (env->lines[start_point + i]->text[0].codepoint == ' ') {
							line_delete(env->lines[start_point + i],1,start_point+i);
						}
					}
				}
				_redraw_line(start_point+i+1,1);
			}
		} else if (direction == 1) {
			if (env->tabs) {
				char_t c;
				c.codepoint = '\t';
				c.display_width = env->tabstop;
				c.flags = FLAG_SELECT;
				env->lines[start_point + i] = line_insert(env->lines[start_point + i], c, 0, start_point + i);
			} else {
				for (int j = 0; j < env->tabstop; ++j) {
					char_t c;
					c.codepoint = ' ';
					c.display_width = 1;
					c.flags = FLAG_SELECT;
					env->lines[start_point + i] = line_insert(env->lines[start_point + i], c, 0, start_point + i);
				}
			}
			_redraw_line(start_point+i+1,1);
		}
	}
	if (env->col_no > env->lines[env->line_no-1]->actual) {
		env->col_no = env->lines[env->line_no-1]->actual;
	}
	set_preferred_column();
	set_modified();
}

void recalculate_selected_lines(void) {
	int start = env->line_no < env->start_line ? env->line_no : env->start_line;
	int end = env->line_no > env->start_line ? env->line_no : env->start_line;
	if (start < 1) start = 1;
	if (start > env->line_count) start = env->line_count;
	if (end < 1) end = 1;
	if (end > env->line_count) end = env->line_count;
	for (int i = (start > 1) ? (start-1) : (start); i <= end; ++i) {
		for (int j = 0; j < env->lines[i-1]->actual; j++) {
			env->lines[i-1]->text[j].flags &= ~(FLAG_SELECT);
		}
	}
	redraw_all();
}

BIM_ACTION(enter_line_selection, 0,
	"Enter line selection mode."
,void) {
	/* Set mode */
	env->mode = MODE_LINE_SELECTION;
	/* Store start position */
	env->start_line = env->line_no;
	env->prev_line  = env->start_line;
	env->start_col  = env->col_no;
	/* Redraw commandline to get -- LINE SELECTION -- text */
	redraw_commandline();
	unhighlight_matching_paren();

	/* Set this line as selected for syntax highlighting */
	for (int j = 0; j < env->lines[env->line_no-1]->actual; ++j) {
		env->lines[env->line_no-1]->text[j].flags |= FLAG_SELECT;
	}

	/* And redraw it */
	redraw_line(env->line_no-1);
}

BIM_ACTION(switch_selection_mode, ARG_IS_CUSTOM,
	"Swap between LINE and CHAR selection modes."
,int mode) {
	env->mode = mode;
	if (mode == MODE_LINE_SELECTION) {
		int start = env->line_no < env->start_line ? env->line_no : env->start_line;
		int end = env->line_no > env->start_line ? env->line_no : env->start_line;
		for (int i = start; i <= end; ++i) {
			_redraw_line(i, 1);
		}
	} else if (mode == MODE_CHAR_SELECTION) {
		int start = env->line_no < env->start_line ? env->line_no : env->start_line;
		int end = env->line_no > env->start_line ? env->line_no : env->start_line;
		for (int i = start; i <= end; ++i) {
			_redraw_line_char(i, 1);
		}
	}
}

BIM_ACTION(delete_and_yank_lines, 0,
	"Delete and yank the selected lines."
,void) {
	yank_lines();
	if (env->start_line <= env->line_no) {
		int lines_to_delete = env->line_no - env->start_line + 1;
		for (int i = 0; i < lines_to_delete; ++i) {
			env->lines = remove_line(env->lines, env->start_line-1);
		}
		env->line_no = env->start_line;
	} else {
		int lines_to_delete = env->start_line - env->line_no + 1;
		for (int i = 0; i < lines_to_delete; ++i) {
			env->lines = remove_line(env->lines, env->line_no-1);
		}
	}
	if (env->line_no > env->line_count) {
		env->line_no = env->line_count;
	}
	if (env->col_no > env->lines[env->line_no-1]->actual) {
		env->col_no = env->lines[env->line_no-1]->actual;
	}
	set_preferred_column();
	set_modified();
}

BIM_ACTION(enter_insert, ACTION_IS_RW,
	"Enter insert mode."
,void) {
	env->mode = MODE_INSERT;
	set_history_break();
}

BIM_ACTION(delete_lines_and_enter_insert, ACTION_IS_RW,
	"Delete and yank the selected lines and then enter insert mode."
,void) {
	delete_and_yank_lines();
	env->lines = add_line(env->lines, env->line_no-1);
	redraw_text();
	env->mode = MODE_INSERT;
}

BIM_ACTION(replace_chars_in_line, ARG_IS_PROMPT | ACTION_IS_RW,
	"Replace characters in the selected lines."
,int c) {
	if (c >= KEY_ESCAPE) {
		render_error("Invalid key for replacement");
		return;
	}
	char_t _c = {codepoint_width(c), 0, c};
	int start_point = env->start_line < env->line_no ? env->start_line : env->line_no;
	int end_point = env->start_line < env->line_no ? env->line_no : env->start_line;
	for (int line = start_point; line <= end_point; ++line) {
		for (int i = 0; i < env->lines[line-1]->actual; ++i) {
			line_replace(env->lines[line-1], _c, i, line-1);
		}
	}
}

BIM_ACTION(leave_selection, 0,
	"Leave selection modes and return to normal mode."
,void) {
	set_history_break();
	env->mode = MODE_NORMAL;
	recalculate_selected_lines();
}

BIM_ACTION(insert_char_at_column, ARG_IS_INPUT | ACTION_IS_RW,
	"Insert a character on all lines at the current column."
,int c) {
	char_t _c;
	_c.codepoint = c;
	_c.flags = 0;
	_c.display_width = codepoint_width(c);

	int inserted_width = 0;

	/* For each line */
	int s = (env->line_no < env->start_line) ? env->line_no : env->start_line;
	int e = (env->line_no < env->start_line) ? env->start_line : env->line_no;
	for (int i = s; i <= e; i++) {
		line_t * line = env->lines[i - 1];

		int _x = 0;
		int col = 1;

		int j = 0;
		for (; j < line->actual; ++j) {
			char_t * c = &line->text[j];
			_x += c->display_width;
			col = j+1;
			if (_x > env->sel_col) break;
		}

		if ((_x == env->sel_col && j == line->actual)) {
			_x = env->sel_col + 1;
			col = line->actual + 1;
		}

		if (_x > env->sel_col) {
			line_t * nline = line_insert(line, _c, col - 1, i - 1);
			if (line != nline) {
				env->lines[i - 1] = nline;
				line = nline;
			}
			set_modified();
		}
		recalculate_tabs(line);
		inserted_width = line->text[col-1].display_width;
		if (i == env->line_no) env->col_no = col + 1;
	}

	env->sel_col += inserted_width;
	env->preferred_column = env->sel_col;
	place_cursor_actual();
}

BIM_ACTION(insert_tab_at_column, ACTION_IS_RW,
	"Insert an indentation character on multiple lines."
,void) {
	if (env->tabs) {
		insert_char_at_column('\t');
	} else {
		for (int i = 0; i < env->tabstop; ++i) {
			insert_char_at_column(' ');
		}
	}
}

BIM_ACTION(enter_col_insert, ACTION_IS_RW,
	"Enter column insert mode."
,void) {
	env->mode = MODE_COL_INSERT;
}

BIM_ACTION(enter_col_insert_after, ACTION_IS_RW,
	"Enter column insert mode after the selected column."
,void) {
	env->sel_col += 1;
	enter_col_insert();
}

BIM_ACTION(enter_col_selection, 0,
	"Enter column selection mode."
,void) {
	/* Set mode */
	env->mode = MODE_COL_SELECTION;
	/* Store cursor */
	env->start_line = env->line_no;
	env->sel_col = env->preferred_column;
	env->prev_line = env->start_line;
	/* Redraw commandline */
	redraw_commandline();
	/* Nothing else to do here; rely on cursor */
}

BIM_ACTION(yank_characters, 0,
	"Yank the selected characters to the paste buffer."
,void) {
	int end_line = env->line_no;
	int end_col  = env->col_no;
	if (env->start_line == end_line) {
		if (env->start_col > end_col) {
			int tmp = env->start_col;
			env->start_col = end_col;
			end_col = tmp;
		}
	} else if (env->start_line > end_line) {
		int tmp = env->start_line;
		env->start_line = end_line;
		end_line = tmp;
		tmp = env->start_col;
		env->start_col = end_col;
		end_col = tmp;
	}
	yank_text(env->start_line, env->start_col, end_line, end_col);
}

BIM_ACTION(delete_and_yank_chars, ACTION_IS_RW,
	"Delete and yank the selected characters."
,void) {
	int end_line = env->line_no;
	int end_col  = env->col_no;
	if (env->start_line == end_line) {
		if (env->start_col > end_col) {
			int tmp = env->start_col;
			env->start_col = end_col;
			end_col = tmp;
		}
		yank_text(env->start_line, env->start_col, end_line, end_col);
		for (int i = env->start_col; i <= end_col; ++i) {
			line_delete(env->lines[env->start_line-1], env->start_col, env->start_line - 1);
		}
		env->col_no = env->start_col;
	} else {
		if (env->start_line > end_line) {
			int tmp = env->start_line;
			env->start_line = end_line;
			end_line = tmp;
			tmp = env->start_col;
			env->start_col = end_col;
			end_col = tmp;
		}
		/* Copy lines */
		yank_text(env->start_line, env->start_col, end_line, end_col);
		/* Delete lines */
		for (int i = env->start_line+1; i < end_line; ++i) {
			env->lines = remove_line(env->lines, env->start_line);
		} /* end_line is no longer valid; should be start_line+1*/
		/* Delete from env->start_col forward */
		int tmp = env->lines[env->start_line-1]->actual;
		for (int i = env->start_col; i <= tmp; ++i) {
			line_delete(env->lines[env->start_line-1], env->start_col, env->start_line - 1);
		}
		for (int i = 1; i <= end_col; ++i) {
			line_delete(env->lines[env->start_line], 1, env->start_line);
		}
		/* Merge start and end lines */
		merge_lines(env->lines, env->start_line);
		env->line_no = env->start_line;
		env->col_no = env->start_col;
	}
	if (env->line_no > env->line_count) {
		env->line_no = env->line_count;
	}
	set_preferred_column();
	set_modified();
}

BIM_ACTION(delete_chars_and_enter_insert, ACTION_IS_RW,
	"Delete and yank the selected characters and then enter insert mode."
,void) {
	delete_and_yank_chars();
	redraw_text();
	enter_insert();
}

BIM_ACTION(replace_chars, ARG_IS_PROMPT | ACTION_IS_RW,
	"Replace the selected characters."
,int c) {
	if (c >= KEY_ESCAPE) {
		render_error("Invalid key for replacement");
		return;
	}

	char_t _c = {codepoint_width(c), 0, c};
	/* This should probably be a function line "do_over_range" or something */
	if (env->start_line == env->line_no) {
		int s = (env->start_col < env->col_no) ? env->start_col : env->col_no;
		int e = (env->start_col < env->col_no) ? env->col_no : env->start_col;
		for (int i = s; i <= e; ++i) {
			line_replace(env->lines[env->start_line-1], _c, i-1, env->start_line-1);
		}
		redraw_text();
	} else {
		if (env->start_line < env->line_no) {
			for (int s = env->start_col-1; s < env->lines[env->start_line-1]->actual; ++s) {
				line_replace(env->lines[env->start_line-1], _c, s, env->start_line-1);
			}
			for (int line = env->start_line + 1; line < env->line_no; ++line) {
				for (int i = 0; i < env->lines[line-1]->actual; ++i) {
					line_replace(env->lines[line-1], _c, i, line-1);
				}
			}
			for (int s = 0; s < env->col_no; ++s) {
				line_replace(env->lines[env->line_no-1], _c, s, env->line_no-1);
			}
		} else {
			for (int s = env->col_no-1; s < env->lines[env->line_no-1]->actual; ++s) {
				line_replace(env->lines[env->line_no-1], _c, s, env->line_no-1);
			}
			for (int line = env->line_no + 1; line < env->start_line; ++line) {
				for (int i = 0; i < env->lines[line-1]->actual; ++i) {
					line_replace(env->lines[line-1], _c, i, line-1);
				}
			}
			for (int s = 0; s < env->start_col; ++s) {
				line_replace(env->lines[env->start_line-1], _c, s, env->start_line-1);
			}
		}
	}
}

BIM_ACTION(enter_char_selection, 0,
	"Enter character selection mode."
,void) {
	/* Set mode */
	env->mode = MODE_CHAR_SELECTION;
	/* Set cursor positions */
	env->start_line = env->line_no;
	env->start_col  = env->col_no;
	env->prev_line  = env->start_line;
	/* Redraw commandline for -- CHAR SELECTION -- */
	redraw_commandline();
	unhighlight_matching_paren();

	/* Select single character */
	env->lines[env->line_no-1]->text[env->col_no-1].flags |= FLAG_SELECT;
	redraw_line(env->line_no-1);
}

BIM_ACTION(insert_at_end_of_selection, ACTION_IS_RW,
	"Move the cursor to the end of the selection and enter insert mode."
,void) {
	recalculate_selected_lines();
	if (env->line_no == env->start_line) {
		env->col_no = env->col_no > env->start_col ? env->col_no + 1 : env->start_col + 1;
	} else if (env->line_no < env->start_line) {
		env->col_no = env->start_col + 1;
		env->line_no = env->start_line;
	} else {
		env->col_no += 1;
	}
	env->mode = MODE_INSERT;
}

void free_completion_match(struct completion_match * match) {
	if (match->string) free(match->string);
	if (match->file) free(match->file);
	if (match->search) free(match->search);
}

/**
 * Read ctags file to find matches for a symbol
 */
int read_tags(uint32_t * comp, struct completion_match **matches, int * matches_count, int complete_match) {
	int _matches_len = 4;
	int *matches_len = &_matches_len;
	*matches_count = 0;
	*matches = malloc(sizeof(struct completion_match) * (*matches_len));

	FILE * tags = fopen("tags","r");
	if (tags) {
		char tmp[4096]; /* max line */
		while (!feof(tags) && fgets(tmp, 4096, tags)) {
			if (tmp[0] == '!') continue;
			int i = 0;
			while (comp[i] && comp[i] == (unsigned int)tmp[i]) i++;
			if (comp[i] == '\0') {
				if (complete_match && tmp[i] != '\t') continue;
				int j = i;
				while (tmp[j] != '\t' && tmp[j] != '\n' && tmp[j] != '\0') j++;
				tmp[j] = '\0'; j++;
				char * file = &tmp[j];
				while (tmp[j] != '\t' && tmp[j] != '\n' && tmp[j] != '\0') j++;
				tmp[j] = '\0'; j++;
				char * search = &tmp[j];
				while (!(tmp[j] == '/' && tmp[j+1] == ';' && tmp[j+2] == '"' && tmp[j+3] == '\t') /* /normal searches/ */
				       && !(tmp[j] == ';' && tmp[j+1] == '"' && tmp[j+2] == '\t') /* Old ctags line number searches */
				       && (tmp[j] != '\n' && tmp[j] != '\0')) j++;
				tmp[j] = '\0'; j++;

				add_match(tmp,file,search);
			}
		}
		fclose(tags);
	}

	if (env->syntax && env->syntax->completion_matcher) {
		env->syntax->completion_matcher(comp,matches,matches_count,complete_match,matches_len, env);
	}

	return 0;
}

/**
 * Draw an autocomplete popover with matches.
 */
void draw_completion_matches(uint32_t * tmp, struct completion_match *matches, int matches_count, int index) {
	int original_length = 0;
	while (tmp[original_length]) original_length++;
	int max_width = 0;
	for (int i = 0; i < matches_count; ++i) {
		/* TODO unicode width */
		unsigned int my_width = strlen(matches[i].string) + (matches[i].file ? strlen(matches[i].file) + 1 : 0);
		if (my_width > (unsigned int)max_width) {
			max_width = my_width;
		}
	}

	/* Figure out how much space we have to display the window */
	int cursor_y = env->line_no - env->offset + global_config.tabs_visible;
	int max_y = global_config.term_height - global_config.bottom_size - cursor_y;

	/* Find a good place to put the box horizontally */
	int num_size = num_width() + gutter_width();
	int x = num_size + 1 - env->coffset;

	/* Determine where the cursor is physically */
	for (int i = 0; i < env->col_no - 1 - original_length; ++i) {
		char_t * c = &env->lines[env->line_no-1]->text[i];
		x += c->display_width;
	}

	int box_width = max_width;
	int box_x = x;
	int box_y = cursor_y + 1;
	if (max_width > env->width - num_width() - gutter_width()) {
		box_width = env->width - num_width() - gutter_width();
		box_x = num_width() + gutter_width() + 1;
	} else if (env->width - x < max_width) {
		box_width = max_width;
		box_x = env->width - max_width;
	}

	int max_count = (max_y < matches_count) ? max_y - 1 : matches_count;

	for (int x = index; x < max_count+index; ++x) {
		int i = x % matches_count;
		place_cursor(box_x + env->left, box_y+x-index);
		set_colors(COLOR_KEYWORD, COLOR_STATUS_BG);
		/* TODO wide characters */
		int match_width = strlen(matches[i].string);
		int file_width = matches[i].file ? strlen(matches[i].file) : 0;
		for (int j = 0; j < box_width; ++j) {
			if (j == original_length) set_colors(i == index ? COLOR_NUMERAL : COLOR_STATUS_FG, COLOR_STATUS_BG);
			if (j == match_width) set_colors(COLOR_TYPE, COLOR_STATUS_BG);
			if (j < match_width) printf("%c", matches[i].string[j]);
			else if (j > match_width && j - match_width - 1 < file_width) printf("%c", matches[i].file[j-match_width-1]);
			else printf(" ");
		}
	}
	if (max_count == 0) {
		place_cursor(box_x + env->left, box_y);
		set_colors(COLOR_STATUS_FG, COLOR_STATUS_BG);
		printf(" (no matches) ");
	} else if (max_count != matches_count) {
		place_cursor(box_x + env->left, box_y+max_count);
		set_colors(COLOR_STATUS_FG, COLOR_STATUS_BG);
		printf(" (%d more) ", matches_count-max_count);
	}
}

/**
 * Autocomplete words (function/variable names, etc.) in input mode.
 */
int omni_complete(int quit_quietly_on_none) {
	int retval = 0;
	int index = 0;

	int c;

	int (*qualifier)(int c) = simple_keyword_qualifier;
	if (env->syntax && env->syntax->completion_qualifier) {
		qualifier = env->syntax->completion_qualifier;
	}

	/* Pull the word from before the cursor */
	int c_before = 0;
	int i = env->col_no-1;
	while (i > 0) {
		int c = env->lines[env->line_no-1]->text[i-1].codepoint;
		if (!qualifier(c)) break;
		c_before++;
		i--;
	}

	if (!c_before && quit_quietly_on_none) return 0;

	/* Populate with characters */
	uint32_t * tmp = malloc(sizeof(uint32_t) * (c_before+1));
	int j = 0;
	while (c_before) {
		tmp[j] = env->lines[env->line_no-1]->text[env->col_no-c_before-1].codepoint;
		c_before--;
		j++;
	}
	tmp[j] = 0;

	/*
	 * TODO matches should probably be a struct with more data than just
	 * the matching string; maybe offset where the needle was found,
	 * class information, source file information - anything we can extract
	 * from ctags, but also other information for other sources of completion.
	 */
	struct completion_match *matches;
	int matches_count;

	/* TODO just reading ctags is rather mediocre; can we do something cool here? */
	if (read_tags(tmp, &matches, &matches_count, 0)) {
		goto _completion_done;
	}

	/* Draw box with matches at cursor-width(tmp) */
	if (quit_quietly_on_none && matches_count == 0) {
		free(tmp);
		free(matches);
		return 0;
	}

	draw_completion_matches(tmp, matches, matches_count, 0);

_completion_done:
	place_cursor_actual();
	while (1) {
		c = bim_getch();
		if (c == -1) continue;
		if (matches_count < 1) {
			redraw_all();
			break;
		}
		if (c == 15) {
			index = (index + 1) % matches_count;
			draw_completion_matches(tmp, matches, matches_count, index);
			place_cursor_actual();
			continue;
		} else if (c == '\t') {
			for (unsigned int i = j; i < strlen(matches[index].string); ++i) {
				insert_char(matches[index].string[i]);
			}
			set_preferred_column();
			redraw_text();
			place_cursor_actual();
			goto _finish_completion;
		} else if (isgraph(c) && c != '}') {
			/* insert and continue matching */
			insert_char(c);
			set_preferred_column();
			redraw_text();
			place_cursor_actual();
			retval = 1;
			goto _finish_completion;
		} else if (c == DELETE_KEY || c == BACKSPACE_KEY) {
			delete_at_cursor();
			set_preferred_column();
			redraw_text();
			place_cursor_actual();
			retval = 1;
			goto _finish_completion;
		}
		/* TODO: Keyboard navigation of the matches list would be nice */
		redraw_all();
		break;
	}
	bim_unget(c);
_finish_completion:
	for (int i = 0; i < matches_count; ++i) {
		free_completion_match(&matches[i]);
	}
	free(matches);
	free(tmp);
	return retval;
}

/**
 * Set the search string from a UTF-8 sequence.
 * Since the search string is normally a series of codepoints, this saves
 * some effort when trying to search for things we pulled from the outside world.
 * (eg., ctags search terms)
 */
static void set_search_from_bytes(char * bytes) {
	if (global_config.search) free(global_config.search);
	global_config.search = malloc(sizeof(uint32_t) * (strlen(bytes) * 2 + 1));
	uint32_t * s = global_config.search;
	char * tmp = bytes;
	uint32_t c, istate = 0;
	while (*tmp) {
		if (strchr("\\.()[]+*?", *tmp)) {
			*s++ = '\\';
			*s++ = *tmp;
			*s = 0;
			tmp++;
			continue;
		}
		if (!decode(&istate, &c, *tmp)) {
			*s = c;
			s++;
			*s = 0;
		} else if (istate == UTF8_REJECT) {
			istate = 0;
		}
		tmp++;
	}
}

static void _perform_correct_search(struct completion_match * matches, int i) {
	if (matches[i].search[0] == '/') {
		set_search_from_bytes(&matches[i].search[1]);
		search_next();
	} else {
		goto_line(atoi(matches[i].search));
	}
}

BIM_ACTION(goto_definition, 0,
	"Jump to the definition of the word under under cursor."
,void) {
	uint32_t * word = get_word_under_cursor();
	if (!word) {
		render_error("No match");
		return;
	}

	struct completion_match *matches;
	int matches_count;

	if (read_tags(word, &matches, &matches_count, 1)) {
		render_error("No tags file");
		goto _done;
	}

	if (!matches_count) {
		render_error("No match");
		goto _done;
	}

	if (env->file_name && !strcmp(matches[0].file, env->file_name)) {
		_perform_correct_search(matches, 0);
	} else {
		/* Check if there were other matches that are in this file */
		for (int i =1; env->file_name && i < matches_count; ++i) {
			if (!strcmp(matches[i].file, env->file_name)) {
				_perform_correct_search(matches, i);
				goto _done;
			}
		}
		/* Check buffers */
		for (int i = 0; i < buffers_len; ++i) {
			if (buffers[i]->file_name && !strcmp(matches[0].file,buffers[i]->file_name)) {
				if (left_buffer && buffers[i] != left_buffer && buffers[i] != right_buffer) unsplit();
				env = buffers[i];
				redraw_tabbar();
				_perform_correct_search(matches, i);
				goto _done;
			}
		}
		/* Okay, let's try opening */
		buffer_t * old_buf = env;
		open_file(matches[0].file);
		if (env != old_buf) {
			_perform_correct_search(matches, 0);
		} else {
			render_error("Could not locate file containing definition");
		}
	}

_done:
	for (int i = 0; i < matches_count; ++i) {
		free_completion_match(&matches[i]);
	}
	free(matches);
	free(word);
}

/**
 * Read one codepoint, with verbatim support.
 */
int read_one_character(char * message) {
	/* Read one character and replace */
	if (!global_config.overlay_mode) {
		render_commandline_message(message);
		place_cursor_actual();
	}
	int c;
	while ((c = bim_getkey(DEFAULT_KEY_WAIT))) {
		if (c == KEY_TIMEOUT) continue;
		if (c == KEY_CTRL_V) {
			if (!global_config.overlay_mode) {
				render_commandline_message(message);
				printf(" ^V");
				place_cursor_actual();
			}
			while ((c = bim_getch()) == -1);
			break;
		}
		break;
	}

	redraw_commandline();
	return c;
}

int read_one_byte(char * message) {
	if (!global_config.overlay_mode) {
		render_commandline_message(message);
		place_cursor_actual();
	}
	int c;
	while ((c = bim_getch())) {
		if (c == -1) continue;
		break;
	}
	redraw_commandline();
	return c;
}

BIM_ACTION(cursor_left_with_wrap, 0,
	"Move the cursor one position left, wrapping to the previous line."
,void) {
	if (env->line_no > 1 && env->col_no == 1) {
		env->line_no--;
		env->col_no = env->lines[env->line_no-1]->actual;
		set_preferred_column();
		place_cursor_actual();
	} else {
		cursor_left();
	}
}

BIM_ACTION(prepend_and_insert, ACTION_IS_RW,
	"Insert a new line before the current line and enter insert mode."
,void) {
	set_history_break();
	env->lines = add_line(env->lines, env->line_no-1);
	env->col_no = 1;
	add_indent(env->line_no-1,env->line_no,0);
	if (env->highlighting_paren && env->highlighting_paren > env->line_no) env->highlighting_paren++;
	redraw_text();
	set_preferred_column();
	set_modified();
	place_cursor_actual();
	env->mode = MODE_INSERT;
}

BIM_ACTION(append_and_insert, ACTION_IS_RW,
	"Insert a new line after the current line and enter insert mode."
,void) {
	set_history_break();
	unhighlight_matching_paren();
	env->lines = add_line(env->lines, env->line_no);
	env->col_no = 1;
	env->line_no += 1;
	add_indent(env->line_no-1,env->line_no-2,0);
	set_preferred_column();
	if (env->line_no > env->offset + global_config.term_height - global_config.bottom_size - 1) {
		env->offset += 1;
	}
	if (env->highlighting_paren && env->highlighting_paren > env->line_no) env->highlighting_paren++;
	redraw_text();
	set_modified();
	place_cursor_actual();
	env->mode = MODE_INSERT;
}

BIM_ACTION(insert_after_cursor, ACTION_IS_RW,
	"Place the cursor after the selected character and enter insert mode."
,void) {
	if (env->col_no < env->lines[env->line_no-1]->actual + 1) {
		env->col_no += 1;
	}
	enter_insert();
}

BIM_ACTION(delete_forward, ACTION_IS_RW,
	"Delete the character under the cursor."
,void) {
	if (env->col_no <= env->lines[env->line_no-1]->actual) {
		line_delete(env->lines[env->line_no-1], env->col_no, env->line_no-1);
		redraw_text();
	} else if (env->col_no == env->lines[env->line_no-1]->actual + 1 && env->line_count > env->line_no) {
		merge_lines(env->lines, env->line_no);
		redraw_text();
	}
	set_modified();
	redraw_statusbar();
	place_cursor_actual();
}

BIM_ACTION(delete_forward_and_insert, ACTION_IS_RW,
	"Delete the character under the cursor and enter insert mode."
,void) {
	set_history_break();
	delete_forward();
	env->mode = MODE_INSERT;
}

BIM_ACTION(paste, ARG_IS_CUSTOM | ACTION_IS_RW,
	"Paste yanked text before (`P`) or after (`p`) the cursor."
,int direction) {
	if (global_config.yanks) {
		env->slowop = 1;
		if (!global_config.yank_is_full_lines) {
			/* Handle P for paste before, p for past after */
			int target_column = (direction == -1 ? (env->col_no) : (env->col_no+1));
			if (target_column > env->lines[env->line_no-1]->actual + 1) {
				target_column = env->lines[env->line_no-1]->actual + 1;
			}
			if (global_config.yank_count > 1) {
				/* Spit the current line at the current position */
				env->lines = split_line(env->lines, env->line_no - 1, target_column - 1); /* Split after */
			}
			/* Insert first line at current position */
			for (int i = 0; i < global_config.yanks[0]->actual; ++i) {
				env->lines[env->line_no - 1] = line_insert(env->lines[env->line_no - 1], global_config.yanks[0]->text[i], target_column + i - 1, env->line_no - 1); 
			}
			if (global_config.yank_count > 1) {
				/* Insert full lines */
				for (unsigned int i = 1; i < global_config.yank_count - 1; ++i) {
					env->lines = add_line(env->lines, env->line_no);
				}
				for (unsigned int i = 1; i < global_config.yank_count - 1; ++i) {
					replace_line(env->lines, env->line_no + i - 1, global_config.yanks[i]);
				}
				/* Insert characters from last line into (what was) the next line */
				for (int i = 0; i < global_config.yanks[global_config.yank_count-1]->actual; ++i) {
					env->lines[env->line_no + global_config.yank_count - 2] = line_insert(env->lines[env->line_no + global_config.yank_count - 2], global_config.yanks[global_config.yank_count-1]->text[i], i, env->line_no + global_config.yank_count - 2);
				}
			}
		} else {
			/* Insert full lines */
			for (unsigned int i = 0; i < global_config.yank_count; ++i) {
				env->lines = add_line(env->lines, env->line_no - (direction == -1 ? 1 : 0));
			}
			for (unsigned int i = 0; i < global_config.yank_count; ++i) {
				replace_line(env->lines, env->line_no - (direction == -1 ? 1 : 0) + i, global_config.yanks[i]);
			}
		}
		env->slowop = 0;
		schedule_complete_recalc();
		/* Recalculate whole document syntax */
		if (direction == 1) {
			if (global_config.yank_is_full_lines) {
				env->line_no += 1;
			} else {
				if (global_config.yank_count == 1) {
					env->col_no = env->col_no + global_config.yanks[0]->actual;
				} else {
					env->line_no = env->line_no + global_config.yank_count - 1;
					env->col_no = global_config.yanks[global_config.yank_count-1]->actual;
				}
			}
		}
		if (global_config.yank_is_full_lines) {
			env->col_no = 1;
			for (int i = 0; i < env->lines[env->line_no-1]->actual; ++i) {
				if (!is_whitespace(env->lines[env->line_no-1]->text[i].codepoint)) {
					env->col_no = i + 1;
					break;
				}
			}
		}
		set_history_break();
		set_modified();
		redraw_all();
	}
}

BIM_ACTION(insert_at_end, ACTION_IS_RW,
	"Move the cursor to the end of the current line and enter insert mode."
,void) {
	env->col_no = env->lines[env->line_no-1]->actual+1;
	env->mode = MODE_INSERT;
	set_history_break();
}

BIM_ACTION(enter_replace, ACTION_IS_RW,
	"Enter replace mode."
,void) {
	env->mode = MODE_REPLACE;
	set_history_break();
}

BIM_ACTION(toggle_numbers, 0,
	"Toggle the display of line numbers."
,void) {
	env->numbers = !env->numbers;
	redraw_all();
	place_cursor_actual();
}

BIM_ACTION(toggle_gutter, 0,
	"Toggle the display of the revision status gutter."
,void) {
	env->gutter = !env->gutter;
	redraw_all();
	place_cursor_actual();
}

BIM_ACTION(toggle_indent, 0,
	"Toggle smart indentation."
,void) {
	env->indent = !env->indent;
	redraw_statusbar();
	place_cursor_actual();
}

BIM_ACTION(toggle_smartcomplete, 0,
	"Toggle smart completion."
,void) {
	global_config.smart_complete = !global_config.smart_complete;
	redraw_statusbar();
	place_cursor_actual();
}

BIM_ACTION(expand_split_right, 0,
	"Move the view split divider to the right."
,void) {
	global_config.split_percent += 1;
	update_split_size();
	redraw_all();
}

BIM_ACTION(expand_split_left, 0,
	"Move the view split divider to the left."
,void) {
	global_config.split_percent -= 1;
	update_split_size();
	redraw_all();
}

BIM_ACTION(go_page_up, 0,
	"Jump up a screenfull."
,void) {
	int destination = env->line_no - (global_config.term_height - 6);
	if (destination < 1) destination = 1;
	goto_line(destination);
}

BIM_ACTION(go_page_down, 0,
	"Jump down a screenfull."
,void) {
	goto_line(env->line_no + (global_config.term_height - 6));
}

BIM_ACTION(jump_to_matching_bracket, 0,
	"Find and jump to the matching bracket for the character under the cursor."
,void) {
	recalculate_selected_lines();
	int paren_line = -1, paren_col = -1;
	find_matching_paren(&paren_line, &paren_col, 1);
	if (paren_line != -1) {
		env->line_no = paren_line;
		env->col_no = paren_col;
		set_preferred_column();
		place_cursor_actual();
		redraw_statusbar();
	}
}

BIM_ACTION(jump_to_previous_blank, 0,
	"Jump to the preceding blank line before the cursor."
,void) {
	env->col_no = 1;
	if (env->line_no == 1) return;
	do {
		env->line_no--;
		if (env->lines[env->line_no-1]->actual == 0) break;
	} while (env->line_no > 1);
	set_preferred_column();
	redraw_statusbar();
}

BIM_ACTION(jump_to_next_blank, 0,
	"Jump to the next blank line after the cursor."
,void) {
	env->col_no = 1;
	if (env->line_no == env->line_count) return;
	do {
		env->line_no++;
		if (env->lines[env->line_no-1]->actual == 0) break;
	} while (env->line_no < env->line_count);
	set_preferred_column();
	redraw_statusbar();
}

BIM_ACTION(first_non_whitespace, 0,
	"Jump to the first non-whitespace character in the current line."
,void) {
	for (int i = 0; i < env->lines[env->line_no-1]->actual; ++i) {
		if (!is_whitespace(env->lines[env->line_no-1]->text[i].codepoint)) {
			env->col_no = i + 1;
			break;
		}
	}
	set_preferred_column();
	redraw_statusbar();
}

BIM_ACTION(next_line_non_whitespace, 0,
	"Jump to the first non-whitespace character in the next next line."
,void) {
	if (env->line_no < env->line_count) {
		env->line_no++;
		env->col_no = 1;
	} else {
		return;
	}
	first_non_whitespace();
}

BIM_ACTION(smart_backspace, ACTION_IS_RW,
	"Delete the preceding character, with special handling for indentation."
,void) {
	if (!env->tabs && env->col_no > 1) {
		int i;
		for (i = 0; i < env->col_no-1; ++i) {
			if (!is_whitespace(env->lines[env->line_no-1]->text[i].codepoint)) break;
		}
		if (i == env->col_no-1) {
			/* Backspace until aligned */
			delete_at_cursor();
			while (env->col_no > 1 && (env->col_no-1) % env->tabstop) {
				delete_at_cursor();
			}
			return;
		}
	}
	delete_at_cursor();
}

BIM_ACTION(perform_omni_completion, ACTION_IS_RW,
	"(temporary) Perform smart symbol completion from ctags."
,void) {
	/* This should probably be a submode */
	while (omni_complete(0) == 1);
}

BIM_ACTION(smart_tab, ACTION_IS_RW,
	"Insert a tab or spaces depending on indent mode. (Use ^V <tab> to guarantee a literal tab)"
,void) {
	if (env->tabs) {
		insert_char('\t');
	} else {
		for (int i = 0; i < env->tabstop; ++i) {
			insert_char(' ');
		}
	}
}

BIM_ACTION(smart_comment_end, ARG_IS_INPUT | ACTION_IS_RW,
	"Insert a `/` ending a C-style comment."
,int c) {
	/* smart *end* of comment anyway */
	if (env->indent) {
		if ((env->lines[env->line_no-1]->text[env->col_no-2].flags & 0x1F) == FLAG_COMMENT &&
			(env->lines[env->line_no-1]->text[env->col_no-2].codepoint == ' ') &&
			(env->col_no > 3) &&
			(env->lines[env->line_no-1]->text[env->col_no-3].codepoint == '*')) {
			env->col_no--;
			replace_char('/');
			env->col_no++;
			place_cursor_actual();
			return;
		}
	}
	insert_char(c);
}

BIM_ACTION(smart_brace_end, ARG_IS_INPUT | ACTION_IS_RW,
	"Insert a closing brace and smartly position it if it is the first character on a line."
,int c) {
	if (env->indent) {
		int was_whitespace = 1;
		for (int i = 0; i < env->lines[env->line_no-1]->actual; ++i) {
			if (env->lines[env->line_no-1]->text[i].codepoint != ' ' &&
				env->lines[env->line_no-1]->text[i].codepoint != '\t') {
				was_whitespace = 0;
				break;
			}
		}
		insert_char(c);
		if (was_whitespace) {
			int line = -1, col = -1;
			env->col_no--;
			find_matching_paren(&line,&col, 1);
			if (line != -1) {
				line = find_brace_line_start(line, col);
				while (env->lines[env->line_no-1]->actual) {
					line_delete(env->lines[env->line_no-1], env->lines[env->line_no-1]->actual, env->line_no-1);
				}
				add_indent(env->line_no-1,line-1,1);
				env->col_no = env->lines[env->line_no-1]->actual + 1;
				insert_char(c);
			}
		}
		set_preferred_column();
		return;
	}
	insert_char(c);
}

BIM_ACTION(enter_line_selection_and_cursor_up, 0,
	"Enter line selection and move the cursor up one line."
,void) {
	enter_line_selection();
	cursor_up();
}

BIM_ACTION(enter_line_selection_and_cursor_down, 0,
	"Enter line selection and move the cursor down one line."
,void) {
	enter_line_selection();
	cursor_down();
}

BIM_ACTION(shift_horizontally, ARG_IS_CUSTOM,
	"Shift the current line or screen view horizontally, depending on settings."
,int amount) {
	env->coffset += amount;
	if (env->coffset < 0) env->coffset = 0;
	redraw_text();
}

static int state_before_paste = 0;
static int line_before_paste = 0;
BIM_ACTION(paste_begin, 0, "Begin bracketed paste; disable indentation, completion, etc.",void) {
	if (global_config.smart_complete) state_before_paste |= 0x01;
	if (env->indent) state_before_paste |= 0x02;

	global_config.smart_complete = 0;
	env->indent = 0;
	env->slowop = 1;
	line_before_paste = env->line_no;
}

BIM_ACTION(paste_end, 0, "End bracketed paste; restore indentation, completion, etc.",void) {
	if (state_before_paste & 0x01) global_config.smart_complete = 1;
	if (state_before_paste & 0x02) env->indent = 1;
	env->slowop = 0;
	int line_to_recalculate = (line_before_paste > 1 ? line_before_paste - 1 : 0);
	recalculate_syntax(env->lines[line_to_recalculate], line_to_recalculate);
	redraw_all();
}

#define MAP_ACTION(key, func, opts, arg) {key, opts, {{(uintptr_t)func, arg}}}

struct action_map _NORMAL_MAP[] = {
	MAP_ACTION(KEY_BACKSPACE, cursor_left_with_wrap, opt_rep, 0),
	MAP_ACTION('V',           enter_line_selection, 0, 0),
	MAP_ACTION('v',           enter_char_selection, 0, 0),
	MAP_ACTION(KEY_CTRL_V,    enter_col_selection, 0, 0),
	MAP_ACTION('O',           prepend_and_insert, opt_rw, 0),
	MAP_ACTION('o',           append_and_insert, opt_rw, 0),
	MAP_ACTION('a',           insert_after_cursor, opt_rw, 0),
	MAP_ACTION('s',           delete_forward_and_insert, opt_rw, 0),
	MAP_ACTION('x',           delete_forward, opt_rep | opt_rw, 0),
	MAP_ACTION('P',           paste, opt_arg | opt_rw, -1),
	MAP_ACTION('p',           paste, opt_arg | opt_rw, 1),
	MAP_ACTION('r',           replace_char, opt_char | opt_rw, 0),
	MAP_ACTION('A',           insert_at_end, opt_rw, 0),
	MAP_ACTION('u',           undo_history, opt_rw, 0),
	MAP_ACTION(KEY_CTRL_R,    redo_history, opt_rw, 0),
	MAP_ACTION(KEY_CTRL_L,    redraw_all, 0, 0),
	MAP_ACTION(KEY_CTRL_G,    goto_definition, 0, 0),
	MAP_ACTION('i',           enter_insert, opt_rw, 0),
	MAP_ACTION('R',           enter_replace, opt_rw, 0),
	MAP_ACTION(KEY_SHIFT_UP,   enter_line_selection_and_cursor_up, 0, 0),
	MAP_ACTION(KEY_SHIFT_DOWN, enter_line_selection_and_cursor_down, 0, 0),
	MAP_ACTION(KEY_ALT_UP,    previous_tab, 0, 0),
	MAP_ACTION(KEY_ALT_DOWN,  next_tab, 0, 0),
	MAP_ACTION(KEY_CTRL_UNDERSCORE, start_file_search, 0, 0),
	MAP_ACTION(-1, NULL, 0, 0),
};

struct action_map _INSERT_MAP[] = {
	MAP_ACTION(KEY_ESCAPE,    leave_insert, 0, 0),
	MAP_ACTION(KEY_DELETE,    delete_forward, 0, 0),
	MAP_ACTION(KEY_CTRL_C,    leave_insert, 0, 0),
	MAP_ACTION(KEY_BACKSPACE, smart_backspace, 0, 0),
	MAP_ACTION(KEY_ENTER,     insert_line_feed, 0, 0),
	MAP_ACTION(KEY_CTRL_O,    perform_omni_completion, 0, 0),
	MAP_ACTION(KEY_CTRL_V,    insert_char, opt_byte, 0),
	MAP_ACTION(KEY_CTRL_W,    delete_word, 0, 0),
	MAP_ACTION('\t',          smart_tab, 0, 0),
	MAP_ACTION('/',           smart_comment_end, opt_arg, '/'),
	MAP_ACTION('}',           smart_brace_end, opt_arg, '}'),
	MAP_ACTION(KEY_PASTE_BEGIN, paste_begin, 0, 0),
	MAP_ACTION(KEY_PASTE_END, paste_end, 0, 0),
	MAP_ACTION(-1, NULL, 0, 0),
};

struct action_map _REPLACE_MAP[] = {
	MAP_ACTION(KEY_ESCAPE,    leave_insert, 0, 0),
	MAP_ACTION(KEY_DELETE,    delete_forward, 0, 0),
	MAP_ACTION(KEY_BACKSPACE, cursor_left_with_wrap, 0, 0),
	MAP_ACTION(KEY_ENTER,     insert_line_feed, 0, 0),
	MAP_ACTION(-1, NULL, 0, 0),
};

struct action_map _LINE_SELECTION_MAP[] = {
	MAP_ACTION(KEY_ESCAPE,    leave_selection, 0, 0),
	MAP_ACTION(KEY_CTRL_C,    leave_selection, 0, 0),
	MAP_ACTION('V',           leave_selection, 0, 0),
	MAP_ACTION('v',           switch_selection_mode, opt_arg, MODE_CHAR_SELECTION),
	MAP_ACTION('y',           yank_lines, opt_norm, 0),
	MAP_ACTION(KEY_BACKSPACE, cursor_left_with_wrap, 0, 0),
	MAP_ACTION('\t',          adjust_indent, opt_arg | opt_rw, 1),
	MAP_ACTION(KEY_SHIFT_TAB, adjust_indent, opt_arg | opt_rw, -1),
	MAP_ACTION('D',           delete_and_yank_lines, opt_rw | opt_norm, 0),
	MAP_ACTION('d',           delete_and_yank_lines, opt_rw | opt_norm, 0),
	MAP_ACTION('x',           delete_and_yank_lines, opt_rw | opt_norm, 0),
	MAP_ACTION('s',           delete_lines_and_enter_insert, opt_rw, 0),
	MAP_ACTION('r',           replace_chars_in_line, opt_char | opt_rw, 0),

	MAP_ACTION(KEY_SHIFT_UP,   cursor_up, 0, 0),
	MAP_ACTION(KEY_SHIFT_DOWN, cursor_down, 0, 0),
	MAP_ACTION(-1, NULL, 0, 0),
};

struct action_map _CHAR_SELECTION_MAP[] = {
	MAP_ACTION(KEY_ESCAPE,    leave_selection, 0, 0),
	MAP_ACTION(KEY_CTRL_C,    leave_selection, 0, 0),
	MAP_ACTION('v',           leave_selection, 0, 0),
	MAP_ACTION('V',           switch_selection_mode, opt_arg, MODE_LINE_SELECTION),
	MAP_ACTION('y',           yank_characters, opt_norm, 0),
	MAP_ACTION(KEY_BACKSPACE, cursor_left_with_wrap, 0, 0),
	MAP_ACTION('D',           delete_and_yank_chars, opt_rw | opt_norm, 0),
	MAP_ACTION('d',           delete_and_yank_chars, opt_rw | opt_norm, 0),
	MAP_ACTION('x',           delete_and_yank_chars, opt_rw | opt_norm, 0),
	MAP_ACTION('s',           delete_chars_and_enter_insert, opt_rw, 0),
	MAP_ACTION('r',           replace_chars, opt_char | opt_rw, 0),
	MAP_ACTION('A',           insert_at_end_of_selection, opt_rw, 0),
	MAP_ACTION(-1, NULL, 0, 0),
};

struct action_map _COL_SELECTION_MAP[] = {
	MAP_ACTION(KEY_ESCAPE,    leave_selection, 0, 0),
	MAP_ACTION(KEY_CTRL_C,    leave_selection, 0, 0),
	MAP_ACTION(KEY_CTRL_V,    leave_selection, 0, 0),
	MAP_ACTION('I',           enter_col_insert, opt_rw, 0),
	MAP_ACTION('a',           enter_col_insert_after, opt_rw, 0),
	MAP_ACTION('d',           delete_at_column, opt_arg | opt_rw, 1),
	MAP_ACTION(-1, NULL, 0, 0),
};

struct action_map _COL_INSERT_MAP[] = {
	MAP_ACTION(KEY_ESCAPE,    leave_selection, 0, 0),
	MAP_ACTION(KEY_CTRL_C,    leave_selection, 0, 0),
	MAP_ACTION(KEY_BACKSPACE, delete_at_column, opt_arg, -1),
	MAP_ACTION(KEY_DELETE,    delete_at_column, opt_arg, 1),
	MAP_ACTION(KEY_ENTER,     NULL, 0, 0),
	MAP_ACTION(KEY_CTRL_W,    NULL, 0, 0),
	MAP_ACTION(KEY_CTRL_V,    insert_char_at_column, opt_char, 0),
	MAP_ACTION('\t',          insert_tab_at_column, 0, 0),
	MAP_ACTION(KEY_LEFT,      column_left, 0, 0),
	MAP_ACTION(KEY_RIGHT,     column_right, 0, 0),
	MAP_ACTION(KEY_UP,        column_up, 0, 0),
	MAP_ACTION(KEY_DOWN,      column_down, 0, 0),
	MAP_ACTION(-1, NULL, 0, 0),
};

struct action_map _NAVIGATION_MAP[] = {
	/* Common navigation */
	MAP_ACTION(KEY_CTRL_B,    go_page_up, opt_rep, 0),
	MAP_ACTION(KEY_CTRL_F,    go_page_down, opt_rep, 0),
	MAP_ACTION(':',           enter_command, 0, 0),
	MAP_ACTION('/',           enter_search, opt_arg, 1),
	MAP_ACTION('?',           enter_search, opt_arg, 0),
	MAP_ACTION('n',           search_next, opt_rep, 0),
	MAP_ACTION('N',           search_prev, opt_rep, 0),
	MAP_ACTION('j',           cursor_down, opt_rep, 0),
	MAP_ACTION('k',           cursor_up, opt_rep, 0),
	MAP_ACTION('h',           cursor_left, opt_rep, 0),
	MAP_ACTION('l',           cursor_right, opt_rep, 0),
	MAP_ACTION('b',           word_left, opt_rep, 0),
	MAP_ACTION('w',           word_right, opt_rep, 0),
	MAP_ACTION('B',           big_word_left, opt_rep, 0),
	MAP_ACTION('W',           big_word_right, opt_rep, 0),

	MAP_ACTION('<',           shift_horizontally, opt_arg, -1),
	MAP_ACTION('>',           shift_horizontally, opt_arg, 1),

	MAP_ACTION('f',           find_character_forward, opt_rep | opt_arg | opt_char, 'f'),
	MAP_ACTION('F',           find_character_backward, opt_rep | opt_arg | opt_char, 'F'),
	MAP_ACTION('t',           find_character_forward, opt_rep | opt_arg | opt_char, 't'),
	MAP_ACTION('T',           find_character_backward, opt_rep | opt_arg | opt_char, 'T'),

	MAP_ACTION('G',           goto_line, opt_nav, 0),
	MAP_ACTION('*',           search_under_cursor, 0, 0),
	MAP_ACTION(' ',           go_page_down, opt_rep, 0),
	MAP_ACTION('%',           jump_to_matching_bracket, 0, 0),
	MAP_ACTION('{',           jump_to_previous_blank, opt_rep, 0),
	MAP_ACTION('}',           jump_to_next_blank, opt_rep, 0),
	MAP_ACTION('$',           cursor_end, 0, 0),
	MAP_ACTION('|',           cursor_home, 0, 0),
	MAP_ACTION(KEY_ENTER,     next_line_non_whitespace, opt_rep, 0),
	MAP_ACTION('^',           first_non_whitespace, 0, 0),
	MAP_ACTION('0',           cursor_home, 0, 0),

	MAP_ACTION(-1, NULL, 0, 0),
};

struct action_map _ESCAPE_MAP[] = {
	MAP_ACTION(KEY_F1,        toggle_numbers, 0, 0),
	MAP_ACTION(KEY_F2,        toggle_indent, 0, 0),
	MAP_ACTION(KEY_F3,        toggle_gutter, 0, 0),
	MAP_ACTION(KEY_F4,        toggle_smartcomplete, 0, 0),
	MAP_ACTION(KEY_MOUSE,     handle_mouse, 0, 0),
	MAP_ACTION(KEY_MOUSE_SGR, handle_mouse_sgr, 0, 0),

	MAP_ACTION(KEY_UP,        cursor_up, opt_rep, 0),
	MAP_ACTION(KEY_DOWN,      cursor_down, opt_rep, 0),

	MAP_ACTION(KEY_RIGHT,     cursor_right, opt_rep, 0),
	MAP_ACTION(KEY_CTRL_RIGHT, big_word_right, opt_rep, 0),
	MAP_ACTION(KEY_SHIFT_RIGHT, word_right, opt_rep, 0),
	MAP_ACTION(KEY_ALT_RIGHT, expand_split_right, opt_rep, 0),
	MAP_ACTION(KEY_ALT_SHIFT_RIGHT, use_right_buffer, opt_rep, 0),

	MAP_ACTION(KEY_LEFT,      cursor_left, opt_rep, 0),
	MAP_ACTION(KEY_CTRL_LEFT, big_word_left, opt_rep, 0),
	MAP_ACTION(KEY_SHIFT_LEFT, word_left, opt_rep, 0),
	MAP_ACTION(KEY_ALT_LEFT, expand_split_left, opt_rep, 0),
	MAP_ACTION(KEY_ALT_SHIFT_LEFT, use_left_buffer, opt_rep, 0),

	MAP_ACTION(KEY_HOME, cursor_home, 0, 0),
	MAP_ACTION(KEY_END, cursor_end, 0, 0),
	MAP_ACTION(KEY_PAGE_UP, go_page_up, opt_rep, 0),
	MAP_ACTION(KEY_PAGE_DOWN, go_page_down, opt_rep, 0),

	MAP_ACTION(KEY_CTRL_Z,   suspend, 0, 0),

	MAP_ACTION(-1, NULL, 0, 0)
};

struct action_map _COMMAND_MAP[] = {
	MAP_ACTION(KEY_ENTER,     command_accept, 0, 0),
	MAP_ACTION('\t',          command_tab_complete_buffer, 0, 0),
	MAP_ACTION(KEY_UP,        command_scroll_history, opt_arg, -1), /* back */
	MAP_ACTION(KEY_DOWN,      command_scroll_history, opt_arg, 1), /* forward */

	MAP_ACTION(-1, NULL, 0, 0)
};

struct action_map _FILESEARCH_MAP[] = {
	MAP_ACTION(KEY_ENTER,    file_search_accept, 0, 0),

	MAP_ACTION(KEY_UP,       NULL, 0, 0),
	MAP_ACTION(KEY_DOWN,     NULL, 0, 0),

	MAP_ACTION(-1, NULL, 0, 0)
};

struct action_map _SEARCH_MAP[] = {
	MAP_ACTION(KEY_ENTER,    search_accept, 0, 0),

	MAP_ACTION(KEY_UP,        command_scroll_search_history, opt_arg, -1), /* back */
	MAP_ACTION(KEY_DOWN,      command_scroll_search_history, opt_arg, 1), /* forward */

	MAP_ACTION(-1, NULL, 0, 0)
};

struct action_map _INPUT_BUFFER_MAP[] = {
	/* These are generic and shared with search */
	MAP_ACTION(KEY_ESCAPE,    command_discard, 0, 0),
	MAP_ACTION(KEY_CTRL_C,    command_discard, 0, 0),
	MAP_ACTION(KEY_BACKSPACE, command_backspace, 0, 0),
	MAP_ACTION(KEY_CTRL_W,    command_word_delete, 0, 0),
	MAP_ACTION(KEY_MOUSE,     eat_mouse, 0, 0),
	MAP_ACTION(KEY_MOUSE_SGR, eat_mouse_sgr, 0, 0),
	MAP_ACTION(KEY_LEFT,      command_cursor_left, 0, 0),
	MAP_ACTION(KEY_CTRL_LEFT, command_word_left, 0, 0),
	MAP_ACTION(KEY_RIGHT,     command_cursor_right, 0, 0),
	MAP_ACTION(KEY_CTRL_RIGHT,command_word_right, 0, 0),
	MAP_ACTION(KEY_HOME,      command_cursor_home, 0, 0),
	MAP_ACTION(KEY_END,       command_cursor_end, 0, 0),

	MAP_ACTION(-1, NULL, 0, 0)
};

/* DIRECTORY_BROWSE_MAP is only to override KEY_ENTER and should not be remapped,
 * so unlike the others it is not going to be redefined as a pointer. */
struct action_map DIRECTORY_BROWSE_MAP[] = {
	MAP_ACTION(KEY_ENTER,     open_file_from_line, 0, 0),
	MAP_ACTION(-1, NULL, 0, 0)
};

struct action_map * NORMAL_MAP = NULL;
struct action_map * INSERT_MAP = NULL;
struct action_map * REPLACE_MAP = NULL;
struct action_map * LINE_SELECTION_MAP = NULL;
struct action_map * CHAR_SELECTION_MAP = NULL;
struct action_map * COL_SELECTION_MAP = NULL;
struct action_map * COL_INSERT_MAP = NULL;
struct action_map * NAVIGATION_MAP = NULL;
struct action_map * ESCAPE_MAP = NULL;
struct action_map * COMMAND_MAP = NULL;
struct action_map * SEARCH_MAP = NULL;
struct action_map * FILESEARCH_MAP = NULL;
struct action_map * INPUT_BUFFER_MAP = NULL;

struct mode_names mode_names[] = {
	{"Normal","norm",&NORMAL_MAP},
	{"Insert","insert",&INSERT_MAP},
	{"Replace","replace",&REPLACE_MAP},
	{"Line Selection","line",&LINE_SELECTION_MAP},
	{"Char Selection","char",&CHAR_SELECTION_MAP},
	{"Col Selection","col",&COL_SELECTION_MAP},
	{"Col Insert","colinsert",&COL_INSERT_MAP},
	{"Navigation (Select)","nav",&NAVIGATION_MAP},
	{"Escape (Select, Insert)","esc",&ESCAPE_MAP},
	{"Command","command",&COMMAND_MAP},
	{"Search","search",&SEARCH_MAP},
	{"Input (Command, Search)","input",&INPUT_BUFFER_MAP},
	{NULL,NULL,NULL},
};

typedef void (*action_no_arg)(void);
typedef void (*action_one_arg)(int);
typedef void (*action_two_arg)(int,int);
typedef void (*action_three_arg)(int,int,int);

int handle_action(struct action_map * basemap, int key) {
	for (struct action_map * map = basemap; map->key != -1; map++) {
		if (map->key == key) {
			if (!map->method) return 1;
			if ((map->options & opt_rw) && (env->readonly)) {
				render_error("Buffer is read-only");
				return 2;
			}
			/* Determine how to format this request */
			int reps = (map->options & opt_rep) ? ((nav_buffer) ? atoi(nav_buf) : 1) : 1;
			int c = 0;
			if (map->options & opt_char) {
				c = read_one_character(name_from_key(key));
			}
			if (map->options & opt_byte) {
				c = read_one_byte(name_from_key(key));
			}
			for (int i = 0; i < reps; ++i) {
				if (((map->options & opt_char) || (map->options & opt_byte)) && (map->options & opt_arg)) {
					((action_two_arg)map->method)(map->arg, c);
				} else if ((map->options & opt_char) || (map->options & opt_byte)) {
					((action_one_arg)map->method)(c);
				} else if (map->options & opt_arg) {
					((action_one_arg)map->method)(map->arg);
				} else if (map->options & opt_nav) {
					if (nav_buffer) {
						((action_one_arg)map->method)(atoi(nav_buf));
						reset_nav_buffer(0);
					} else {
						((action_one_arg)map->method)(-1);
					}
				} else if (map->options & opt_krk) {
					ptrdiff_t before = krk_currentThread.stackTop - krk_currentThread.stack;
					krk_push(map->callable);
					krk_push(INTEGER_VAL(key));
					krk_push(map->callable);
					KrkValue result = krk_callStack(2);
					krk_currentThread.stackTop = krk_currentThread.stack + before;
					if (IS_NONE(result) && (krk_currentThread.flags & KRK_THREAD_HAS_EXCEPTION)) {
						render_error("Exception during action: %s", AS_INSTANCE(krk_currentThread.currentException)->_class->name->chars);
						krk_dumpTraceback();
						int key = 0;
						while ((key = bim_getkey(DEFAULT_KEY_WAIT)) == KEY_TIMEOUT);
					}
				} else {
					((action_no_arg)map->method)();
				}
			}
			if (map->options & opt_norm) {
				if (env->mode == MODE_INSERT || env->mode == MODE_REPLACE) leave_insert();
				else if (env->mode == MODE_LINE_SELECTION || env->mode == MODE_CHAR_SELECTION || env->mode == MODE_COL_SELECTION) leave_selection();
				else {
					env->mode = MODE_NORMAL;
					redraw_all();
				}
			}
			return 1;
		}
	}

	return 0;
}

int handle_nav_buffer(int key) {
	if ((key >= '1' && key <= '9') || (key == '0' && nav_buffer)) {
		if (nav_buffer < NAV_BUFFER_MAX) {
			/* Up to NAV_BUFFER_MAX=10 characters; that should be enough for most tasks */
			nav_buf[nav_buffer] = key;
			nav_buf[nav_buffer+1] = 0;
			nav_buffer++;
			/* Print the number buffer */
			redraw_commandline();
		}
		return 0;
	}
	return 1;
}

/**
 * NORMAL mode
 *
 * Default editor mode - just cursor navigation and keybinds
 * to enter the other modes.
 */
void normal_mode(void) {

	int last_mode = MODE_NORMAL;
	int refresh = 0;

	while (1) {

		if (global_config.overlay_mode) {
			if (global_config.overlay_mode == OVERLAY_MODE_COMMAND) {
				if (refresh) {
					render_command_input_buffer();
					refresh = 0;
				}
				int key = bim_getkey(DEFAULT_KEY_WAIT);
				if (key != KEY_TIMEOUT) {
					refresh = 1;
					if (!handle_action(COMMAND_MAP, key))
						if (!handle_action(INPUT_BUFFER_MAP, key))
							if (key < KEY_ESCAPE) command_insert_char(key);
				}
				continue;
			} else if (global_config.overlay_mode == OVERLAY_MODE_FILESEARCH) {
				if (refresh) {
					redraw_tabbar();
					render_command_input_buffer();
					refresh = 0;
				}
				int key = bim_getkey(DEFAULT_KEY_WAIT);
				if (key != KEY_TIMEOUT) {
					refresh = 1;
					if (!handle_action(FILESEARCH_MAP, key))
						if (!handle_action(INPUT_BUFFER_MAP, key))
							if (key < KEY_ESCAPE) command_insert_char(key);
				}
				continue;
			} else if (global_config.overlay_mode == OVERLAY_MODE_SEARCH) {
				if (refresh) {
					render_command_input_buffer();
					refresh = 0;
				}
				int key = bim_getkey(DEFAULT_KEY_WAIT);
				if (key != KEY_TIMEOUT) {
					refresh = 1;
					if (!handle_action(SEARCH_MAP, key)) {
						if (!handle_action(INPUT_BUFFER_MAP, key)) {
							if (key < KEY_ESCAPE) command_insert_char(key);
						}
					}

					if (global_config.overlay_mode == OVERLAY_MODE_SEARCH) {
						/* Find the next search match */
						uint32_t * buffer = malloc(sizeof(uint32_t) * (global_config.command_buffer->actual+1));
						for (int i = 0; i < global_config.command_buffer->actual; ++i) {
							buffer[i] = global_config.command_buffer->text[i].codepoint;
						}
						buffer[global_config.command_buffer->actual] = 0;
						int line = -1, col = -1;
						if (global_config.search_direction == 1) {
							find_match(global_config.prev_line, global_config.prev_col, &line, &col, buffer, NULL);
							if (line == -1 && global_config.search_wraps) {
								find_match(1, 1, &line, &col, buffer, NULL);
							}
						} else {
							find_match_backwards(global_config.prev_line, global_config.prev_col, &line, &col, buffer);
							if (line == -1 && global_config.search_wraps) {
								find_match_backwards(env->line_count, env->lines[env->line_count-1]->actual, &line, &col, buffer);
							}
						}

						if (line != -1) {
							env->col_no = col;
							env->line_no = line;
							set_preferred_column();
						} else {
							env->coffset = global_config.prev_coffset;
							env->offset = global_config.prev_offset;
							env->col_no = global_config.prev_col;
							set_preferred_column();
							env->line_no = global_config.prev_line;
						}
						draw_search_match(buffer, 0);

						free(buffer);
					}
				}
				continue;
			}
		}

		if (env->mode != last_mode) {
			redraw_statusbar();
			redraw_commandline();
			last_mode = env->mode;
		}

		if (env->mode == MODE_NORMAL) {
			place_cursor_actual();
			int key = 0;
			do {
				key = bim_getkey(DEFAULT_KEY_WAIT);
			} while (key == KEY_TIMEOUT);
			if (handle_nav_buffer(key)) {
				if (!handle_action(NORMAL_MAP, key))
					if (!handle_action(NAVIGATION_MAP, key))
						handle_action(ESCAPE_MAP, key);
			}
			reset_nav_buffer(key);
		} else if (env->mode == MODE_INSERT) {
			if (!refresh) place_cursor_actual();
			int key = bim_getkey(refresh ? 10 : DEFAULT_KEY_WAIT);
			if (key == KEY_TIMEOUT) {
				place_cursor_actual();
				if (refresh > 1) {
					redraw_text();
				} else if (refresh) {
					redraw_line(env->line_no-1);
				}
				refresh = 0;
			} else if (handle_action(INSERT_MAP, key)) {
				refresh = 2;
			} else if (handle_action(ESCAPE_MAP, key)) {
				/* Do nothing */
			} else if (key < KEY_ESCAPE) {
				insert_char(key);
				if (global_config.smart_complete) {
					redraw_line(env->line_no-1);
					while (omni_complete(1) == 1);
				}
				refresh |= 1;
			}
		} else if (env->mode == MODE_REPLACE) {
			place_cursor_actual();
			int key = bim_getkey(DEFAULT_KEY_WAIT);
			if (key != KEY_TIMEOUT) {
				if (handle_action(REPLACE_MAP, key)) {
					redraw_text();
				} else if (!handle_action(ESCAPE_MAP, key)) {
					/* Perform replacement */
					if (key < KEY_ESCAPE) {
						if (env->col_no <= env->lines[env->line_no - 1]->actual) {
							replace_char(key);
							env->col_no += 1;
						} else {
							insert_char(key);
							redraw_line(env->line_no-1);
						}
						set_preferred_column();
					}
				}
			}
		} else if (env->mode == MODE_LINE_SELECTION) {
			place_cursor_actual();
			int key = bim_getkey(DEFAULT_KEY_WAIT);
			if (key == KEY_TIMEOUT) continue;

			if (handle_nav_buffer(key)) {
				if (!handle_action(LINE_SELECTION_MAP, key))
					if (!handle_action(NAVIGATION_MAP, key))
						handle_action(ESCAPE_MAP, key);
			}

			reset_nav_buffer(key);

			if (env->mode == MODE_LINE_SELECTION) {
				/* Mark current line */
				_redraw_line(env->line_no,0);
				_redraw_line(env->start_line,1);

				/* Properly mark everything in the span we just moved through */
				if (env->prev_line < env->line_no) {
					for (int i = env->prev_line; i < env->line_no; ++i) {
						_redraw_line(i,0);
					}
					env->prev_line = env->line_no;
				} else if (env->prev_line > env->line_no) {
					for (int i = env->line_no + 1; i <= env->prev_line; ++i) {
						_redraw_line(i,0);
					}
					env->prev_line = env->line_no;
				}
				redraw_commandline();
			}
		} else if (env->mode == MODE_CHAR_SELECTION) {
			place_cursor_actual();
			int key = bim_getkey(DEFAULT_KEY_WAIT);
			if (key == KEY_TIMEOUT) continue;

			if (handle_nav_buffer(key)) {
				if (!handle_action(CHAR_SELECTION_MAP, key))
					if (!handle_action(NAVIGATION_MAP, key))
						handle_action(ESCAPE_MAP, key);
			}

			reset_nav_buffer(key);

			if (env->mode == MODE_CHAR_SELECTION) {
				/* Mark current line */
				_redraw_line_char(env->line_no,1);

				/* Properly mark everything in the span we just moved through */
				if (env->prev_line < env->line_no) {
					for (int i = env->prev_line; i < env->line_no; ++i) {
						_redraw_line_char(i,1);
					}
					env->prev_line = env->line_no;
				} else if (env->prev_line > env->line_no) {
					for (int i = env->line_no + 1; i <= env->prev_line; ++i) {
						_redraw_line_char(i,1);
					}
					env->prev_line = env->line_no;
				}
			}
		} else if (env->mode == MODE_COL_SELECTION) {
			place_cursor_actual();
			int key = bim_getkey(DEFAULT_KEY_WAIT);
			if (key == KEY_TIMEOUT) continue;

			if (handle_nav_buffer(key)) {
				if (!handle_action(COL_SELECTION_MAP, key))
					if (!handle_action(NAVIGATION_MAP, key))
						handle_action(ESCAPE_MAP, key);
			}

			reset_nav_buffer(key);

			if (env->mode == MODE_COL_SELECTION) {
				_redraw_line_col(env->line_no, 0);
				/* Properly mark everything in the span we just moved through */
				if (env->prev_line < env->line_no) {
					for (int i = env->prev_line; i < env->line_no; ++i) {
						_redraw_line_col(i,0);
					}
					env->prev_line = env->line_no;
				} else if (env->prev_line > env->line_no) {
					for (int i = env->line_no + 1; i <= env->prev_line; ++i) {
						_redraw_line_col(i,0);
					}
					env->prev_line = env->line_no;
				}

				redraw_commandline();
			}
		} else if (env->mode == MODE_COL_INSERT) {
			int key = bim_getkey(refresh ? 10 : DEFAULT_KEY_WAIT);
			if (key == KEY_TIMEOUT) {
				if (refresh) {
					redraw_commandline();
					redraw_text();
				}
				refresh = 0;
			} else if (handle_action(COL_INSERT_MAP, key)) {
				refresh = 2;
			} else if (key < KEY_ESCAPE) {
				insert_char_at_column(key);
				refresh = 1;
			}
		} if (env->mode == MODE_DIRECTORY_BROWSE) {
			place_cursor_actual();
			int key = bim_getkey(DEFAULT_KEY_WAIT);
			if (handle_nav_buffer(key)) {
				if (!handle_action(DIRECTORY_BROWSE_MAP, key))
					if (!handle_action(NAVIGATION_MAP, key))
						handle_action(ESCAPE_MAP, key);
			}
			reset_nav_buffer(key);
		}
	}

}

KrkClass * CommandDef;
struct CommandDef {
	KrkInstance inst;
	struct command_def * command;
};

int process_krk_command(const char * cmd, KrkValue * outVal) {
	place_cursor(global_config.term_width, global_config.term_height);
	fprintf(stdout, "\n");
	/* By resetting, we're at 0 frames. */
	krk_resetStack();
	/* Push something so we're not at the bottom of the stack when an
	 * exception happens, or we'll get the normal interpreter behavior
	 * and won't be able to examine the exception ourselves. */
	krk_push(NONE_VAL());
	/* If we don't set outSlots for the top frame a syntax error will
	 * get printed by the interpreter and we can't catch it here. */
	krk_currentThread.frames[0].outSlots = 1;
	/* Call the interpreter */
	KrkValue out = krk_interpret(cmd,"<bim>");
	/* If the user typed just a command name, try to execute it. */
	if (krk_isInstanceOf(out,CommandDef)) {
		krk_push(out);
		out = krk_callStack(0);
	}
	if (outVal) *outVal = out;
	int retval = (IS_INTEGER(out)) ? AS_INTEGER(out) : 0;
	int hadOutput = 0;
	/* If we got an exception during execution, print it now */
	if (krk_currentThread.flags & KRK_THREAD_HAS_EXCEPTION) {
		if (krk_isInstanceOf(krk_currentThread.currentException, vm.exceptions->syntaxError)) {
		}
		set_fg_color(COLOR_RED);
		fflush(stdout);
		krk_dumpTraceback();
		set_fg_color(COLOR_FG);
		fflush(stdout);
		hadOutput = 1;
		krk_resetStack();
	}
	/* Otherwise, we can look at the result here. */
	if (!IS_NONE(out) && !(IS_INTEGER(out) && AS_INTEGER(out) == 0)) {
		krk_attachNamedValue(&vm.builtins->fields, "_", out);
		krk_push(out);
		KrkValue repr = krk_callDirect(krk_getType(out)->_reprer, 1);
		if (IS_STRING(repr)) {
			fprintf(stdout, " => %s\n", AS_CSTRING(repr));
			clear_to_end();
		}
		krk_resetStack();
		hadOutput = 1;
	}
	/* If we had either an exception or a non-zero, non-None result,
	 * we want to wait for a key press before continuing, and avoid
	 * clearing the screen if the user is going to enter another command. */
	if (hadOutput) {
		int c;
		while ((c = bim_getch())== -1);
		if (c != ':') {
			bim_unget(c);
		} else {
			enter_command();
			global_config.command_offset = 0;
			global_config.command_col_no = 1;
			render_command_input_buffer();
			return retval;
		}
	}
	global_config.break_from_selection = 1;
	if (!global_config.had_error) redraw_all();
	global_config.had_error = 0;
	return retval;
}

/**
 * Show help text for -?
 */
static void show_usage(char * argv[]) {
#define _s "\033[3m"
#define _e "\033[0m\n"
	printf(
			"bim - Text editor\n"
			"\n"
			"usage: %s [options] [file]\n"
			"       %s [options] -- -\n"
			"\n"
			" -R     " _s "open initial buffer read-only" _e
			" -O     " _s "set various options; examples:" _e
			"        noaltscreen " _s "disable alternate screen buffer" _e
			"        nounicode   " _s "disable unicode display" _e
			"        nosyntax    " _s "disable syntax highlighting on load" _e
			"        nohistory   " _s "disable undo/redo" _e
			"        nomouse     " _s "disable mouse support" _e
			"        cansgrmouse " _s "enable SGR mouse escape sequences" _e
			" -c,-C  " _s "print file to stdout with syntax highlighting" _e
			"        " _s "-C includes line numbers, -c does not" _e
			" -u     " _s "override bimrc file" _e
			" -?     " _s "show this help text" _e
			"\n"
			"Long options:\n"
			" --help          " _s "show this help text" _e
			" --version       " _s "show version information and available plugins" _e
			" --dump-mappings " _s "dump markdown description of key mappings" _e
			" --dump-commands " _s "dump markdown description of all commands" _e
			" --dump-config   " _s "dump key mappings as a bimscript" _e
			" --html FILE     " _s "convert FILE to syntax-highlighted HTML" _e
			"\n", argv[0], argv[0]);
#undef _e
#undef _s
}

BIM_COMMAND(runkrk,"runkrk", "Run a kuroko script") {
	if (argc < 2) return 1;
	krk_runfile(argv[1],argv[1]);
	redraw_all();
	return 0;
}

/**
 * Enable or disable terminal features/quirks and other options.
 * Used by -O and by the `quirks` bimrc command.
 */
int set_capability(char * arg) {
	char * argname;
	int value;
	if (strstr(arg,"no") == arg) {
		argname = &arg[2];
		value = 0;
	} else if (strstr(arg,"can") == arg) {
		argname = &arg[3];
		value = 1;
	} else {
		render_error("Capabilities must by 'no{CAP}' or 'can{CAP}': %s", arg);
		return 2;
	}

	/* Terminal features / quirks */
	if (!strcmp(argname, "24bit")) global_config.can_24bit = value;
	else if (!strcmp(argname, "256color")) global_config.can_256color = value;
	else if (!strcmp(argname, "altscreen")) global_config.can_altscreen = value;
	else if (!strcmp(argname, "bce")) global_config.can_bce = value;
	else if (!strcmp(argname, "bright")) global_config.can_bright = value;
	else if (!strcmp(argname, "hideshow")) global_config.can_hideshow = value;
	else if (!strcmp(argname, "italic")) global_config.can_italic = value;
	else if (!strcmp(argname, "mouse")) global_config.can_mouse = value;
	else if (!strcmp(argname, "scroll")) global_config.can_scroll = value;
	else if (!strcmp(argname, "title")) global_config.can_title = value;
	else if (!strcmp(argname, "unicode")) global_config.can_unicode = value;
	else if (!strcmp(argname, "insert")) global_config.can_insert = value;
	else if (!strcmp(argname, "paste")) global_config.can_bracketedpaste = value;
	else if (!strcmp(argname, "sgrmouse")) global_config.can_sgrmouse = value;
	/* Startup options */
	else if (!strcmp(argname, "syntax")) global_config.highlight_on_open = value;
	else if (!strcmp(argname, "history")) global_config.history_enabled = value;
	else {
		render_error("Unknown capability: %s", argname);
		return 1;
	}
	return 0;
}

BIM_COMMAND(setcap, "setcap", "Enable or disable quirks/features.") {
	for (int i = 1; i < argc; ++i) {
		if (set_capability(argv[i])) return 1;
	}
	return 0;
}

BIM_COMMAND(quirk,"quirk","Handle quirks based on environment variables") {
	if (argc < 3) goto _quirk_arg_error;
	char * varname = argv[1];
	char * teststr = argv[2];
	char * value = getenv(varname);
	if (!value) return 0;

	if (strstr(value, teststr) == value) {
		/* Process quirk strings */
		for (int i = 3; i < argc; ++i) {
			set_capability(argv[i]);
		}
	}

	return 0;
_quirk_arg_error:
	render_error("Usage: quirk ENVVAR value no... can...");
	return 1;
}

/**
 * Load bimrc configuration file.
 *
 * At the moment, this a simple key=value list.
 */
void load_bimrc(void) {
	if (!global_config.bimrc_path) return;

	/* Default is ~/.bimrc */
	char * tmp = strdup(global_config.bimrc_path);

	if (!*tmp) {
		free(tmp);
		return;
	}

	/* Parse ~ at the front of the path. */
	if (*tmp == '~') {
		char path[1024] = {0};
		char * home = getenv("HOME");
		if (!home) {
			/* $HOME is unset? */
			free(tmp);
			return;
		}

		/* New path is $HOME/.bimrc */
		snprintf(path, 1024, "%s%s", home, tmp+1);
		free(tmp);
		tmp = strdup(path);
	}

	struct stat statbuf;
	if (stat(tmp, &statbuf)) {
		free(tmp);
		return;
	}

	krk_runfile(tmp,tmp);
	free(tmp);
}

static int checkClass(KrkClass * _class, KrkClass * base) {
	while (_class) {
		if (_class == base) return 1;
		_class = _class->base;
	}
	return 0;
}

static KrkValue krk_bim_syntax_dict;
static KrkValue krk_bim_register_syntax(int argc, const KrkValue argv[], int hasKw) {
	if (argc < 1 || !IS_CLASS(argv[0]) || !checkClass(AS_CLASS(argv[0]), syntaxStateClass))
		return krk_runtimeError(vm.exceptions->typeError, "Can not register '%s' as a syntax highlighter, expected subclass of SyntaxState.", krk_typeName(argv[0]));

	KrkValue name = krk_valueGetAttribute_default(argv[0], "name", NONE_VAL());
	KrkValue extensions = krk_valueGetAttribute_default(argv[0], "extensions", NONE_VAL());
	KrkValue spaces = krk_valueGetAttribute_default(argv[0], "spaces", BOOLEAN_VAL(0));
	KrkValue calculate = krk_valueGetAttribute_default(argv[0], "calculate", NONE_VAL());

	if (!IS_STRING(name))
		return krk_runtimeError(vm.exceptions->typeError, "%s.name must be str", AS_CLASS(argv[0])->name->chars);
	if (!IS_TUPLE(extensions))
		return krk_runtimeError(vm.exceptions->typeError, "%s.extensions must be tuple<str>", AS_CLASS(argv[0])->name->chars);
	if (!IS_BOOLEAN(spaces))
		return krk_runtimeError(vm.exceptions->typeError, "%s.spaces must be bool", AS_CLASS(argv[0])->name->chars);
	if (!IS_CLOSURE(calculate))
		return krk_runtimeError(vm.exceptions->typeError, "%s.calculate must be method, not '%s'", AS_CLASS(argv[0])->name->chars, krk_typeName(calculate));

	/* Convert tuple of strings */
	char ** ext = malloc(sizeof(char *) * (AS_TUPLE(extensions)->values.count + 1)); /* +1 for NULL */
	ext[AS_TUPLE(extensions)->values.count] = NULL;
	for (size_t i = 0; i < AS_TUPLE(extensions)->values.count; ++i) {
		if (!IS_STRING(AS_TUPLE(extensions)->values.values[i])) {
			free(ext);
			return krk_runtimeError(vm.exceptions->typeError, "%s.extensions must by tuple<str>", AS_CLASS(argv[0])->name->chars);
		}
		ext[i] = AS_CSTRING(AS_TUPLE(extensions)->values.values[i]);
	}

	add_syntax((struct syntax_definition) {
		AS_CSTRING(name), /* name */
		ext, /* NULL-terminated array of extensions */
		NULL, /* calculate function */
		AS_BOOLEAN(spaces), /* spaces */
		NULL, /* qualifier */
		NULL, /* matcher */
		AS_OBJECT(calculate), /* krkFunc */
		AS_OBJECT(argv[0]),
	});

	/* And save it in the module stuff. */
	krk_tableSet(AS_DICT(krk_bim_syntax_dict), name, argv[0]);

	return NONE_VAL();
}

static KrkValue krk_bim_theme_dict;
static KrkValue krk_bim_define_theme(int argc, const KrkValue argv[], int hasKw) {
	if (argc < 1 || !IS_CLOSURE(argv[0]))
		return krk_runtimeError(vm.exceptions->typeError, "themes must be functions, not '%s'", krk_typeName(argv[0]));

	KrkValue name = OBJECT_VAL(AS_CLOSURE(argv[0])->function->name);

	add_colorscheme((struct theme_def) {
		AS_CSTRING(name),
		AS_OBJECT(argv[0]),
	});

	krk_tableSet(AS_DICT(krk_bim_theme_dict), name, argv[0]);
	return argv[0];
}

static int c_keyword_qualifier(int c) {
	return isalnum(c) || (c == '_');
}

#define BIM_STATE() \
	if (unlikely(argc < 1 || !krk_isInstanceOf(argv[0],syntaxStateClass))) return krk_runtimeError(vm.exceptions->typeError, "expected state"); \
	KrkInstance * _self = AS_INSTANCE(argv[0]); \
	struct SyntaxState * self = (struct SyntaxState*)_self; \
	struct syntax_state * state = &self->state;

static KrkTuple * _bim_state_chars = NULL;

static KrkValue bim_krk_state_getstate(int argc, const KrkValue argv[], int hasKw) {
	BIM_STATE();
	return INTEGER_VAL(state->state);
}
static KrkValue bim_krk_state_setstate(int argc, const KrkValue argv[], int hasKw) {
	BIM_STATE();
	if (argc < 2 || !IS_INTEGER(argv[1])) return NONE_VAL();
	state->state = AS_INTEGER(argv[1]);
	return INTEGER_VAL(state->state);
}
static KrkValue bim_krk_state_index(int argc, const KrkValue argv[], int hasKw) {
	BIM_STATE();
	return INTEGER_VAL(state->i);
}
static KrkValue bim_krk_state_lineno(int argc, const KrkValue argv[], int hasKw) {
	BIM_STATE();
	return INTEGER_VAL(state->line_no);
}
static KrkValue bim_krk_state_get(int argc, const KrkValue argv[], int hasKw) {
	BIM_STATE();

	/* non-slice item */
	if (IS_INTEGER(argv[1])) {
		long arg = AS_INTEGER(argv[1]);
		int charRel = charrel(arg);
		if (charRel == -1) return NONE_VAL();
		if (charRel >= 32 && charRel <= 126) return _bim_state_chars->values.values[charRel - 32];
		char tmp[8] = {0};
		size_t len = to_eight(charRel, tmp);
		return OBJECT_VAL(krk_copyString(tmp,len));
	} else if (IS_slice(argv[1])) {
		struct StringBuilder sb = {0};

		extern int krk_extractSlicer(const char * _method_name, KrkValue slicerVal, krk_integer_type count, krk_integer_type *start, krk_integer_type *end, krk_integer_type *step);
		krk_integer_type start, end, step;
		if (krk_extractSlicer("__getitem__", argv[1], state->line->actual - state->i, &start, &end, &step)) {
			return NONE_VAL();
		}

		krk_integer_type i = start;

		while ((step < 0) ? (i > end) : (i < end)) {
			int charRel = charrel(i);
			if (charRel == -1) break;
			char tmp[8] = {0};
			size_t len = to_eight(charRel, tmp);
			pushStringBuilderStr(&sb, tmp, len);
			i += step;
		}

		return finishStringBuilder(&sb);
	} else {
		return krk_runtimeError(vm.exceptions->typeError, "%s() expects %s, not '%s'",
			"__getitem__", "int or slice", krk_typeName(argv[1]));
	}
}
static KrkValue bim_krk_state_isdigit(int argc, const KrkValue argv[], int hasKw) {
	if (IS_NONE(argv[1])) return BOOLEAN_VAL(0);
	if (!IS_STRING(argv[1])) {
		krk_runtimeError(vm.exceptions->typeError, "not a string: %s", krk_typeName(argv[1]));
		return BOOLEAN_VAL(0);
	}
	if (AS_STRING(argv[1])->codesLength > 1) {
		krk_runtimeError(vm.exceptions->typeError, "arg must be str of len 1");
		return BOOLEAN_VAL(0);
	}
	unsigned int c = krk_unicodeCodepoint(AS_STRING(argv[1]), 0);
	return BOOLEAN_VAL(!!isdigit(c));
}
static KrkValue bim_krk_state_isxdigit(int argc, const KrkValue argv[], int hasKw) {
	if (!IS_STRING(argv[1])) return BOOLEAN_VAL(0);
	if (AS_STRING(argv[1])->length > 1) return BOOLEAN_VAL(0);
	int c = AS_CSTRING(argv[1])[0];
	return BOOLEAN_VAL(!!isxdigit(c));
}
static KrkValue bim_krk_state_paint(int argc, const KrkValue argv[], int hasKw) {
	/* self.paint(count, color)
	 * or
	 * self[count] = color
	 */
	BIM_STATE();
	long howMuch = AS_INTEGER(argv[1]);
	if (howMuch == -1) howMuch = state->line->actual;
	long whatFlag = AS_INTEGER(argv[2]);
	paint(howMuch, whatFlag);
	return NONE_VAL();
}
#define KRK_STRING_FAST(string,offset)  (uint32_t)\
	((string->obj.flags & KRK_OBJ_FLAGS_STRING_MASK) <= (KRK_OBJ_FLAGS_STRING_UCS1) ? ((uint8_t*)string->codes)[offset] : \
	((string->obj.flags & KRK_OBJ_FLAGS_STRING_MASK) == (KRK_OBJ_FLAGS_STRING_UCS2) ? ((uint16_t*)string->codes)[offset] : \
	((uint32_t*)string->codes)[offset]))
static KrkValue bim_krk_state_check(int argc, const KrkValue argv[], int hasKw) {
	/* 'string' in self */
	BIM_STATE();
	int c = charrel(0);

	if (IS_NONE(argv[1])) return BOOLEAN_VAL((c == -1));
	if (!IS_STRING(argv[1])) return krk_runtimeError(vm.exceptions->typeError, "expected string");

	KrkString * s = AS_STRING(argv[1]);
	krk_unicodeString(s);

	for (size_t i = 0; i < s->codesLength; ++i) {
		int cp = (int)KRK_STRING_FAST(s,i);
		if (c == cp) return BOOLEAN_VAL(1);
	}
	return BOOLEAN_VAL(0);
}

static KrkValue bim_krk_state_paintComment(int argc, const KrkValue argv[], int hasKw) {
	BIM_STATE();
	paint_comment(state);
	return NONE_VAL();
}
static KrkValue bim_krk_state_skip(int argc, const KrkValue argv[], int hasKw) {
	BIM_STATE();
	skip();
	return NONE_VAL();
}
static KrkValue bim_krk_state_cKeywordQualifier(int argc, const KrkValue argv[], int hasKw) {
	if (IS_INTEGER(argv[0])) return BOOLEAN_VAL(!!c_keyword_qualifier(AS_INTEGER(argv[0])));
	if (!IS_STRING(argv[0])) return BOOLEAN_VAL(0);
	if (AS_STRING(argv[0])->length > 1) return BOOLEAN_VAL(0);
	return BOOLEAN_VAL(!!c_keyword_qualifier(AS_CSTRING(argv[0])[0]));
}

static int callQualifier(KrkValue qualifier, int codepoint) {
	if (IS_NATIVE(qualifier) && AS_NATIVE(qualifier)->function == bim_krk_state_cKeywordQualifier) return AS_BOOLEAN(!!c_keyword_qualifier(codepoint));
	krk_push(qualifier);
	krk_push(INTEGER_VAL(codepoint));
	KrkValue result = krk_callStack(1);
	if (IS_BOOLEAN(result)) return AS_BOOLEAN(result);
	return 0;
}

static KrkValue bim_krk_state_findKeywords(int argc, const KrkValue argv[], int hasKw) {
	BIM_STATE();
	if (unlikely(argc < 4 || !(IS_INSTANCE(argv[1]) && AS_INSTANCE(argv[1])->_class == vm.baseClasses->listClass) || !IS_INTEGER(argv[2])))
		return krk_runtimeError(vm.exceptions->typeError, "invalid arguments to SyntaxState.findKeywords");

	KrkValue qualifier = argv[3];
	int flag = AS_INTEGER(argv[2]);

	if (callQualifier(qualifier, lastchar())) return BOOLEAN_VAL(0);
	if (!callQualifier(qualifier, charat()))  return BOOLEAN_VAL(0);

	for (size_t keyword = 0; keyword < AS_LIST(argv[1])->count; ++keyword) {
		if (!IS_STRING(AS_LIST(argv[1])->values[keyword]))
			return krk_runtimeError(vm.exceptions->typeError, "expected list of strings, found '%s'", krk_typeName(AS_LIST(argv[1])->values[keyword]));

		KrkString * me = AS_STRING(AS_LIST(argv[1])->values[keyword]);
		size_t d = 0;
		if ((me->obj.flags & KRK_OBJ_FLAGS_STRING_MASK) == KRK_OBJ_FLAGS_STRING_ASCII) {
			while (state->i + (int)d < state->line->actual &&
			       d < me->codesLength &&
			       state->line->text[state->i+d].codepoint == me->chars[d]) d++;
		} else {
			krk_unicodeString(me);
			while (state->i + (int)d < state->line->actual &&
			       d < me->codesLength &&
			       state->line->text[state->i+d].codepoint == KRK_STRING_FAST(me,d)) d++;
		}
		if (d == me->codesLength && (state->i + (int)d >= state->line->actual ||
			!callQualifier(qualifier,state->line->text[state->i+d].codepoint))) {
			paint((int)me->codesLength, flag);
			return BOOLEAN_VAL(1);
		}
	}
	return BOOLEAN_VAL(0);
}
static KrkValue bim_krk_state_matchAndPaint(int argc, const KrkValue argv[], int hasKw) {
	BIM_STATE();
	if (argc < 4 || !IS_STRING(argv[1]) || !IS_INTEGER(argv[2]))
		return krk_runtimeError(vm.exceptions->typeError, "invalid arguments to SyntaxState.matchAndPaint");
	KrkValue qualifier = argv[3];
	int flag = AS_INTEGER(argv[2]);
	KrkString * me = AS_STRING(argv[1]);
	size_t d = 0;
	if ((me->obj.flags & KRK_OBJ_FLAGS_STRING_MASK) == KRK_OBJ_FLAGS_STRING_ASCII) {
		while (state->i + (int)d < state->line->actual &&
		       d < me->codesLength &&
		       state->line->text[state->i+d].codepoint == me->chars[d]) d++;
	} else {
		krk_unicodeString(me);
		while (state->i + (int)d < state->line->actual &&
		       d < me->codesLength &&
		       state->line->text[state->i+d].codepoint == KRK_STRING_FAST(me,d)) d++;
	}
	if (d == me->codesLength && (state->i + (int)d >= state->line->actual ||
		!callQualifier(qualifier,state->line->text[state->i+d].codepoint))) {
		paint((int)me->codesLength, flag);
		return BOOLEAN_VAL(1);
	}
	return BOOLEAN_VAL(0);
}
static KrkValue bim_krk_state_rewind(int argc, const KrkValue argv[], int hasKw) {
	BIM_STATE();
	state->i -= AS_INTEGER(argv[1]);
	return NONE_VAL();
}
static KrkValue bim_krk_state_commentBuzzwords(int argc, const KrkValue argv[], int hasKw) {
	BIM_STATE();
	return BOOLEAN_VAL(common_comment_buzzwords(state));
}
static KrkValue bim_krk_state_init(int argc, const KrkValue argv[], int hasKw) {
	BIM_STATE();
	if (argc < 2 || !krk_isInstanceOf(argv[1], syntaxStateClass)) {
		return krk_runtimeError(vm.exceptions->typeError, "Can only initialize subhighlighter from an existing highlighter.");
	}

	*state = ((struct SyntaxState*)AS_INSTANCE(argv[1]))->state;

	return argv[0];
}

static KrkValue krk_bim_get_commands(int argc, const KrkValue argv[], int hasKw) {
	KrkValue myList = krk_list_of(0, NULL,0);
	krk_push(myList);
	for (struct command_def * c = regular_commands; regular_commands && c->name; ++c) {
		krk_writeValueArray(AS_LIST(myList), OBJECT_VAL(krk_copyString(c->name,strlen(c->name))));
	}
	for (struct command_def * c = prefix_commands; prefix_commands && c->name; ++c) {
		krk_writeValueArray(AS_LIST(myList), OBJECT_VAL(krk_copyString(c->name,strlen(c->name))));
	}
	return krk_pop();
}

KrkClass * ActionDef;
struct ActionDef {
	KrkInstance inst;
	struct action_def * action;
};

static KrkValue bim_krk_action_call(int argc, const KrkValue argv[], int hasKw) {
	struct ActionDef * self = (void*)AS_OBJECT(argv[0]);

	/* Figure out arguments */
	int args = 0;
	if (self->action->options & ARG_IS_CUSTOM) args++;
	if (self->action->options & ARG_IS_INPUT) args++;
	if (self->action->options & ARG_IS_PROMPT) args++;

	int argsAsInts[3] = { 0, 0, 0 };
	for (int i = 0; i < args; i++) {
		if (argc < i + 2)
			return krk_runtimeError(vm.exceptions->argumentError, "%s() takes %d argument%s",
				self->action->name, args, args == 1 ? "" : "s");
		if (IS_INTEGER(argv[i+1])) {
			argsAsInts[i] = AS_INTEGER(argv[i+1]);
		} else if (IS_STRING(argv[i+1]) && AS_STRING(argv[i+1])->codesLength == 1) {
			argsAsInts[i] = krk_unicodeCodepoint(AS_STRING(argv[i+1]), 0);
		} else if (IS_BOOLEAN(argv[i+1])) {
			argsAsInts[i] = AS_BOOLEAN(argv[i+1]);
		} else {
			return krk_runtimeError(vm.exceptions->typeError,
				"argument to %s() must be int, bool, or str of len 1",
				self->action->name);
		}
	}

	((action_three_arg)self->action->action)(argsAsInts[0], argsAsInts[1], argsAsInts[2]);

	return NONE_VAL();
}

static KrkValue bim_krk_command_call(int argc, const KrkValue argv[], int hasKw) {
	struct CommandDef * self = (void*)AS_OBJECT(argv[0]);

	char ** args = malloc(sizeof(char*)*argc);
	args[0] = strdup(self->command->name);

	for (int i = 1; i < argc; ++i) {
		if (IS_STRING(argv[i])) {
			args[i] = strdup(AS_CSTRING(argv[i]));
		} else {
			krk_push(argv[i]);
			KrkValue asString = krk_callDirect(krk_getType(argv[i])->_tostr, 1);
			args[i] = strdup(AS_CSTRING(asString));
		}
	}

	int result = self->command->command(args[0], argc, args);

	for (int i = 0; i < argc; ++i) {
		free(args[i]);
	}
	free(args);

	return INTEGER_VAL(result);
}

static void makeClass(KrkInstance * module, KrkClass ** _class, const char * name, KrkClass * base) {
	KrkString * str_Name = krk_copyString(name,strlen(name));
	krk_push(OBJECT_VAL(str_Name));
	*_class = krk_newClass(str_Name, base);
	krk_push(OBJECT_VAL(*_class));
	/* Bind it */
	krk_attachNamedObject(&module->fields,name,(KrkObj*)*_class);
	krk_pop();
	krk_pop();
}

void import_directory(char * dirName) {
	const char * extra = "";
	char * dirpath = NULL;
	char file[4096];
	if (vm.binpath) {
		char * tmp = strdup(vm.binpath);
		dirpath = strdup(dirname(tmp));
		extra = "/";
		free(tmp);
		sprintf(file, "%s/%s", dirpath, dirName);
	} else {
		sprintf(file, "%s", dirName);
	}

	DIR * dirp = opendir(file);
	if (!dirp && dirpath) {
		/* Try ../share/bim/dirName */
		sprintf(file, "%s/../share/bim/%s", dirpath, dirName);
		extra = "/../share/bim/";
		dirp = opendir(file);
	}
	if (!dirp) {
		/* Try /usr/share/bim */
		if (dirpath) free(dirpath);
		dirpath = strdup("/usr/share/bim");
		sprintf(file, "%s/%s", dirpath, dirName);
		extra = "/";
		dirp = opendir(file);
	}
	if (!dirp) {
		/* Try one last fallback */
		if (dirpath) free(dirpath);
		dirpath = strdup("/usr/local/share/bim");
		sprintf(file, "%s/%s", dirpath, dirName);
		extra = "/";
		dirp = opendir(file);
	}
	if (!dirp) {
		fprintf(stderr, "Could not find startup files: %s\n", dirName);
		exit(1);
	}

	if (dirpath) {
		/* get kuroko.module_paths */
		krk_push(krk_valueGetAttribute(OBJECT_VAL(vm.system), "module_paths"));
		krk_push(krk_valueGetAttribute(krk_peek(0), "insert"));
		krk_push(INTEGER_VAL(0));
		/* calculate dirpath + extra */
		krk_push(OBJECT_VAL(krk_copyString(dirpath,strlen(dirpath))));
		krk_push(OBJECT_VAL(krk_copyString(extra,strlen(extra))));
		krk_addObjects();
		krk_callStack(2); /* result value is popped */
		krk_pop(); /* should just be the list */
	}

	if (dirpath) free(dirpath);
	struct dirent * ent = readdir(dirp);
	while (ent) {
		if (str_ends_with(ent->d_name,".krk") && !str_ends_with(ent->d_name,"__init__.krk")) {
			/* put "dir.file" onto the stack */
			krk_push(OBJECT_VAL(krk_copyString(dirName,strlen(dirName))));
			krk_push(OBJECT_VAL(S(".")));
			krk_addObjects();
			krk_push(OBJECT_VAL(krk_copyString(ent->d_name,strlen(ent->d_name)-4)));
			krk_addObjects();

			/* import that */
			krk_doRecursiveModuleLoad(AS_STRING(krk_peek(0)));

			if (krk_currentThread.flags & KRK_THREAD_HAS_EXCEPTION) {
				krk_dumpTraceback();
				render_error("The above exception was encountered while loading '%s/%s'.", dirName, ent->d_name);

				if (global_config.has_terminal) {
					/* Prompt to continue */
					render_commandline_message("Continue loading modules? (y/N) ");
					int key;
					while ((key = bim_getkey(DEFAULT_KEY_WAIT)) == KEY_TIMEOUT);
					if (key != 'y') {
						krk_resetStack();
						break;
					}
				} else {
					render_error("Press ENTER to continue loading.");
					int c;
					while ((c = bim_getch(), c != ENTER_KEY && c != LINE_FEED));
				}
			}

			/* reset the stack */
			krk_resetStack();
		}
		ent = readdir(dirp);
	}
	closedir(dirp);
}

static void findBim(char * argv[]) {
	/* Try asking /proc */
	char * binpath = realpath("/proc/self/exe", NULL);
	if (!binpath) {
		if (strchr(argv[0], '/')) {
			binpath = realpath(argv[0], NULL);
		} else {
			/* Search PATH for argv[0] */
			char * _path = strdup(getenv("PATH"));
			char * path = _path;
			while (path) {
				char * next = strchr(path,':');
				if (next) *next++ = '\0';

				char tmp[4096];
				sprintf(tmp, "%s/%s", path, argv[0]);
				if (access(tmp, X_OK)) {
					binpath = strdup(tmp);
					break;
				}
				path = next;
			}
			free(_path);
		}
	}
	if (binpath) {
		vm.binpath = binpath;
	} /* Else, give up at this point and just don't attach it at all. */
}

BIM_COMMAND(reload,"reload","Reloads all the Kuroko stuff.") {
	/* Unload everything syntax-y */
	KrkValue result = krk_interpret(
		"if True:\n"
		" import kuroko\n"
		" for mod in kuroko.modules():\n"
		"  if mod.startswith('syntax.') or mod.startswith('themes.'):\n"
		"   kuroko.unload(mod)\n", "<bim>");

	if (IS_NONE(result) && (krk_currentThread.flags & KRK_THREAD_HAS_EXCEPTION)) {
		krk_dumpTraceback();
		return 1;
	}

	/* Reload everything */
	krk_resetStack();
	krk_startModule("<bim-syntax>");
	import_directory("syntax");
	krk_startModule("<bim-themes>");
	import_directory("themes");
	krk_startModule("<bim-repl>");
	/* Re-run the RC file */
	load_bimrc();
	krk_resetStack();
	return 0;
}

static KrkValue krk_bim_getDocumentText(int argc, const KrkValue argv[], int hasKw) {
	struct StringBuilder sb = {0};

	int i, j;
	for (i = 0; i < env->line_count; ++i) {
		line_t * line = env->lines[i];
		for (j = 0; j < line->actual; j++) {
			char_t c = line->text[j];
			if (c.codepoint == 0) {
				pushStringBuilder(&sb, 0);
			} else {
				char tmp[8] = {0};
				int len = to_eight(c.codepoint, tmp);
				pushStringBuilderStr(&sb, tmp, len);
			}
		}
		pushStringBuilder(&sb, '\n');
	}

	return finishStringBuilder(&sb);
}

static KrkValue krk_bim_renderError(int argc, const KrkValue argv[], int hasKw) {
	static const char * _method_name = "renderError";
	if (argc != 1 || !IS_STRING(argv[0])) return TYPE_ERROR(str,argv[0]);
	if (AS_STRING(argv[0])->length == 0)
		redraw_commandline();
	else
		render_error(AS_CSTRING(argv[0]));
	return NONE_VAL();
}

KRK_Function(getDocumentFilename) {
	if (!env || !env->file_name) return NONE_VAL();
	return OBJECT_VAL(krk_copyString(env->file_name,strlen(env->file_name)));
}

static KrkValue krk_bim_custom_action_dict;
KRK_Function(bindkey) {
	const char * key = NULL;
	const char * mode = NULL;
	KrkValue callable;

	if (!krk_parseArgs("ssV", (const char*[]){"key","mode","callable"},
		&key, &mode, &callable)) {
		return NONE_VAL();
	}

	struct action_map ** mode_map = NULL;
	for (struct mode_names * m = mode_names; m->name; ++m) {
		if (!strcmp(m->name, mode)) {
			mode_map = m->mode;
			break;
		}
	}
	if (!mode_map) return krk_runtimeError(vm.exceptions->valueError, "invalid mode: %s", mode);

	enum Key keycode = key_from_name(key);
	if (keycode == -1) return krk_runtimeError(vm.exceptions->valueError, "invalid key: %s", key);

	struct action_map * candidate = NULL;
	for (struct action_map * m = *mode_map; m->key != -1; ++m) {
		if (m->key == keycode) {
			candidate = m;
			break;
		}
	}

	if (!candidate) {
		/* get size */
		int len = 0;
		for (struct action_map * m = *mode_map; m->key != -1; m++, len++);
		*mode_map = realloc(*mode_map, sizeof(struct action_map) * (len + 2));
		candidate = &(*mode_map)[len];
		(*mode_map)[len+1].key = -1;
	}

	candidate->key = keycode;
	candidate->options = opt_krk;
	candidate->callable = callable;

	return 0;

	krk_tableSet(AS_DICT(krk_bim_custom_action_dict), callable, BOOLEAN_VAL(1));
	return NONE_VAL();
}


/**
 * Run global initialization tasks
 */
void initialize(void) {
	/* Force empty locale */
#ifdef __APPLE__
	/* TODO figure out a better way to do this; maybe just LC_CTYPE? */
	setlocale(LC_ALL, "en_US.UTF-8");
#else
	setlocale(LC_ALL, "");
#endif

	/* Set up default key mappings */
#define CLONE_MAP(map) do { \
	int len = 0, i = 0; \
	for (struct action_map * m = _ ## map; m->key != -1; m++, len++); \
	map = malloc(sizeof(struct action_map) * (len + 1)); \
	for (struct action_map * m = _ ## map; m->key != -1; m++, i++) { \
		memcpy(&map[i], m, sizeof(struct action_map)); \
	} \
	map[i].key = -1; \
} while (0)

	CLONE_MAP(NORMAL_MAP);
	CLONE_MAP(INSERT_MAP);
	CLONE_MAP(REPLACE_MAP);
	CLONE_MAP(LINE_SELECTION_MAP);
	CLONE_MAP(CHAR_SELECTION_MAP);
	CLONE_MAP(COL_SELECTION_MAP);
	CLONE_MAP(COL_INSERT_MAP);
	CLONE_MAP(NAVIGATION_MAP);
	CLONE_MAP(ESCAPE_MAP);
	CLONE_MAP(COMMAND_MAP);
	CLONE_MAP(SEARCH_MAP);
	CLONE_MAP(FILESEARCH_MAP);
	CLONE_MAP(INPUT_BUFFER_MAP);

#undef CLONE_MAP

	/* Simple ASCII defaults, but you could use " " as a config option */
	global_config.tab_indicator = strdup(">");
	global_config.space_indicator = strdup("-");

	/* Initialize Kuroko runtime context */
	krk_initVM(0);

	/**
	 * Build the 'bim' module:
	 * @c bindHighlighter Applied to syntax highlighter classes to register them.
	 * @c getCommands     Returns a list of bim command objects.
	 * @c themes          dict, theme names to theme functions.
	 * @c defineTheme     Applied to theme functions to register them.
	 * @c highlighters    dict, syntax highlighter names to highlighter classes.
	 * @c getDocumentText Return a string with the full contents of the current buffer.
	 * @c renderError     Binding to render_error.
	 */
	KrkInstance * bimModule = krk_newInstance(vm.baseClasses->moduleClass);
	krk_attachNamedObject(&vm.modules, "bim", (KrkObj*)bimModule);
	krk_attachNamedObject(&bimModule->fields, "__name__", (KrkObj*)S("bim"));
	krk_defineNative(&bimModule->fields, "bindHighlighter", krk_bim_register_syntax);
	krk_defineNative(&bimModule->fields, "getCommands", krk_bim_get_commands);
	krk_bim_theme_dict = krk_dict_of(0,NULL,0);
	krk_attachNamedValue(&bimModule->fields, "themes", krk_bim_theme_dict);
	krk_defineNative(&bimModule->fields, "defineTheme", krk_bim_define_theme);
	krk_bim_syntax_dict = krk_dict_of(0,NULL,0);
	krk_attachNamedValue(&bimModule->fields, "highlighters", krk_bim_syntax_dict);
	krk_defineNative(&bimModule->fields, "getDocumentText", krk_bim_getDocumentText);
	krk_defineNative(&bimModule->fields, "renderError", krk_bim_renderError);

	krk_bim_custom_action_dict = krk_dict_of(0,NULL,0);
	krk_attachNamedValue(&bimModule->fields,"customActions", krk_bim_custom_action_dict);
	BIND_FUNC(bimModule, bindkey);
	BIND_FUNC(bimModule, getDocumentFilename);

	/**
	 * Class representing a BIM_ACTION.
	 * Actions end up in __builtins__, which is dirty, but done for config reasons.
	 * Calling an action executes it.
	 */
	makeClass(bimModule, &ActionDef, "Action", vm.baseClasses->objectClass);
	ActionDef->allocSize = sizeof(struct ActionDef);
	krk_defineNative(&ActionDef->methods, "__call__", bim_krk_action_call);
	krk_finalizeClass(ActionDef);

	for (struct action_def * a = mappable_actions; mappable_actions && a->name; ++a) {
		struct ActionDef * actionObj = (void*)krk_newInstance(ActionDef);
		actionObj->action = a;
		krk_attachNamedObject(&vm.builtins->fields, a->name, (KrkObj*)actionObj);
	}

	/* Class representing a BIM_COMMAND. Works the same as actions. */
	makeClass(bimModule, &CommandDef, "Command", vm.baseClasses->objectClass);
	CommandDef->allocSize = sizeof(struct CommandDef);
	krk_defineNative(&CommandDef->methods, "__call__", bim_krk_command_call);
	krk_finalizeClass(CommandDef);

	/* For silly legacy config reasons, we have a special 'global' namespace
	 * that we just shove into __builtins__. This contains all of the command
	 * objects that are bound with names starting with "global.", naturally. */
	KrkInstance * global = krk_newInstance(vm.baseClasses->objectClass);
	krk_attachNamedObject(&vm.builtins->fields, "global", (KrkObj*)global);

	for (struct command_def * c = regular_commands; regular_commands && c->name; ++c) {
		struct CommandDef * commandObj = (void*)krk_newInstance(CommandDef);
		commandObj->command = c;
		if (strstr(c->name,"global.") == c->name) {
			krk_attachNamedObject(&global->fields, c->name + 7, (KrkObj*)commandObj);
		} else {
			krk_attachNamedObject(&vm.builtins->fields, c->name, (KrkObj*)commandObj);
		}
	}

	/**
	 * SyntaxState is the base class for syntax highlighters.
	 *
	 * @class SyntaxState
	 *  @e Properties
	 *   @b state  Read-write access to the underlying state number (used for passing context between lines)
	 *   @b i      Read access to the offset into the line.
	 *   @b lineno Read access to the line number of the line being highlighted.
	 *
	 *  @e Methods
	 *   @b findKeywords()     Takes a list of keywords and highlights with a given flag based on a qualifier.
	 *   @b isdigit()          Determines if the argument character is a "digit" (0-9)
	 *   @b isxdigit()         Determines if the argument character is a "hex digit" (0-9, a-f, A-F)
	 *   @b paint()            Paints a number of character cells a given color and advances the highlighter.
	 *   @b paintComment()     Paints an end-of-line comment, with buzzword handling. Legacy convenience function.
	 *   @b skip()             Moves the highlighter forward one character cell without painting.
	 *   @b matchAndPaint()    Similar to @c findKeywords but only highlights one thing.
	 *   @b commentBuzzwords() Detects and automatically highlights common comment buzzwords. Legacy convenience function.
	 *   @b rewind()           Rewinds the highlighter, moving it back to a previous character cell.
	 *   @b __getitem__()      Index into character cells of the current line from the highlighter.
	 *                         Note, negative indexes will reference cells before the 'cursor', but this
	 *                         does not apply to slicing, which treats the rest of the line (starting at the
	 *                         cursor) as a single string, thus -1 is the last character of the line.
	 *                         Indexing returns @c None rather than raising an IndexError if the requested
	 *                         character is out of range, similar to behavior of the C @c charrel interface.
	 *  @e Flags
	 *   These flags supply the C FLAG_ constants. Unfortunately, this is kinda slow, and
	 *   it would be nice to have some sort of compile-time constant available so that these
	 *   don't have to imply attribute lookups at runtime...
	 */
	makeClass(bimModule, &syntaxStateClass, "SyntaxState", vm.baseClasses->objectClass);
	syntaxStateClass->allocSize = sizeof(struct SyntaxState);
	krk_defineNativeProperty(&syntaxStateClass->methods, "state", bim_krk_state_getstate);
	krk_defineNativeProperty(&syntaxStateClass->methods, "i", bim_krk_state_index);
	krk_defineNativeProperty(&syntaxStateClass->methods, "lineno", bim_krk_state_lineno);
	krk_defineNative(&syntaxStateClass->methods, "__init__", bim_krk_state_init);
	krk_defineNative(&syntaxStateClass->methods, "findKeywords", bim_krk_state_findKeywords);
	krk_defineNative(&syntaxStateClass->methods, "cKeywordQualifier", bim_krk_state_cKeywordQualifier)->obj.flags |= KRK_OBJ_FLAGS_FUNCTION_IS_STATIC_METHOD;
	krk_defineNative(&syntaxStateClass->methods, "isdigit", bim_krk_state_isdigit);
	krk_defineNative(&syntaxStateClass->methods, "isxdigit", bim_krk_state_isxdigit);
	krk_defineNative(&syntaxStateClass->methods, "paint", bim_krk_state_paint);
	krk_defineNative(&syntaxStateClass->methods, "paintComment", bim_krk_state_paintComment);
	krk_defineNative(&syntaxStateClass->methods, "skip", bim_krk_state_skip);
	krk_defineNative(&syntaxStateClass->methods, "matchAndPaint", bim_krk_state_matchAndPaint);
	krk_defineNative(&syntaxStateClass->methods, "commentBuzzwords", bim_krk_state_commentBuzzwords);
	krk_defineNative(&syntaxStateClass->methods, "rewind", bim_krk_state_rewind);
	krk_defineNative(&syntaxStateClass->methods, "__getitem__", bim_krk_state_get);
	krk_defineNative(&syntaxStateClass->methods, "__setitem__", bim_krk_state_paint);
	krk_defineNative(&syntaxStateClass->methods, "__contains__", bim_krk_state_check);
	krk_defineNative(&syntaxStateClass->methods, "__mod__", bim_krk_state_getstate);
	krk_defineNative(&syntaxStateClass->methods, "__lshift__", bim_krk_state_setstate);
	krk_attachNamedValue(&syntaxStateClass->methods, "FLAG_NONE", INTEGER_VAL(FLAG_NONE));
	krk_attachNamedValue(&syntaxStateClass->methods, "FLAG_KEYWORD", INTEGER_VAL(FLAG_KEYWORD));
	krk_attachNamedValue(&syntaxStateClass->methods, "FLAG_STRING", INTEGER_VAL(FLAG_STRING));
	krk_attachNamedValue(&syntaxStateClass->methods, "FLAG_COMMENT", INTEGER_VAL(FLAG_COMMENT));
	krk_attachNamedValue(&syntaxStateClass->methods, "FLAG_TYPE", INTEGER_VAL(FLAG_TYPE));
	krk_attachNamedValue(&syntaxStateClass->methods, "FLAG_PRAGMA", INTEGER_VAL(FLAG_PRAGMA));
	krk_attachNamedValue(&syntaxStateClass->methods, "FLAG_NUMERAL", INTEGER_VAL(FLAG_NUMERAL));
	krk_attachNamedValue(&syntaxStateClass->methods, "FLAG_ERROR", INTEGER_VAL(FLAG_ERROR));
	krk_attachNamedValue(&syntaxStateClass->methods, "FLAG_DIFFPLUS", INTEGER_VAL(FLAG_DIFFPLUS));
	krk_attachNamedValue(&syntaxStateClass->methods, "FLAG_DIFFMINUS", INTEGER_VAL(FLAG_DIFFMINUS));
	krk_attachNamedValue(&syntaxStateClass->methods, "FLAG_NOTICE", INTEGER_VAL(FLAG_NOTICE));
	krk_attachNamedValue(&syntaxStateClass->methods, "FLAG_BOLD", INTEGER_VAL(FLAG_BOLD));
	krk_attachNamedValue(&syntaxStateClass->methods, "FLAG_LINK", INTEGER_VAL(FLAG_LINK));
	krk_attachNamedValue(&syntaxStateClass->methods, "FLAG_ESCAPE", INTEGER_VAL(FLAG_ESCAPE));
	krk_attachNamedValue(&syntaxStateClass->methods, "FLAG_EXTRA", INTEGER_VAL(FLAG_EXTRA));
	krk_attachNamedValue(&syntaxStateClass->methods, "FLAG_SPECIAL", INTEGER_VAL(FLAG_SPECIAL));
	krk_attachNamedValue(&syntaxStateClass->methods, "FLAG_UNDERLINE", INTEGER_VAL(FLAG_UNDERLINE));

	

	/* This is a dumb cache of characters to avoid recreating them all the time */
	_bim_state_chars = krk_newTuple(95);
	krk_attachNamedObject(&syntaxStateClass->methods, "__chars__", (KrkObj*)_bim_state_chars);
	for (int c = 0; c < 95; ++c) {
		char tmp = c + 32;
		_bim_state_chars->values.values[_bim_state_chars->values.count++] = OBJECT_VAL(krk_copyString(&tmp,1));
	}

	krk_finalizeClass(syntaxStateClass);

	krk_resetStack();

	krk_startModule("<bim-syntax>");
	import_directory("syntax");
	krk_startModule("<bim-themes>");
	import_directory("themes");

	/* Start context for command line */
	krk_startModule("<bim-repl>");

	/* Load bimrc */
	load_bimrc();
	krk_resetStack();

	/* Disable default traceback printing */
	vm.globalFlags |= KRK_GLOBAL_CLEAN_OUTPUT;

	/* Initialize space for buffers */
	buffers_avail = 4;
	buffers = malloc(sizeof(buffer_t *) * buffers_avail);
}

/**
 * Initialize terminal for editor display.
 */
void init_terminal(void) {
	if (!isatty(STDIN_FILENO) && isatty(STDOUT_FILENO)) {
		global_config.tty_in = STDERR_FILENO;
	}
	set_alternate_screen();
	set_bracketed_paste();
	update_screen_size();
	get_initial_termios();
	set_unbuffered();
	mouse_enable();
	global_config.has_terminal = 1;

	signal(SIGWINCH, SIGWINCH_handler);
	signal(SIGCONT,  SIGCONT_handler);
	signal(SIGTSTP,  SIGTSTP_handler);
	signal(SIGINT,   SIGINT_handler);
}

struct action_def * find_action(uintptr_t action) {
	if (!action) return NULL;
	for (int i = 0; i < flex_mappable_actions_count; ++i) {
		if (action == mappable_actions[i].action) return &mappable_actions[i];
	}
	return NULL;
}

void dump_mapping(const char * description, struct action_map * map) {
	printf("## %s\n", description);
	printf("\n");
	printf("| **Key** | **Action** | **Description** |\n");
	printf("|---------|------------|-----------------|\n");
	struct action_map * m = map;
	while (m->key != -1) {
		/* Find the name of this action */
		struct action_def * action = find_action(m->method);
		printf("| `%s` | `%s` | %s |\n", name_from_key(m->key),
			action ? action->name : "(unbound)",
			action ? action->description : "(unbound)");
		m++;
	}
	printf("\n");
}

int sort_regular_commands(const void * a, const void * b) {
	return strcmp(regular_commands[*(int *)a].name, regular_commands[*(int *)b].name);
}

int sort_prefix_commands(const void * a, const void * b) {
	return strcmp(prefix_commands[*(int *)a].name, prefix_commands[*(int *)b].name);
}

void dump_commands(void) {
	printf("## Regular Commands\n");
	printf("\n");
	printf("| **Command** | **Description** |\n");
	printf("|-------------|-----------------|\n");
	int * offsets = malloc(sizeof(int) * flex_regular_commands_count);
	for (int i = 0; i < flex_regular_commands_count; ++i) {
		offsets[i] = i;
	}
	qsort(offsets, flex_regular_commands_count, sizeof(int), sort_regular_commands);
	for (int i = 0; i < flex_regular_commands_count; ++i) {
		printf("| `:%s` | %s |\n", regular_commands[offsets[i]].name, regular_commands[offsets[i]].description);
	}
	free(offsets);
	printf("\n");
	printf("## Prefix Commands\n");
	printf("\n");
	printf("| **Command** | **Description** |\n");
	printf("|-------------|-----------------|\n");
	offsets = malloc(sizeof(int) * flex_prefix_commands_count);
	for (int i = 0; i < flex_prefix_commands_count; ++i) offsets[i] = i;
	qsort(offsets, flex_prefix_commands_count, sizeof(int), sort_prefix_commands);
	for (int i = 0; i < flex_prefix_commands_count; ++i) {
		printf("| `:%s...` | %s |\n", !strcmp(prefix_commands[offsets[i]].name, "`") ? "`(backtick)`" : 
			prefix_commands[offsets[i]].name, prefix_commands[offsets[i]].description);
	}
	free(offsets);
	printf("\n");
}

BIM_COMMAND(whatis,"whatis","Describe actions bound to a key in different modes.") {
	int key = 0;

	if (argc < 2) {
		render_commandline_message("(press a key)");
		while ((key = bim_getkey(DEFAULT_KEY_WAIT)) == KEY_TIMEOUT);
	} else if (strlen(argv[1]) > 1 && argv[1][0] == '^') {
		/* See if it's a valid ctrl key */
		if (argv[1][2] != '\0') {
			goto _invalid_key_name;
		}
		if ((unsigned char)argv[1][1] < '@' || (unsigned char)argv[1][1] > '@' + '_') {
			goto _invalid_key_name;
		}
		key = argv[1][1] - '@';
	} else if (argv[1][1] != '\0') {
		for (unsigned int i = 0; i < sizeof(KeyNames)/sizeof(KeyNames[0]); ++i) {
			if (!strcmp(KeyNames[i].name,argv[1])) {
				key = KeyNames[i].keycode;
			}
		}
		if (!key) goto _invalid_key_name;
	} else {
		key = (unsigned char)argv[1][0];
	}

	struct MappingNames {
		char * name;
		struct action_map * map;
	} maps[] = {
		{"Normal", NORMAL_MAP},
		{"Insert", INSERT_MAP},
		{"Replace", REPLACE_MAP},
		{"Line Selection", LINE_SELECTION_MAP},
		{"Char Selection", CHAR_SELECTION_MAP},
		{"Col Selection", COL_SELECTION_MAP},
		{"Col Insert", COL_INSERT_MAP},
		{"Navigation (Select)", NAVIGATION_MAP},
		{"Escape (Select, Insert)", ESCAPE_MAP},
		{"Command", COMMAND_MAP},
		{"Search", SEARCH_MAP},
		{"Input (Command, Search)", INPUT_BUFFER_MAP},
		{NULL, NULL},
	};

	render_commandline_message("");
	int found_something = 0;

	for (struct MappingNames * map = maps; map->name; ++map) {
		/* See if this key is mapped */
		struct action_map * m = map->map;
		while (m->key != -1) {
			if (m->key == key) {
				if (m->options & opt_krk) {
					struct StringBuilder sb = {0};
					krk_pushStringBuilderFormat(&sb, "%R", m->callable);
					krk_pushStringBuilder(&sb, '\0');
					render_commandline_message("%s: %s\n", map->name, sb.bytes);
					krk_discardStringBuilder(&sb);
				} else {
					struct action_def * action = find_action(m->method);
					render_commandline_message("%s: %s\n", map->name, action ? action->description : "(unmapped)");
					found_something = 1;
				}
				break;
			}
			m++;
		}
	}

	if (!found_something) {
		render_commandline_message("Nothing bound for this key");
	}

	pause_for_key();

	return 0;
_invalid_key_name:
	render_error("Invalid key name");
	return 1;
}

BIM_COMMAND(setcolor, "setcolor", "Set colorscheme colors") {
#define PRINT_COLOR do { \
	render_commandline_message("%20s = ", c->name); \
	set_colors(*c->value, *c->value); \
	printf("   "); \
	set_colors(COLOR_FG, COLOR_BG); \
	printf(" %s\n", *c->value); \
	} while (0)
	if (argc < 2) {
		/* Print colors */
		struct ColorName * c = color_names;
		while (c->name) {
			PRINT_COLOR;
			c++;
		}
		pause_for_key();
	} else {
		char * colorname = argv[1];
		if (argc == 2) {
			struct ColorName * c = color_names;
			while (c->name) {
				if (!strcmp(c->name, colorname)) {
					PRINT_COLOR;
					return 0;
				}
				c++;
			}
			render_error(":setcolor <colorname> <colorvalue>");
			return 1;
		}
		char * colorvalue = argv[2];
		struct ColorName * c = color_names;
		while (c->name) {
			if (!strcmp(c->name, colorname)) {
				*(c->value) = strdup(colorvalue);
				return 0;
			}
			c++;
		}
		render_error("Unknown color: %s", colorname);
		return 1;
	}
	return 0;
#undef PRINT_COLOR
}

BIM_COMMAND(checkprop,"checkprop","Check a property value; returns the inverse of the property") {
	if (argc < 2) {
		return 1;
	}
	if (!strcmp(argv[1],"can_scroll")) return !global_config.can_scroll;
	else if (!strcmp(argv[1],"can_hideshow")) return !global_config.can_hideshow;
	else if (!strcmp(argv[1],"can_altscreen")) return !global_config.can_altscreen;
	else if (!strcmp(argv[1],"can_mouse")) return !global_config.can_mouse;
	else if (!strcmp(argv[1],"can_unicode")) return !global_config.can_unicode;
	else if (!strcmp(argv[1],"can_bright")) return !global_config.can_bright;
	else if (!strcmp(argv[1],"can_title")) return !global_config.can_title;
	else if (!strcmp(argv[1],"can_bce")) return !global_config.can_bce;
	else if (!strcmp(argv[1],"can_24bit")) return !global_config.can_24bit;
	else if (!strcmp(argv[1],"can_256color")) return !global_config.can_256color;
	else if (!strcmp(argv[1],"can_italic")) return !global_config.can_italic;
	render_error("Unknown property '%s'", argv[1]);
	return 1;
}

BIM_COMMAND(action,"action","Execute a bim action") {
	if (argc < 2) {
		render_error("Expected :action <action-name> [arg [arg [arg...]]]");
		return 1;
	}

	/* Split argument on spaces */
	char * action = argv[1];
	char * arg1 = NULL, * arg2 = NULL, * arg3 = NULL;
	if (argc > 2) arg1 = argv[2];
	if (argc > 3) arg2 = argv[3];
	if (argc > 4) arg3 = argv[4];

	/* Find the action */
	for (int i = 0; i < flex_mappable_actions_count; ++i) {
		if (!strcmp(mappable_actions[i].name, action)) {
			/* Count arguments */
			int args = 0;
			if (mappable_actions[i].options & ARG_IS_CUSTOM) args++;
			if (mappable_actions[i].options & ARG_IS_INPUT) args++;
			if (mappable_actions[i].options & ARG_IS_PROMPT) args++;

			if (args == 0) {
				((action_no_arg)mappable_actions[i].action)();
			} else if (args == 1) {
				if (!arg1) { render_error("Expected one argument"); return 1; }
				((action_one_arg)mappable_actions[i].action)(atoi(arg1));
			} else if (args == 2) {
				if (!arg2) { render_error("Expected two arguments"); return 1; }
				((action_two_arg)mappable_actions[i].action)(atoi(arg1), atoi(arg2));
			} else if (args == 3) {
				if (!arg3) { render_error("Expected three arguments"); return 1; }
				((action_three_arg)mappable_actions[i].action)(atoi(arg1), atoi(arg2), atoi(arg3));
			}
			return 0;
		}
	}

	render_error("Unknown action: %s", action);
	return 1;
}

char * describe_options(int options) {
	static char out[16];

	memset(out,0,sizeof(out));
	if (options & opt_rep)  strcat(out,"r"); /* Repeats */
	if (options & opt_arg)  strcat(out,"a"); /* takes Argument */
	if (options & opt_char) strcat(out,"c"); /* takes Character */
	if (options & opt_nav)  strcat(out,"n"); /* consumes Nav buffer */
	if (options & opt_rw)   strcat(out,"w"); /* read-Write */
	if (options & opt_norm) strcat(out,"m"); /* changes Mode */
	if (options & opt_byte) strcat(out,"b"); /* takes Byte */

	return out;
}

void dump_map_commands(const char * name, struct action_map * map) {
	struct action_map * m = map;
	while (m->key != -1) {
		if (m->options & opt_krk) {
			fprintf(stdout,"# key %s bound in %s mode to a krk function",
				name_from_key(m->key), name);
		} else {
			struct action_def * action = find_action(m->method);
			fprintf(stdout,"mapkey %s %s %s",
				name,
				name_from_key(m->key),
				action ? action->name : "none");
			if (m->options) {
				printf(" %s", describe_options(m->options));
				if (m->options & opt_arg) {
					printf(" %d", m->arg);
				}
			}
		}
		printf("\n");
		m++;
	}
}

BIM_COMMAND(mapkey,"mapkey","Map a key to an action.") {
	if (argc < 2) goto _argument_error;

	char * mode = argv[1];

	char * key = strstr(mode," ");
	if (!key) goto _argument_error;
	*key = '\0';
	key++;

	char * action = strstr(key," ");
	if (!action) goto _argument_error;
	*action = '\0';
	action++;

	/* Options are optional */
	char * options = strstr(action, " ");
	char * arg = NULL;
	if (options) {
		*options = '\0';
		options++;

		arg = strstr(options, " ");
		if (arg) {
			*arg = '\0';
			arg++;
		}
	}

	render_status_message("Going to map key %s in mode %s to action %s with options %s, %s",
		key, mode, action, options, arg);

	/* Convert mode to mode name */
	struct action_map ** mode_map = NULL;
	for (struct mode_names * m = mode_names; m->name; ++m) {
		if (!strcmp(m->name, mode)) {
			mode_map = m->mode;
			break;
		}
	}

	if (!mode_map) {
		render_error("invalid mode: %s", mode);
		return 1;
	}

	enum Key keycode = key_from_name(key);
	if (keycode == -1) {
		render_error("invalid key: %s", key);
		return 1;
	}

	struct action_def * action_def = NULL;
	for (int i = 0; i < flex_mappable_actions_count; ++i) {
		if (!strcmp(mappable_actions[i].name, action)) {
			action_def = &mappable_actions[i];
			break;
		}
	}

	if (!action_def) {
		render_error("invalid action: %s", action);
		return 1;
	}

	/* Sanity check required options */
	if ((action_def->options & ARG_IS_CUSTOM) &&
		(!options || (!strchr(options,'a') && !strchr(options,'n')))) goto _action_sanity;
	if ((action_def->options & ARG_IS_PROMPT) &&
		(!options || (!strchr(options,'c') && !strchr(options,'b')))) goto _action_sanity;
	if ((action_def->options & ACTION_IS_RW)  &&
		(!options || !strchr(options,'w'))) goto _action_sanity;

	int option_map = 0;

	if (options) {
		for (char * o = options; *o; ++o) {
			switch (*o) {
				case 'r': option_map |= opt_rep; break;
				case 'a': option_map |= opt_arg; break;
				case 'c': option_map |= opt_char; break;
				case 'n': option_map |= opt_nav; break;
				case 'w': option_map |= opt_rw; break;
				case 'm': option_map |= opt_norm; break;
				case 'b': option_map |= opt_byte; break;
				default:
					render_error("Invalid option flag: %c", *o);
					return 1;
			}
		}
	}

	if ((option_map & opt_arg) && !arg) {
		render_error("flag 'a' requires an additional argument");
		return 1;
	}

	int arg_value = (option_map & opt_arg) ? atoi(arg) : 0;

	/* Make space */
	struct action_map * candidate = NULL;
	for (struct action_map * m = *mode_map; m->key != -1; ++m) {
		if (m->key == keycode) {
			candidate = m;
			break;
		}
	}

	if (!candidate) {
		/* get size */
		int len = 0;
		for (struct action_map * m = *mode_map; m->key != -1; m++, len++);
		*mode_map = realloc(*mode_map, sizeof(struct action_map) * (len + 2));
		candidate = &(*mode_map)[len];
		(*mode_map)[len+1].key = -1;
	}

	candidate->key = keycode;
	candidate->method = action_def->action;
	candidate->options = option_map;
	candidate->arg = arg_value;

	return 0;

_action_sanity:
	render_error("action %s requires missing flag", action);
	return 1;

_argument_error:
	render_error("usage: mapkey MODE KEY ACTION [OPTIONS [ARG]]");
	return 1;
}

int main(int argc, char * argv[]) {
	findBim(argv);
	int opt;
	while ((opt = getopt(argc, argv, "?c:C:u:q:RS:O:-:")) != -1) {
		switch (opt) {
			case 'R':
				global_config.initial_file_is_read_only = 1;
				break;
			case 'q':;
				initialize();
				global_config.use_biminfo = 0;
				global_config.go_to_line = 0;
				open_file(optarg);
				env->loading = 1;
				for (int i = 0; i < env->line_count; ++i) {
					recalculate_syntax(env->lines[i], i);
				}
				return 0;
				break;
			case 'c':
			case 'C':
				/* Print file to stdout using our syntax highlighting and color theme */
				initialize();
				global_config.use_biminfo = 0;
				global_config.go_to_line = 0;
				open_file(optarg);
				for (int i = 0; i < env->line_count; ++i) {
					recalculate_syntax(env->lines[i], i);
					if (opt == 'C') {
						draw_line_number(i);
					}
					render_line(env->lines[i], 6 * (env->lines[i]->actual + 1), 0, -1);
					reset();
					fprintf(stdout, "\n");
				}
				return 0;
			case 'u':
				global_config.bimrc_path = optarg;
				break;
			case 'S':
				global_config.syntax_fallback = optarg;
				break;
			case 'O':
				/* Set various display options */
				if (set_capability(optarg)) {
					fprintf(stderr, "%s: unrecognized -O option: %s\n", argv[0], optarg);
					return 1;
				}
				break;
			case '-':
				if (!strcmp(optarg,"version")) {
					initialize(); /* Need to load bimrc to get themes */
					update_screen_size(); /* Get terminal size if possible */
					fprintf(stderr, "\033[1mbim\033[0m %s%s\n%s\n\n", BIM_VERSION, BIM_BUILD_DATE, BIM_COPYRIGHT);
					#define SECTION(title) do { \
						int x, width; \
						width = 2 + display_width_of_string(title); \
						fprintf(stderr, " \033[1m%s\033[0m:", title); \
						x = width;
					#define ENDSECTION() fprintf(stderr, "\n\n"); } while (0)
					#define ITEM(str) do { \
						int my_width = display_width_of_string(str); \
						if (x + my_width + 1 >= global_config.term_width) { \
							fprintf(stderr, "\n"); \
							for (x = 0; x <= width; ++x) fprintf(stderr, " "); \
							fprintf(stderr, "%s", str); \
							x += width; \
						} else { \
							fprintf(stderr, " %s", str); \
							x += my_width + 1; \
						} } while(0)

					SECTION("Syntax");
					for (struct syntax_definition * s = syntaxes; syntaxes && s->name; ++s) {
						ITEM(s->name);
					}
					ENDSECTION();
					SECTION("Themes");
					for (struct theme_def * d = themes; themes && d->name; ++d) {
						ITEM(d->name);
					}
					ENDSECTION();
					return 0;
				} else if (!strcmp(optarg,"help")) {
					show_usage(argv);
					return 0;
				} else if (!strcmp(optarg,"dump-mappings")) {
					initialize();
					for (struct mode_names * m = mode_names; m->name; ++m) {
						dump_mapping(m->description, *m->mode);
					}
					return 0;
				} else if (!strcmp(optarg,"dump-commands")) {
					initialize();
					dump_commands();
					return 0;
				} else if (!strcmp(optarg,"html")) {
					if (optind >= argc) {
						show_usage(argv);
						return 1;
					}
					initialize();
					global_config.go_to_line = 0;
					open_file(argv[optind]);
					for (int i = 0; i < env->line_count; ++i) {
						recalculate_syntax(env->lines[i], i);
					}
					convert_to_html();
					/* write to stdout */
					output_file(env, stdout);
					return 0;
				} else if (!strcmp(optarg,"dump-config")) {
					initialize();
					/* Dump a config file representing the current key mappings */
					for (struct mode_names * m = mode_names; m->name; ++m) {
						dump_map_commands(m->name, *m->mode);
					}
					return 0;
				} else if (strlen(optarg)) {
					fprintf(stderr, "bim: unrecognized option `%s'\n", optarg);
					return 1;
				} /* Else, this is -- to indicate end of arguments */
				break;
			case '?':
				show_usage(argv);
				return 0;
		}
	}

	/* Set up terminal */
	initialize();
	init_terminal();

	/* Open file */
	if (argc > optind) {
		while (argc > optind) {
			open_file(argv[optind]);
			update_title();
			if (global_config.initial_file_is_read_only) {
				env->readonly = 1;
			}
			optind++;
		}
		env = buffers[0];
	} else {
		env = buffer_new();
		setup_buffer(env);
	}

	update_title();

	/* Draw the screen once */
	redraw_all();

	/* Start accepting key commands */
	normal_mode();

	return 0;
}
