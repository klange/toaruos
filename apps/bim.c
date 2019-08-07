/* Bim - A Text Editor
 *
 * Copyright (C) 2012-2019 K. Lange
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
#define _XOPEN_SOURCE 700
#define _DARWIN_C_SOURCE
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <unistd.h>
#include <termios.h>
#include <signal.h>
#include <locale.h>
#include <wchar.h>
#include <ctype.h>
#include <dirent.h>
#include <poll.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#define BIM_VERSION   "1.8.2"
#define BIM_COPYRIGHT "Copyright 2012-2019 K. Lange <\033[3mklange@toaruos.org\033[23m>"

#define BLOCK_SIZE 4096
#define ENTER_KEY     '\r'
#define LINE_FEED     '\n'
#define BACKSPACE_KEY 0x08
#define DELETE_KEY    0x7F

/**
 * Theming data
 *
 * This is all overridden by a load_colorscheme_ method.
 * The default is to load_colorscheme_ansi, but config
 * files can be used to set a different default theme.
 */
const char * COLOR_FG        = "@17";
const char * COLOR_BG        = "@0";
const char * COLOR_ALT_FG    = "@17";
const char * COLOR_ALT_BG    = "@0";
const char * COLOR_NUMBER_FG = "@17";
const char * COLOR_NUMBER_BG = "@0";
const char * COLOR_STATUS_FG = "@17";
const char * COLOR_STATUS_BG = "@0";
const char * COLOR_TABBAR_BG = "@0";
const char * COLOR_TAB_BG    = "@0";
const char * COLOR_ERROR_FG  = "@17";
const char * COLOR_ERROR_BG  = "@0";
const char * COLOR_SEARCH_FG = "@17";
const char * COLOR_SEARCH_BG = "@0";
const char * COLOR_KEYWORD   = "@17";
const char * COLOR_STRING    = "@17";
const char * COLOR_COMMENT   = "@17";
const char * COLOR_TYPE      = "@17";
const char * COLOR_PRAGMA    = "@17";
const char * COLOR_NUMERAL   = "@17";
const char * COLOR_SELECTFG  = "@0";
const char * COLOR_SELECTBG  = "@17";
const char * COLOR_RED       = "@1";
const char * COLOR_GREEN     = "@2";
const char * COLOR_BOLD      = "@17";
const char * COLOR_LINK      = "@17";
const char * COLOR_ESCAPE    = "@17";
const char * current_theme = "none";

/**
 * Syntax highlighting flags.
 */
#define FLAG_NONE      0
#define FLAG_KEYWORD   1
#define FLAG_STRING    2
#define FLAG_COMMENT   3
#define FLAG_TYPE      4
#define FLAG_PRAGMA    5
#define FLAG_NUMERAL   6
#define FLAG_ERROR     7
#define FLAG_DIFFPLUS  8
#define FLAG_DIFFMINUS 9
#define FLAG_NOTICE    10
#define FLAG_BOLD      11
#define FLAG_LINK      12
#define FLAG_ESCAPE    13

#define FLAG_SELECT    (1 << 5)
#define FLAG_SEARCH    (1 << 6)

/**
 * Convert syntax highlighting flag to color code
 */
const char * flag_to_color(int _flag) {
	int flag = _flag & 0xF;
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
		case FLAG_LINK:
			return COLOR_LINK;
		case FLAG_ESCAPE:
			return COLOR_ESCAPE;
		default:
			return COLOR_FG;
	}
}

/**
 * Line buffer definitions
 *
 * Lines are essentially resizable vectors of char_t structs,
 * which represent single codepoints in the file.
 */
typedef struct {
	uint32_t display_width:4;
	uint32_t flags:7;
	uint32_t codepoint:21;
} __attribute__((packed)) char_t;

/**
 * Lines have available and actual lengths, describing
 * how much space was allocated vs. how much is being
 * used at the moment.
 */
typedef struct {
	int available;
	int actual;
	int istate;
	int is_current;
	int rev_status;
	char_t   text[];
} line_t;

/**
 * Global configuration state
 */
struct {
	/* Terminal size */
	int term_width, term_height;
	int bottom_size;

	line_t ** yanks;
	size_t    yank_count;
	int       yank_is_full_lines;

	int tty_in;

	const char * bimrc_path;

	unsigned int highlight_on_open:1;
	unsigned int initial_file_is_read_only:1;
	unsigned int can_scroll:1;
	unsigned int can_hideshow:1;
	unsigned int can_altscreen:1;
	unsigned int can_mouse:1;
	unsigned int can_unicode:1;
	unsigned int can_bright:1;
	unsigned int can_title:1;
	unsigned int can_bce:1;
	unsigned int history_enabled:1;
	unsigned int highlight_parens:1;
	unsigned int smart_case:1;
	unsigned int can_24bit:1;
	unsigned int can_256color:1;
	unsigned int can_italic:1;
	unsigned int go_to_line:1;
	unsigned int highlight_current_line:1;
	unsigned int shift_scrolling:1;
	unsigned int check_git:1;
	unsigned int color_gutter:1;
	unsigned int relative_lines:1;
	unsigned int break_from_selection:1;
	unsigned int numbers:1;

	int cursor_padding;
	int split_percent;
	int scroll_amount;
	const char * syntax_fallback;
	uint32_t * search;
} global_config = {
	0, /* term_width */
	0, /* term_height */
	2, /* bottom_size */
	NULL, /* yanks */
	0, /* yank_count */
	0, /* yank is full lines */
	STDIN_FILENO, /* tty_in */
	"~/.bimrc", /* bimrc_path */
	1, /* highlight_on_open */
	0, /* initial_file_is_read_only */
	1, /* can scroll */
	1, /* can hide/show cursor */
	1, /* can use alternate screen */
	1, /* can mouse */
	1, /* can unicode */
	1, /* can use bright colors */
	1, /* can set title */
	1, /* can bce */
	1, /* history enabled */
	1, /* highlight parens/braces when cursor moves */
	1, /* smart case */
	1, /* can use 24-bit color */
	1, /* can use 265 colors */
	1, /* can use italics (without inverting) */
	1, /* should go to line when opening file */
	1, /* highlight the current line */
	1, /* shift scrolling (shifts view rather than moving cursor) */
	0, /* check git on open and on save */
	1, /* color the gutter for modified lines */
	0, /* relative line numbers */
	1, /* status bit for whether command should NOT break from selection */
	1, /* whether to show line numbers */
	/* Things below this are outside of the simple on-off bitmap */
	4, /* cursor padding */
	50, /* split percentage */
	5, /* how many lines to scroll on mouse wheel */
	NULL, /* syntax to fall back to if none other match applies */
	NULL, /* search text */
};

void redraw_line(int x);
int git_examine(char * filename);
void search_next(void);
void set_preferred_column(void);
void quit(const char * message);
void close_buffer(void);
void set_syntax_by_name(const char * name);
void rehighlight_search(line_t * line);
void try_to_center();
int read_one_character(char * message);

/**
 * Special implementation of getch with a timeout
 */
int _bim_unget = -1;

void bim_unget(int c) {
	_bim_unget = c;
}

#define bim_getch() bim_getch_timeout(200)
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
		return -1;
	}
}

#define HISTORY_SENTINEL     0
#define HISTORY_INSERT       1
#define HISTORY_DELETE       2
#define HISTORY_REPLACE      3
#define HISTORY_REMOVE_LINE  4
#define HISTORY_ADD_LINE     5
#define HISTORY_REPLACE_LINE 6
#define HISTORY_MERGE_LINES  7
#define HISTORY_SPLIT_LINE   8

#define HISTORY_BREAK        10

typedef struct history {
	struct history * previous;
	struct history * next;
	int type;
	int line;
	int col;
	union {
		struct {
			int lineno;
			int offset;
			int codepoint;
			int old_codepoint;
		} insert_delete_replace;

		struct {
			int lineno;
			line_t * contents;
			line_t * old_contents;
		} remove_replace_line;

		struct {
			int lineno;
			int split;
		} add_merge_split_lines;
	} contents;
} history_t;

/**
 * Buffer data
 *
 * A buffer describes a file, and stores
 * its name as well as the editor state
 * (cursor offsets, etc.) and the actual
 * line buffers.
 */
typedef struct _env {
	unsigned int loading:1;
	unsigned int tabs:1;
	unsigned int modified:1;
	unsigned int readonly:1;
	unsigned int indent:1;
	unsigned int checkgitstatusonwrite:1;
	unsigned int crnl:1;

	int highlighting_paren;

	short  mode;
	short  tabstop;

	char * file_name;
	int    offset;
	int    coffset;
	int    line_no;
	int    line_count;
	int    line_avail;
	int    col_no;
	int    preferred_column;
	struct syntax_definition * syntax;
	line_t ** lines;

	history_t * history;
	history_t * last_save_history;

	int width;
	int left;

	int start_line;
	int sel_col;
} buffer_t;

/**
 * Pointer to current active buffer
 */
buffer_t * env;

buffer_t * left_buffer;
buffer_t * right_buffer;

/**
 * A buffer for holding a number (line, repetition count)
 */
#define NAV_BUFFER_MAX 10
static char nav_buf[NAV_BUFFER_MAX+1];
static int nav_buffer = 0;

/**
 * Editor modes (like in vim)
 */
#define MODE_NORMAL 0
#define MODE_INSERT 1
#define MODE_LINE_SELECTION 2
#define MODE_REPLACE 3
#define MODE_CHAR_SELECTION 4
#define MODE_COL_SELECTION 5
#define MODE_COL_INSERT 6

/**
 * Available buffers
 */
int    buffers_len;
int    buffers_avail;
buffer_t ** buffers;

/**
 * Create a new buffer
 */
buffer_t * buffer_new(void) {
	if (buffers_len == buffers_avail) {
		/* If we are out of buffer space, expand the buffers vector */
		buffers_avail *= 2;
		buffers = realloc(buffers, sizeof(buffer_t *) * buffers_avail);
	}

	/* TODO: Support having split buffers with more than two buffers open */
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
	buffers_len++;

	return buffers[buffers_len-1];
}

/**
 * Open the biminfo file.
 */
FILE * open_biminfo(void) {
	/* TODO This should probably be configurable line bimrc */
	char * home = getenv("HOME");
	if (!home) {
		/* Since it's not, we need HOME */
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
 * Fetch the cursor position from a biminfo file
 */
int fetch_from_biminfo(buffer_t * buf) {
	/* Can't fetch if we don't have a filename */
	if (!buf->file_name) return 1;

	/* Get the absolute name of the file */
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
			return 0;
		}
	}

	return 0;
}

/**
 * Write a file containing the last cursor position of a buffer.
 */
int update_biminfo(buffer_t * buf) {
	if (!buf->file_name) return 1;

	/* Get the absolute name of the file */
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
		if (line[0] != '>') {
			continue;
		}

		if (!strncmp(&line[1],tmp_path, strlen(tmp_path))) {
			/* Update */
			fsetpos(biminfo, &start_of_line);
			fprintf(biminfo,">%s %20d %20d\n", tmp_path, buf->line_no, buf->col_no);
			goto _done;
		}
	}

	if (ftell(biminfo) == 0) {
		/* New biminfo */
		fprintf(biminfo, "# This is a biminfo file.\n");
		fprintf(biminfo, "# It was generated by bim. Do not edit it by hand!\n");
		fprintf(biminfo, "# Cursor positions and other state are stored here.\n");
	}

	/* Haven't found what we're looking for, should be at end of file */
	fprintf(biminfo, ">%s %20d %20d\n", tmp_path, buf->line_no, buf->col_no);

_done:
	fclose(biminfo);
	return 0;
}

/**
 * Close a buffer
 */
buffer_t * buffer_close(buffer_t * buf) {
	int i;

	/* Locate the buffer in the buffer pointer vector */
	for (i = 0; i < buffers_len; i++) {
		if (buf == buffers[i])
			break;
	}

	/* Invalid buffer? */
	if (i == buffers_len) {
		return env; /* wtf */
	}

	update_biminfo(buf);

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
		memmove(&buffers[i], &buffers[i+1], sizeof(*buffers) * (buffers_len - i));
	}

	/* There is one less buffer */
	buffers_len--;
	if (!buffers_len) { 
		/* There are no more buffers. */
		return NULL;
	}

	/* If this was the last buffer, return the previous last buffer */
	if (i == buffers_len) {
		return buffers[buffers_len-1];
	}

	/* Otherwise return the new last buffer */
	return buffers[i];
}

/**
 * Themes
 */

/* 16-color theme, default */
void load_colorscheme_ansi(void) {
	COLOR_FG        = global_config.can_bright ? "@17" : "@7";
	COLOR_BG        = global_config.can_bright ? "@9"  : "@0";
	COLOR_ALT_FG    = global_config.can_bright ? "@10" : "@5";
	COLOR_ALT_BG    = "@9";
	COLOR_NUMBER_FG = "@3";
	COLOR_NUMBER_BG = "@9";
	COLOR_STATUS_FG = global_config.can_bright ? "@17" : "@7";
	COLOR_STATUS_BG = "@4";
	COLOR_TABBAR_BG = "@4";
	COLOR_TAB_BG    = "@4";
	COLOR_KEYWORD   = global_config.can_bright ? "@14" : "@4";
	COLOR_STRING    = "@2";
	COLOR_COMMENT   = global_config.can_bright ? "@10" : "@5";
	COLOR_TYPE      = "@3";
	COLOR_PRAGMA    = "@1";
	COLOR_NUMERAL   = "@1";

	COLOR_ERROR_FG  = global_config.can_bright ? "@17" : "@7";
	COLOR_ERROR_BG  = "@1";
	COLOR_SEARCH_FG = "@0";
	COLOR_SEARCH_BG = global_config.can_bright ? "@13" : "@3";

	COLOR_SELECTBG  = global_config.can_bright ? "@17" : "@7";
	COLOR_SELECTFG  = "@0";

	COLOR_RED       = "@1";
	COLOR_GREEN     = "@2";

	COLOR_BOLD      = COLOR_FG; /* @ doesn't support extra args; FIXME */
	COLOR_LINK      = global_config.can_bright ? "@14" : "@4";
	COLOR_ESCAPE    = global_config.can_bright ? "@12" : "@2";

	current_theme = "ansi";
}

/* Based on the wombat256 theme for vim */
void load_colorscheme_wombat(void) {
	if (!global_config.can_256color) return;
	COLOR_FG        = "5;230";
	COLOR_BG        = "5;235";
	COLOR_ALT_FG    = "5;244";
	COLOR_ALT_BG    = "5;236";
	COLOR_NUMBER_BG = "5;232";
	COLOR_NUMBER_FG = "5;101";
	COLOR_STATUS_FG = "5;230";
	COLOR_STATUS_BG = "5;238";
	COLOR_TABBAR_BG = "5;230";
	COLOR_TAB_BG    = "5;248";
	COLOR_KEYWORD   = "5;117";
	COLOR_STRING    = "5;113";
	COLOR_COMMENT   = global_config.can_italic ? "5;102;3" : "5;102";
	COLOR_TYPE      = "5;186";
	COLOR_PRAGMA    = "5;173";
	COLOR_NUMERAL   = COLOR_PRAGMA;

	COLOR_ERROR_FG  = "5;15";
	COLOR_ERROR_BG  = "5;196";
	COLOR_SEARCH_FG = "5;234";
	COLOR_SEARCH_BG = "5;226";

	COLOR_SELECTFG  = "5;235";
	COLOR_SELECTBG  = "5;230";

	COLOR_RED       = "@1";
	COLOR_GREEN     = "@2";

	COLOR_BOLD      = "5;230;1";
	COLOR_LINK      = "5;117;4";
	COLOR_ESCAPE    = "5;194";

	current_theme = "wombat";
}

/* "City Lights" based on citylights.xyz */
void load_colorscheme_citylights(void) {
	if (!global_config.can_24bit) return;
	COLOR_FG        = "2;151;178;198";
	COLOR_BG        = "2;29;37;44";
	COLOR_ALT_FG    = "2;45;55;65";
	COLOR_ALT_BG    = "2;33;42;50";
	COLOR_NUMBER_FG = "2;71;89;103";
	COLOR_NUMBER_BG = "2;37;47;56";
	COLOR_STATUS_FG = "2;116;144;166";
	COLOR_STATUS_BG = "2;53;67;78";
	COLOR_TABBAR_BG = "2;37;47;56";
	COLOR_TAB_BG    = "2;29;37;44";
	COLOR_KEYWORD   = "2;94;196;255";
	COLOR_STRING    = "2;83;154;252";
	COLOR_COMMENT   = "2;107;133;153;3";
	COLOR_TYPE      = "2;139;212;156";
	COLOR_PRAGMA    = "2;0;139;148";
	COLOR_NUMERAL   = "2;207;118;132";

	COLOR_ERROR_FG  = "5;15";
	COLOR_ERROR_BG  = "5;196";
	COLOR_SEARCH_FG = "5;234";
	COLOR_SEARCH_BG = "5;226";

	COLOR_SELECTFG  = "2;29;37;44";
	COLOR_SELECTBG  = "2;151;178;198";

	COLOR_RED       = "2;222;53;53";
	COLOR_GREEN     = "2;55;167;0";

	COLOR_BOLD      = "2;151;178;198;1";
	COLOR_LINK      = "2;94;196;255;4";
	COLOR_ESCAPE    = "2;133;182;249";

	current_theme = "citylights";
}

/* Solarized Dark, popular theme */
void load_colorscheme_solarized_dark(void) {
	if (!global_config.can_24bit) return;
	COLOR_FG        = "2;147;161;161";
	COLOR_BG        = "2;0;43;54";
	COLOR_ALT_FG    = "2;147;161;161";
	COLOR_ALT_BG    = "2;7;54;66";
	COLOR_NUMBER_FG = "2;131;148;149";
	COLOR_NUMBER_BG = "2;7;54;66";
	COLOR_STATUS_FG = "2;131;148;150";
	COLOR_STATUS_BG = "2;7;54;66";
	COLOR_TABBAR_BG = "2;7;54;66";
	COLOR_TAB_BG    = "2;131;148;150";
	COLOR_KEYWORD   = "2;133;153;0";
	COLOR_STRING    = "2;42;161;152";
	COLOR_COMMENT   = "2;101;123;131";
	COLOR_TYPE      = "2;181;137;0";
	COLOR_PRAGMA    = "2;203;75;22";
	COLOR_NUMERAL   = "2;220;50;47";

	COLOR_ERROR_FG  = "5;15";
	COLOR_ERROR_BG  = "5;196";
	COLOR_SEARCH_FG = "5;234";
	COLOR_SEARCH_BG = "5;226";

	COLOR_SELECTFG  = "2;0;43;54";
	COLOR_SELECTBG  = "2;147;161;161";

	COLOR_RED       = "2;222;53;53";
	COLOR_GREEN     = "2;55;167;0";

	COLOR_BOLD      = "2;147;161;161;1";
	COLOR_LINK      = "2;42;161;152;4";
	COLOR_ESCAPE    = "2;133;153;0";

	current_theme = "solarized-dark";
}


void load_colorscheme_sunsmoke256(void) {
	if (!global_config.can_256color) return;
	COLOR_FG        = "5;188";
	COLOR_BG        = "5;234";
	COLOR_ALT_FG    = "5;244";
	COLOR_ALT_BG    = "5;236";
	COLOR_NUMBER_FG = "5;101";
	COLOR_NUMBER_BG = "5;232";
	COLOR_STATUS_FG = "5;188";
	COLOR_STATUS_BG = "5;59";
	COLOR_TABBAR_BG = "5;59";
	COLOR_TAB_BG    = "5;59";
	COLOR_KEYWORD   = "5;74";
	COLOR_STRING    = "5;71";
	COLOR_COMMENT   = global_config.can_italic ? "5;102;3" : "5;102";
	COLOR_TYPE      = "5;221";
	COLOR_PRAGMA    = "5;160";
	COLOR_NUMERAL   = "5;161";

	COLOR_ERROR_FG  = "5;15";
	COLOR_ERROR_BG  = "5;196";
	COLOR_SEARCH_FG = "5;234";
	COLOR_SEARCH_BG = "5;226";

	COLOR_SELECTFG  = "5;17";
	COLOR_SELECTBG  = "5;109";

	COLOR_RED       = "@1";
	COLOR_GREEN     = "@2";

	COLOR_BOLD      = "5;188;1";
	COLOR_LINK      = "5;74;4";
	COLOR_ESCAPE    = "5;79";

	current_theme = "sunsmoke256";
}

/* Custom theme */
void load_colorscheme_sunsmoke(void) {
	if (!global_config.can_24bit) {
		load_colorscheme_sunsmoke256();
		return;
	}
	COLOR_FG        = "2;230;230;230";
	COLOR_BG        = "2;31;31;31";
	COLOR_ALT_FG    = "2;122;122;122";
	COLOR_ALT_BG    = "2;46;43;46";
	COLOR_NUMBER_FG = "2;150;139;57";
	COLOR_NUMBER_BG = "2;0;0;0";
	COLOR_STATUS_FG = "2;230;230;230";
	COLOR_STATUS_BG = "2;71;64;58";
	COLOR_TABBAR_BG = "2;71;64;58";
	COLOR_TAB_BG    = "2;71;64;58";
	COLOR_KEYWORD   = "2;51;162;230";
	COLOR_STRING    = "2;72;176;72";
	COLOR_COMMENT   = "2;158;153;129;3";
	COLOR_TYPE      = "2;230;206;110";
	COLOR_PRAGMA    = "2;194;70;54";
	COLOR_NUMERAL   = "2;230;43;127";

	COLOR_ERROR_FG  = "5;15";
	COLOR_ERROR_BG  = "5;196";
	COLOR_SEARCH_FG = "5;234";
	COLOR_SEARCH_BG = "5;226";

	COLOR_SELECTFG  = "2;0;43;54";
	COLOR_SELECTBG  = "2;147;161;161";

	COLOR_RED       = "2;222;53;53";
	COLOR_GREEN     = "2;55;167;0";

	COLOR_BOLD      = "2;230;230;230;1";
	COLOR_LINK      = "2;51;162;230;4";
	COLOR_ESCAPE    = "2;113;203;173";

	current_theme = "sunsmoke";
}

struct theme_def {
	const char * name;
	void (*load)(void);
} themes[] = {
	{"wombat", load_colorscheme_wombat},
	{"citylights", load_colorscheme_citylights},
	{"solarized-dark", load_colorscheme_solarized_dark},
	{"ansi", load_colorscheme_ansi},
	{"sunsmoke", load_colorscheme_sunsmoke},
	{"sunsmoke256", load_colorscheme_sunsmoke256},
	{NULL, NULL}
};

struct syntax_state {
	line_t * line;
	int line_no;
	int state;
	int i;
};

struct syntax_definition {
	char * name;
	char ** ext;
	int (*calculate)(struct syntax_state *);
	int prefers_spaces;
};

struct syntax_definition syntaxes[];

#define paint(length, flag) do { for (int i = 0; i < (length) && state->i < state->line->actual; i++, state->i++) { state->line->text[state->i].flags = (flag); } } while (0)
#define charat() (state->i < state->line->actual ? state->line->text[(state->i)].codepoint : -1)
#define nextchar() (state->i + 1 < state->line->actual ? state->line->text[(state->i+1)].codepoint : -1)
#define lastchar() (state->i - 1 >= 0 ? state->line->text[(state->i-1)].codepoint : -1)
#define skip() (state->i++)
#define charrel(x) (state->i + (x) < state->line->actual ? state->line->text[(state->i+(x))].codepoint : -1)

/**
 * Find keywords from a list and paint them, assuming they aren't in the middle of other words.
 * Returns 1 if a keyword from the last was found, otherwise 0.
 */
static int find_keywords(struct syntax_state * state, char ** keywords, int flag, int (*keyword_qualifier)(int c)) {
	if (keyword_qualifier(lastchar())) return 0;
	if (!keyword_qualifier(charat())) return 0;
	for (char ** keyword = keywords; *keyword; ++keyword) {
		int d = 0;
		while (state->i + d < state->line->actual && state->line->text[state->i+d].codepoint == (*keyword)[d]) d++;
		if ((*keyword)[d] == '\0' && (state->i + d >= state->line->actual || !keyword_qualifier(state->line->text[state->i+d].codepoint))) {
			paint((int)strlen(*keyword), flag);
			return 1;
		}
	}

	return 0;
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
 * Syntax definition for C
 */
static char * syn_c_keywords[] = {
	"while","if","for","continue","return","break","switch","case","sizeof",
	"struct","union","typedef","do","default","else","goto",
	"alignas","alignof","offsetof","asm","__asm__",
	/* C++ stuff */
	"public","private","class","using","namespace","virtual","override","protected",
	"template","typename","static_cast","throw",
	NULL
};

static char * syn_c_types[] = {
	"static","int","char","short","float","double","void","unsigned","volatile","const",
	"register","long","inline","restrict","enum","auto","extern","bool","complex",
	"uint8_t","uint16_t","uint32_t","uint64_t",
	"int8_t","int16_t","int32_t","int64_t","FILE",
	"ssize_t","size_t","uintptr_t","intptr_t","__volatile__",
	"constexpr",
	NULL
};

static char * syn_c_special[] = {
	"NULL",
	"stdin","stdout","stderr",
	"STDIN_FILENO","STDOUT_FILENO","STDERR_FILENO",
	NULL
};

static int c_keyword_qualifier(int c) {
	return isalnum(c) || (c == '_');
}

/**
 * Paints a basic C-style quoted string.
 */
static void paint_c_string(struct syntax_state * state) {
	/* Assumes you came in from a check of charat() == '"' */
	paint(1, FLAG_STRING);
	int last = -1;
	while (charat() != -1) {
		if (last != '\\' && charat() == '"') {
			paint(1, FLAG_STRING);
			return;
		} else if (charat() == '\\' && (nextchar() == '\\' || nextchar() == 'n' || nextchar() == 'r')) {
			paint(2, FLAG_ESCAPE);
			last = -1;
		} else if (charat() == '\\' && nextchar() >= '0' && nextchar() <= '7') {
			paint(2, FLAG_ESCAPE);
			if (charat() >= '0' && charat() <= '7') {
				paint(1, FLAG_ESCAPE);
				if (charat() >= '0' && charat() <= '7') {
					paint(1, FLAG_ESCAPE);
				}
			}
			last = -1;
		} else if (charat() == '%') {
			paint(1, FLAG_ESCAPE);
			if (charat() == '%') {
				paint(1, FLAG_ESCAPE);
			} else {
				while (charat() == '-' || charat() == '#' || charat() == '*' || charat() == '0' || charat() == '+') paint(1, FLAG_ESCAPE);
				while (isdigit(charat())) paint(1, FLAG_ESCAPE);
				if (charat() == '.') {
					paint(1, FLAG_ESCAPE);
					if (charat() == '*') paint(1, FLAG_ESCAPE);
					else while (isdigit(charat())) paint(1, FLAG_ESCAPE);
				}
				while (charat() == 'l' || charat() == 'z') paint(1, FLAG_ESCAPE);
				paint(1, FLAG_ESCAPE);
			}
		} else if (charat() == '\\' && nextchar() == 'x') {
			paint(2, FLAG_ESCAPE);
			while (isxdigit(charat())) paint(1, FLAG_ESCAPE);
		} else {
			last = charat();
			paint(1, FLAG_STRING);
		}
	}
}

static void paint_simple_string(struct syntax_state * state) {
	/* Assumes you came in from a check of charat() == '"' */
	paint(1, FLAG_STRING);
	while (charat() != -1) {
		if (charat() == '\\' && nextchar() == '"') {
			paint(2, FLAG_ESCAPE);
		} else if (charat() == '"') {
			paint(1, FLAG_STRING);
			return;
		} else if (charat() == '\\') {
			paint(2, FLAG_ESCAPE);
		} else {
			paint(1, FLAG_STRING);
		}
	}
}

/**
 * Paint a C character numeral. Can be arbitrarily large, so
 * it supports multibyte chars for things like defining weird
 * ASCII multibyte integer constants.
 */
static void paint_c_char(struct syntax_state * state) {
	/* Assumes you came in from a check of charat() == '\'' */
	paint(1, FLAG_NUMERAL);
	int last = -1;
	while (charat() != -1) {
		if (last != '\\' && charat() == '\'') {
			paint(1, FLAG_NUMERAL);
			return;
		} else if (last == '\\' && charat() == '\\') {
			paint(1, FLAG_NUMERAL);
			last = -1;
		} else {
			last = charat();
			paint(1, FLAG_NUMERAL);
		}
	}
}

/**
 * These words can appear in comments and should be highlighted.
 * Since there are a lot of comment highlighters, let's break them out.
 */
static int common_comment_buzzwords(struct syntax_state * state) {
	if (match_and_paint(state, "TODO", FLAG_NOTICE, c_keyword_qualifier)) { return 1; }
	else if (match_and_paint(state, "XXX", FLAG_NOTICE, c_keyword_qualifier)) { return 1; }
	else if (match_and_paint(state, "FIXME", FLAG_ERROR, c_keyword_qualifier)) { return 1; }
	return 0;
}

/**
 * Paint a comment until end of line, assumes this comment can not continue.
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
 * Paint a classic C comment which continues until terminated.
 * Assumes you've already painted the starting / and *.
 */
static int paint_c_comment(struct syntax_state * state) {
	int last = -1;
	while (charat() != -1) {
		if (common_comment_buzzwords(state)) continue;
		else if (last == '*' && charat() == '/') {
			paint(1, FLAG_COMMENT);
			return 0;
		} else {
			last = charat();
			paint(1, FLAG_COMMENT);
		}
	}
	return 1;
}

/**
 * Paint a generic C pragma, eg. a #define statement.
 */
static int paint_c_pragma(struct syntax_state * state) {
	while (state->i < state->line->actual) {
		if (charat() == '"') {
			/* Paint C string */
			paint_c_string(state);
		} else if (charat() == '\'') {
			paint_c_char(state);
		} else if (charat() == '\\' && state->i == state->line->actual - 1) {
			paint(1, FLAG_PRAGMA);
			return 2;
		} else if (find_keywords(state, syn_c_keywords, FLAG_KEYWORD, c_keyword_qualifier)) {
			continue;
		} else if (find_keywords(state, syn_c_types, FLAG_TYPE, c_keyword_qualifier)) {
			continue;
		} else if (charat() == '/' && nextchar() == '/') {
			/* C++-style comments */
			paint_comment(state);
			return -1;
		} else if (charat() == '/' && nextchar() == '*') {
			/* C-style comments */
			if (paint_c_comment(state) == 1) return 3;
			continue;
		} else {
			paint(1, FLAG_PRAGMA);
		}
	}
	return 0;
}

/**
 * Paint integers and floating point values with some handling for suffixes.
 */
static int paint_c_numeral(struct syntax_state * state) {
	if (charat() == '0' && (nextchar() == 'x' || nextchar() == 'X')) {
		paint(2, FLAG_NUMERAL);
		while (isxdigit(charat())) paint(1, FLAG_NUMERAL);
	} else if (charat() == '0' && nextchar() == '.') {
		paint(2, FLAG_NUMERAL);
		while (isdigit(charat())) paint(1, FLAG_NUMERAL);
		if (charat() == 'f') paint(1, FLAG_NUMERAL);
		return 0;
	} else if (charat() == '0') {
		paint(1, FLAG_NUMERAL);
		while (charat() >= '0' && charat() <= '7') paint(1, FLAG_NUMERAL);
	} else {
		while (isdigit(charat())) paint(1, FLAG_NUMERAL);
		if (charat() == '.') {
			paint(1, FLAG_NUMERAL);
			while (isdigit(charat())) paint(1, FLAG_NUMERAL);
			if (charat() == 'f') paint(1, FLAG_NUMERAL);
			return 0;
		}
	}
	while (charat() == 'u' || charat() == 'U' || charat() == 'l' || charat() == 'L') paint(1, FLAG_NUMERAL);
	return 0;
}

static int syn_c_calculate(struct syntax_state * state) {
	switch (state->state) {
		case -1:
		case 0:
			if (charat() == '#') {
				/* Must be first thing on line, but can have spaces before */
				for (int i = 0; i < state->i; ++i) {
					if (state->line->text[i].codepoint != ' ' && state->line->text[i].codepoint != '\t') {
						skip();
						return 0;
					}
				}
				/* Handle preprocessor functions */
				paint(1, FLAG_PRAGMA);
				while (charat() == ' ') paint(1, FLAG_PRAGMA);
				if (match_and_paint(state, "include", FLAG_PRAGMA, c_keyword_qualifier)) {
					/* Put quotes around <includes> */
					while (charat() == ' ') paint(1, FLAG_PRAGMA);
					if (charat() == '<') {
						paint(1, FLAG_STRING);
						while (charat() != '>' && state->i < state->line->actual) {
							paint(1, FLAG_STRING);
						}
						if (charat() != -1) {
							paint(1, FLAG_STRING);
						}
					}
					/* (for "includes", normal pragma highlighting covers that. */
				} else if (match_and_paint(state, "if", FLAG_PRAGMA, c_keyword_qualifier)) {
					/* These are to prevent #if and #else from being highlighted as keywords */
				} else if (match_and_paint(state, "else", FLAG_PRAGMA, c_keyword_qualifier)) {
					/* ... */
				}
				return paint_c_pragma(state);
			} else if (charat() == '/' && nextchar() == '/') {
				/* C++-style comments */
				paint_comment(state);
			} else if (charat() == '/' && nextchar() == '*') {
				/* C-style comments */
				if (paint_c_comment(state) == 1) return 1;
				return 0;
			} else if (find_keywords(state, syn_c_keywords, FLAG_KEYWORD, c_keyword_qualifier)) {
				return 0;
			} else if (find_keywords(state, syn_c_types, FLAG_TYPE, c_keyword_qualifier)) {
				return 0;
			} else if (find_keywords(state, syn_c_special, FLAG_NUMERAL, c_keyword_qualifier)) {
				return 0;
			} else if (charat() == '\"') {
				paint_c_string(state);
				return 0;
			} else if (charat() == '\'') {
				paint_c_char(state);
				return 0;
			} else if (!c_keyword_qualifier(lastchar()) && isdigit(charat())) {
				paint_c_numeral(state);
				return 0;
			} else if (charat() != -1) {
				skip();
				return 0;
			}
			break;
		case 1:
			/* In a block comment */
			if (paint_c_comment(state) == 1) return 1;
			return 0;
		case 2:
			/* In an unclosed preprocessor statement */
			return paint_c_pragma(state);
		case 3:
			/* In a block comment within an unclosed preprocessor statement */
			if (paint_c_comment(state) == 1) return 3;
			return paint_c_pragma(state);
	}
	return -1;
}

static char * c_ext[] = {".c",".h",".cpp",".hpp",".c++",".h++",".cc",".hh",NULL};

static char * syn_py_keywords[] = {
	"class","def","return","del","if","else","elif","for","while","continue",
	"break","assert","as","and","or","except","finally","from","global",
	"import","in","is","lambda","with","nonlocal","not","pass","raise","try","yield",
	NULL
};

static char * syn_py_types[] = {
	/* built-in functions */
	"abs","all","any","ascii","bin","bool","breakpoint","bytes",
	"bytearray","callable","compile","complex","delattr","chr",
	"dict","dir","divmod","enumerate","eval","exec","filter","float",
	"format","frozenset","getattr","globals","hasattr","hash","help",
	"hex","id","input","int","isinstance","issubclass","iter","len",
	"list","locals","map","max","memoryview","min","next","object",
	"oct","open","ord","pow","print","property","range","repr","reverse",
	"round","set","setattr","slice","sorted","staticmethod","str","sum",
	"super","tuple","type","vars","zip",
	NULL
};

static char * syn_py_special[] = {
	"True","False","None",
	NULL
};

static int paint_py_triple_double(struct syntax_state * state) {
	while (charat() != -1) {
		if (charat() == '"') {
			paint(1, FLAG_STRING);
			if (charat() == '"' && nextchar() == '"') {
				paint(2, FLAG_STRING);
				return 0;
			}
		} else {
			paint(1, FLAG_STRING);
		}
	}
	return 1; /* continues */
}

static int paint_py_triple_single(struct syntax_state * state) {
	while (charat() != -1) {
		if (charat() == '\'') {
			paint(1, FLAG_STRING);
			if (charat() == '\'' && nextchar() == '\'') {
				paint(2, FLAG_STRING);
				return 0;
			}
		} else {
			paint(1, FLAG_STRING);
		}
	}
	return 2; /* continues */
}

static int paint_py_single_string(struct syntax_state * state) {
	paint(1, FLAG_STRING);
	while (charat() != -1) {
		if (charat() == '\\' && nextchar() == '\'') {
			paint(2, FLAG_ESCAPE);
		} else if (charat() == '\'') {
			paint(1, FLAG_STRING);
			return 0;
		} else if (charat() == '\\') {
			paint(2, FLAG_ESCAPE);
		} else {
			paint(1, FLAG_STRING);
		}
	}
	return 0;
}

static int paint_py_numeral(struct syntax_state * state) {
	if (charat() == '0' && (nextchar() == 'x' || nextchar() == 'X')) {
		paint(2, FLAG_NUMERAL);
		while (isxdigit(charat())) paint(1, FLAG_NUMERAL);
	} else if (charat() == '0' && nextchar() == '.') {
		paint(2, FLAG_NUMERAL);
		while (isdigit(charat())) paint(1, FLAG_NUMERAL);
		if ((charat() == '+' || charat() == '-') && (nextchar() == 'e' || nextchar() == 'E')) {
			paint(2, FLAG_NUMERAL);
			while (isdigit(charat())) paint(1, FLAG_NUMERAL);
		} else if (charat() == 'e' || charat() == 'E') {
			paint(1, FLAG_NUMERAL);
			while (isdigit(charat())) paint(1, FLAG_NUMERAL);
		}
		if (charat() == 'j') paint(1, FLAG_NUMERAL);
		return 0;
	} else {
		while (isdigit(charat())) paint(1, FLAG_NUMERAL);
		if (charat() == '.') {
			paint(1, FLAG_NUMERAL);
			while (isdigit(charat())) paint(1, FLAG_NUMERAL);
			if ((charat() == '+' || charat() == '-') && (nextchar() == 'e' || nextchar() == 'E')) {
				paint(2, FLAG_NUMERAL);
				while (isdigit(charat())) paint(1, FLAG_NUMERAL);
			} else if (charat() == 'e' || charat() == 'E') {
				paint(1, FLAG_NUMERAL);
				while (isdigit(charat())) paint(1, FLAG_NUMERAL);
			}
			if (charat() == 'j') paint(1, FLAG_NUMERAL);
			return 0;
		}
		if (charat() == 'j') paint(1, FLAG_NUMERAL);
	}
	while (charat() == 'l' || charat() == 'L') paint(1, FLAG_NUMERAL);
	return 0;
}

static void paint_py_format_string(struct syntax_state * state, char type) {
	paint(1, FLAG_STRING);
	while (charat() != -1) {
		if (charat() == '\\' && nextchar() == type) {
			paint(2, FLAG_ESCAPE);
		} else if (charat() == type) {
			paint(1, FLAG_STRING);
			return;
		} else if (charat() == '\\') {
			paint(2, FLAG_ESCAPE);
		} else if (charat() == '{') {
			paint(1, FLAG_NUMERAL);
			if (charat() == '}') {
				state->i--;
				paint(2, FLAG_ERROR); /* Can't do that. */
			} else {
				while (charat() != -1 && charat() != '}') {
					paint(1, FLAG_NUMERAL);
				}
				paint(1, FLAG_NUMERAL);
			}
		} else {
			paint(1, FLAG_STRING);
		}
	}
}

static int syn_py_calculate(struct syntax_state * state) {
	switch (state->state) {
		case -1:
		case 0:
			if (charat() == '#') {
				paint_comment(state);
			} else if (state->i == 0 && match_and_paint(state, "import", FLAG_PRAGMA, c_keyword_qualifier)) {
				return 0;
			} else if (charat() == '@') {
				paint(1, FLAG_PRAGMA);
				while (c_keyword_qualifier(charat())) paint(1, FLAG_PRAGMA);
				return 0;
			} else if (charat() == '"') {
				if (nextchar() == '"' && charrel(2) == '"') {
					paint(3, FLAG_STRING);
					return paint_py_triple_double(state);
				} else if (lastchar() == 'f') {
					/* I don't like backtracking like this, but it makes this parse easier */
					state->i--;
					paint(1,FLAG_TYPE);
					paint_py_format_string(state,'"');
					return 0;
				} else {
					paint_simple_string(state);
					return 0;
				}
			} else if (find_keywords(state, syn_py_keywords, FLAG_KEYWORD, c_keyword_qualifier)) {
				return 0;
			} else if (lastchar() != '.' && find_keywords(state, syn_py_types, FLAG_TYPE, c_keyword_qualifier)) {
				return 0;
			} else if (find_keywords(state, syn_py_special, FLAG_NUMERAL, c_keyword_qualifier)) {
				return 0;
			} else if (charat() == '\'') {
				if (nextchar() == '\'' && charrel(2) == '\'') {
					paint(3, FLAG_STRING);
					return paint_py_triple_single(state);
				} else if (lastchar() == 'f') {
					/* I don't like backtracking like this, but it makes this parse easier */
					state->i--;
					paint(1,FLAG_TYPE);
					paint_py_format_string(state,'\'');
					return 0;
				} else {
					return paint_py_single_string(state);
				}
			} else if (!c_keyword_qualifier(lastchar()) && isdigit(charat())) {
				paint_py_numeral(state);
				return 0;
			} else if (charat() != -1) {
				skip();
				return 0;
			}
			break;
		case 1: /* multiline """ string */
			return paint_py_triple_double(state);
		case 2: /* multiline ''' string */
			return paint_py_triple_single(state);
	}
	return -1;
}

static char * py_ext[] = {".py",NULL};

static char * syn_java_keywords[] = {
	"assert","break","case","catch","class","continue",
	"default","do","else","enum","exports","extends","finally",
	"for","if","implements","instanceof","interface","module","native",
	"new","requires","return","throws",
	"strictfp","super","switch","synchronized","this","throw","try","while",
	NULL
};

static char * syn_java_types[] = {
	"var","boolean","void","short","long","int","double","float","enum","char",
	"private","protected","public","static","final","transient","volatile","abstract",
	NULL
};

static char * syn_java_special[] = {
	"true","false","import","package","null",
	NULL
};

static char * syn_java_at_comments[] = {
	"@author","@see","@since","@return","@throws",
	"@version","@exception","@deprecated",
	/* @param is special */
	NULL
};

static int at_keyword_qualifier(int c) {
	return isalnum(c) || (c == '_') || (c == '@');
}

static char * syn_java_brace_comments[] = {
	"{@docRoot","{@inheritDoc","{@link","{@linkplain",
	"{@value","{@code","{@literal","{@serial",
	"{@serialData","{@serialField",
	NULL
};

static int brace_keyword_qualifier(int c) {
	return isalnum(c) || (c == '{') || (c == '@');
}

static int paint_java_comment(struct syntax_state * state) {
	int last = -1;
	while (charat() != -1) {
		if (common_comment_buzzwords(state)) continue;
		else if (charat() == '@') {
			if (!find_keywords(state, syn_java_at_comments, FLAG_ESCAPE, at_keyword_qualifier)) {
				if (match_and_paint(state, "@param", FLAG_ESCAPE, at_keyword_qualifier)) {
					while (charat() == ' ') skip();
					while (c_keyword_qualifier(charat())) paint(1, FLAG_TYPE);
				} else {
					/* Paint the @ */
					paint(1, FLAG_COMMENT);
				}
			}
		} else if (charat() == '{') {
			/* see if this terminates */
			if (find_keywords(state, syn_java_brace_comments, FLAG_ESCAPE, brace_keyword_qualifier)) {
				while (charat() != '}' && charat() != -1) {
					paint(1, FLAG_ESCAPE);
				}
				if (charat() == '}') paint(1, FLAG_ESCAPE);
			} else {
				paint(1, FLAG_COMMENT);
			}
		} else if (charat() == '<') {
			int is_tag = 0;
			for (int i = 1; charrel(i) != -1; ++i) {
				if (charrel(i) == '>') {
					is_tag = 1;
					break;
				}
				if (!isalnum(charrel(i)) && charrel(i) != '/') {
					is_tag = 0;
					break;
				}
			}
			if (is_tag) {
				paint(1, FLAG_TYPE);
				while (charat() != -1 && charat() != '>') {
					if (charat() == '/') paint(1, FLAG_TYPE);
					else paint(1, FLAG_KEYWORD);
				}
				if (charat() == '>') paint(1, FLAG_TYPE);
			} else {
				/* Paint the < */
				paint(1, FLAG_COMMENT);
			}
		} else if (last == '*' && charat() == '/') {
			paint(1, FLAG_COMMENT);
			return 0;
		} else {
			last = charat();
			paint(1, FLAG_COMMENT);
		}
	}
	return 1;
}

static int syn_java_calculate(struct syntax_state * state) {
	switch (state->state) {
		case -1:
		case 0:
			if (!c_keyword_qualifier(lastchar()) && isdigit(charat())) {
				paint_c_numeral(state);
				return 0;
			} else if (charat() == '/' && nextchar() == '/') {
				/* C++-style comments */
				paint_comment(state);
			} else if (charat() == '/' && nextchar() == '*') {
				/* C-style comments; TODO: Needs special stuff for @author; <html>; etc. */
				if (paint_java_comment(state) == 1) return 1;
			} else if (find_keywords(state, syn_java_keywords, FLAG_KEYWORD, c_keyword_qualifier)) {
				return 0;
			} else if (find_keywords(state, syn_java_types, FLAG_TYPE, c_keyword_qualifier)) {
				return 0;
			} else if (find_keywords(state, syn_java_special, FLAG_NUMERAL, c_keyword_qualifier)) {
				return 0;
			} else if (charat() == '\"') {
				paint_simple_string(state);
				return 0;
			} else if (charat() == '\'') {
				paint_c_char(state);
				return 0;
			} else if (charat() == '@') {
				paint(1, FLAG_PRAGMA);
				while (c_keyword_qualifier(charat())) paint(1, FLAG_PRAGMA);
				return 0;
			} else if (charat() != -1) {
				skip();
				return 0;
			}
			break;
		case 1:
			if (paint_java_comment(state) == 1) return 1;
			return 0;
	}
	return -1;
}

static char * java_ext[] = {".java",NULL};

static int syn_diff_calculate(struct syntax_state * state) {
	/* No states to worry about */
	if (state->i == 0) {
		int flag = 0;
		if (charat() == '+') {
			flag = FLAG_DIFFPLUS;
		} else if (charat() == '-') {
			flag = FLAG_DIFFMINUS;
		} else if (charat() == '@') {
			flag = FLAG_TYPE;
		} else if (charat() != ' ') {
			flag = FLAG_KEYWORD;
		} else {
			return -1;
		}
		while (charat() != -1) paint(1, flag);
	}
	return -1;
}

static char * diff_ext[] = {".patch",".diff",NULL};

static int syn_conf_calculate(struct syntax_state * state) {
	if (state->i == 0) {
		if (charat() == ';') {
			while (charat() != -1) {
				if (common_comment_buzzwords(state)) continue;
				else paint(1, FLAG_COMMENT);
			}
		} else if (charat() == '#') {
			while (charat() != -1) {
				if (common_comment_buzzwords(state)) continue;
				else paint(1, FLAG_COMMENT);
			}
		} else if (charat() == '[') {
			paint(1, FLAG_KEYWORD);
			while (charat() != ']' && charat() != -1) paint(1, FLAG_KEYWORD);
			if (charat() == ']') paint(1, FLAG_KEYWORD);
		} else {
			while (charat() != '=' && charat() != -1) paint(1, FLAG_TYPE);
		}
	}

	return -1;
}

static char * conf_ext[] = {".conf",".ini",".git/config",NULL};

static char * syn_rust_keywords[] = {
	"as","break","const","continue","crate","else","enum","extern",
	"false","fn","for","if","impl","in","let","loop","match","mod",
	"move","mut","pub","ref","return","Self","self","static","struct",
	"super","trait","true","type","unsafe","use","where","while",
	NULL,
};

static char * syn_rust_types[] = {
	"bool","char","str",
	"i8","i16","i32","i64",
	"u8","u16","u32","u64",
	"isize","usize",
	"f32","f64",
	NULL,
};

static int paint_rs_comment(struct syntax_state * state) {
	while (charat() != -1) {
		if (common_comment_buzzwords(state)) continue;
		else if (charat() == '*' && nextchar() == '/') {
			paint(2, FLAG_COMMENT);
			state->state--;
			if (state->state == 0) return 0;
		} else if (charat() == '/' && nextchar() == '*') {
			state->state++;
			paint(2, FLAG_COMMENT);
		} else {
			paint(1, FLAG_COMMENT);
		}
	}
	return state->state;
}

static int paint_rust_numeral(struct syntax_state * state) {
	if (charat() == '0' && nextchar() == 'b') {
		paint(2, FLAG_NUMERAL);
		while (charat() == '0' || charat() == '1' || charat() == '_') paint(1, FLAG_NUMERAL);
	} else if (charat() == '0' && nextchar() == 'o') {
		paint(2, FLAG_NUMERAL);
		while ((charat() >= '0' && charat() <= '7') || charat() == '_') paint(1, FLAG_NUMERAL);
	} else if (charat() == '0' && nextchar() == 'x') {
		paint(2, FLAG_NUMERAL);
		while (isxdigit(charat()) || charat() == '_') paint(1, FLAG_NUMERAL);
	} else if (charat() == '0' && nextchar() == '.') {
		paint(2, FLAG_NUMERAL);
		while (isdigit(charat()) || charat() == '_') paint(1, FLAG_NUMERAL);
	} else {
		while (isdigit(charat()) || charat() == '_') paint(1, FLAG_NUMERAL);
		if (charat() == '.') {
			paint(1, FLAG_NUMERAL);
			while (isdigit(charat()) || charat() == '_') paint(1, FLAG_NUMERAL);
		}
	}
	return 0;
}

static int syn_rust_calculate(struct syntax_state * state) {
	switch (state->state) {
		case -1:
		case 0:
			if (charat() == '/' && nextchar() == '/') {
				/* C++-style comments */
				paint_comment(state);
			} else if (charat() == '/' && nextchar() == '*') {
				paint(2, FLAG_COMMENT);
				state->state = 1;
				return paint_rs_comment(state);
			} else if (find_keywords(state, syn_rust_keywords, FLAG_KEYWORD, c_keyword_qualifier)) {
				return 0;
			} else if (find_keywords(state, syn_rust_types, FLAG_TYPE, c_keyword_qualifier)) {
				return 0;
			} else if (charat() == '\"') {
				paint_simple_string(state);
				return 0;
			} else if (charat() == '\'') {
				paint_c_char(state);
				return 0;
			} else if (!c_keyword_qualifier(lastchar()) && isdigit(charat())) {
				paint_rust_numeral(state);
				return 0;
			} else if (charat() != -1) {
				skip();
				return 0;
			}
			break;
		default: /* Nested comments */
			return paint_rs_comment(state);
	}

	return -1;
}

static char * rust_ext[] = {".rs",NULL};

static char * syn_bimrc_keywords[] = {
	"history","padding","hlparen","hlcurrent","splitpercent","numbers",
	"shiftscrolling","scrollamount","git","colorgutter","relativenumber",
	NULL
};

static int syn_bimrc_calculate(struct syntax_state * state) {
	/* No states */
	if (state->i == 0) {
		if (charat() == '#') {
			while (charat() != -1) {
				if (common_comment_buzzwords(state)) continue;
				else paint(1, FLAG_COMMENT);
			}
		} else if (match_and_paint(state, "theme", FLAG_KEYWORD, c_keyword_qualifier)) {
			if (charat() == '=') {
				skip();
				for (struct theme_def * s = themes; s->name; ++s) {
					if (match_and_paint(state, s->name, FLAG_TYPE, c_keyword_qualifier)) break;
				}
			}
		} else if (find_keywords(state, syn_bimrc_keywords, FLAG_KEYWORD, c_keyword_qualifier)) {
			return -1;
		}
	}
	return -1;
}

static char * bimrc_ext[] = {".bimrc",NULL};

static int syn_biminfo_calculate(struct syntax_state * state) {
	if (state->i == 0) {
		if (charat() == '#') {
			while (charat() != -1) paint(1, FLAG_COMMENT);
		} else if (charat() == '>') {
			paint(1, FLAG_KEYWORD);
			while (charat() != ' ') paint(1, FLAG_TYPE);
			skip();
			while (charat() != -1) paint(1, FLAG_NUMERAL);
		} else {
			while (charat() != -1) paint(1, FLAG_ERROR);
		}
	}
	return -1;
}

static char * biminfo_ext[] = {".biminfo",NULL};

static int syn_gitcommit_calculate(struct syntax_state * state) {
	if (state->i == 0 && charat() == '#') {
		while (charat() != -1) paint(1, FLAG_COMMENT);
	} else if (state->line_no == 0) {
		/* First line is special */
		while (charat() != -1 && state->i < 50) paint(1, FLAG_KEYWORD);
		while (charat() != -1) paint(1, FLAG_DIFFMINUS);
	} else if (state->line_no == 1) {
		/* No text on second line */
		while (charat() != -1) paint(1, FLAG_DIFFMINUS);
	} else if (charat() != -1) {
		skip();
		return 0;
	}
	return -1;
}

static char * gitcommit_ext[] = {"COMMIT_EDITMSG", NULL};

static char * syn_gitrebase_commands[] = {
	"p","r","e","s","f","x","d",
	"pick","reword","edit","squash","fixup",
	"exec","drop",
	NULL
};

static int syn_gitrebase_calculate(struct syntax_state * state) {
	if (state->i == 0 && charat() == '#') {
		while (charat() != -1) paint(1, FLAG_COMMENT);
	} else if (state->i == 0 && find_keywords(state, syn_gitrebase_commands, FLAG_KEYWORD, c_keyword_qualifier)) {
		while (charat() == ' ') skip();
		while (isxdigit(charat())) paint(1, FLAG_NUMERAL);
		return -1;
	}

	return -1;
}

static char * gitrebase_ext[] = {"git-rebase-todo",NULL};

static int make_command_qualifier(int c) {
	return isalnum(c) || c == '_' || c == '-' || c == '.';
}

static char * syn_make_commands[] = {
	"define","endef","undefine","ifdef","ifndef","ifeq","ifneq","else","endif",
	"include","sinclude","override","export","unexport","private","vpath",
	"-include",
	NULL
};


static char * syn_make_functions[] = {
	"subst","patsubst","findstring","filter","filter-out",
	"sort","word","words","wordlist","firstword","lastword",
	"dir","notdir","suffix","basename","addsuffix","addprefix",
	"join","wildcard","realpath","abspath","error","warning",
	"shell","origin","flavor","foreach","if","or","and",
	"call","eval","file","value",
	NULL
};

static char * syn_make_special_targets[] = {
	"all", /* Not really special, but highlight it 'cause I feel like it. */
	".PHONY", ".SUFFIXES", ".DEFAULT", ".PRECIOUS", ".INTERMEDIATE",
	".SECONDARY", ".SECONDEXPANSION", ".DELETE_ON_ERROR", ".IGNORE",
	".LOW_RESOLUTION_TIME", ".SILENT", ".EXPORT_ALL_VARIABLES",
	".NOTPARALLEL", ".ONESHELL", ".POSIX",
	NULL
};

static int make_close_paren(struct syntax_state * state) {
	paint(2, FLAG_TYPE);
	find_keywords(state, syn_make_functions, FLAG_KEYWORD, c_keyword_qualifier);
	int i = 1;
	while (charat() != -1) {
		if (charat() == '(') {
			i++;
		} else if (charat() == ')') {
			i--;
			if (i == 0) {
				paint(1,FLAG_TYPE);
				return 0;
			}
		} else if (charat() == '"') {
			paint_simple_string(state);
		}
		paint(1,FLAG_TYPE);
	}
	return 0;
}

static int make_close_brace(struct syntax_state * state) {
	paint(2, FLAG_TYPE);
	while (charat() != -1) {
		if (charat() == '}') {
			paint(1, FLAG_TYPE);
			return 0;
		}
		paint(1, FLAG_TYPE);
	}
	return 0;
}

static int make_variable_or_comment(struct syntax_state * state, int flag) {
	while (charat() != -1) {
		if (charat() == '$') {
			switch (nextchar()) {
				case '(':
					make_close_paren(state);
					break;
				case '{':
					make_close_brace(state);
					break;
				default:
					paint(2, FLAG_TYPE);
					break;
			}
		} else if (charat() == '#') {
			while (charat() != -1) paint(1, FLAG_COMMENT);
		} else {
			paint(1, flag);
		}
	}
	return 0;
}

static int syn_make_calculate(struct syntax_state * state) {
	if (state->i == 0 && charat() == '\t') {
		make_variable_or_comment(state, FLAG_NUMERAL);
	} else {
		while (charat() == ' ') { skip(); }
		/* Peek forward to see if this is a rule or a variable */
		int whatisit = 0;
		for (int i = 0; charrel(i) != -1; ++i) {
			if (charrel(i) == ':' && charrel(i+1) != '=') {
				whatisit = 1;
				break;
			} else if (charrel(i) == '=') {
				whatisit = 2;
				break;
			} else if (charrel(i) == '#') {
				break;
			}
		}
		if (!whatisit) {
			/* Check for functions */
			while (charat() != -1) {
				if (charat() == '#') {
					while (charat() != -1) {
						if (common_comment_buzzwords(state)) continue;
						else paint(1, FLAG_COMMENT);
					}
				} else if (find_keywords(state, syn_make_commands, FLAG_KEYWORD, make_command_qualifier)) {
					continue;
				} else if (charat() == '$') {
					make_variable_or_comment(state, FLAG_NONE);
				} else {
					skip();
				}
			}
		} else if (whatisit == 1) {
			/* It's a rule */
			while (charat() != -1) {
				if (charat() == '#') {
					while (charat() != -1) {
						if (common_comment_buzzwords(state)) continue;
						else paint(1, FLAG_COMMENT);
					}
				} else if (charat() == ':') {
					paint(1, FLAG_TYPE);
					make_variable_or_comment(state, FLAG_NONE);
				} else if (find_keywords(state, syn_make_special_targets, FLAG_KEYWORD, make_command_qualifier)) {
						continue;
				} else {
					paint(1, FLAG_TYPE);
				}
			}
		} else if (whatisit == 2) {
			/* It's a variable definition */
			match_and_paint(state, "export", FLAG_KEYWORD, c_keyword_qualifier);
			while (charat() != -1 && charat() != '+' && charat() != '=' && charat() != ':' && charat() != '?') {
				paint(1, FLAG_TYPE);
			}
			while (charat() != -1 && charat() != '=') skip();
			/* Highlight variable expansions */
			make_variable_or_comment(state, FLAG_NONE);
		}
	}
	return -1;
}

static char * make_ext[] = {"Makefile","makefile","GNUmakefile",".mak",NULL};

#define nest(lang, low) \
	do { \
		state->state = (state->state < 1 ? 0 : state->state - low); \
		do { state->state = lang(state); } while (state->state == 0); \
		if (state->state == -1) return low; \
		return state->state + low; \
	} while (0)

static int match_forward(struct syntax_state * state, char * c) {
	int i = 0;
	while (1) {
		if (charrel(i) == -1 && !*c) return 1;
		if (charrel(i) != *c) return 0;
		c++;
		i++;
	}
	return 0;
}

static int syn_json_calculate(struct syntax_state * state);
static int syn_xml_calculate(struct syntax_state * state);
static int syn_markdown_calculate(struct syntax_state * state) {
	if (state->state < 1) {
		while (charat() != -1) {
			if (state->i == 0 && charat() == '#') {
				while (charat() == '#') paint(1, FLAG_KEYWORD);
				while (charat() != -1) paint(1, FLAG_BOLD);
				return -1;
			} else if (state->i == 0) {
				while (charat() == ' ') skip();
				if (charat() == '`' && nextchar() == '`' && charrel(2) == '`') {
					paint(3, FLAG_STRING);
					if (match_forward(state, "c")) {
						nest(syn_c_calculate, 100);
					} else if (match_forward(state,"c++")) {
						nest(syn_c_calculate, 100);
					} else if (match_forward(state,"py") || match_forward(state,"python")) {
						nest(syn_py_calculate, 200);
					} else if (match_forward(state, "java")) {
						nest(syn_java_calculate, 300);
					} else if (match_forward(state,"json")) {
						nest(syn_json_calculate, 400);
					} else if (match_forward(state,"xml")) {
						nest(syn_xml_calculate, 500);
					} else if (match_forward(state,"html")) {
						nest(syn_xml_calculate, 500); // TODO this will be a different highlighter later
					} else if (match_forward(state,"make")) {
						nest(syn_make_calculate, 600);
					} else if (match_forward(state, "diff")) {
						nest(syn_diff_calculate, 700);
					} else if (match_forward(state, "rust")) {
						nest(syn_rust_calculate, 800); /* Keep this at the end for now */
					}
					return 1;
				}
			}
			if (charat() == '`') {
				paint(1, FLAG_STRING);
				while (charat() != -1) {
					if (charat() == '`') {
						paint(1, FLAG_STRING);
						return 0;
					}
					paint(1, FLAG_STRING);
				}
			} else if (charat() == '[') {
				skip();
				while (charat() != -1 && charat() != ']') {
					paint(1, FLAG_LINK);
				}
				if (charat() == ']') skip();
				if (charat() == '(') {
					skip();
					while (charat() != -1 && charat() != ')') {
						paint(1, FLAG_NUMERAL);
					}
				}
			} else {
				skip();
				return 0;
			}
		}
		return -1;
	} else if (state->state >= 1) {
		/* Continuing generic triple-` */
		if (state->i == 0) {
			/* Go backwards until we find the source ``` */
			int count = 0;
			for (int i = state->line_no; i > 0; i--) {
				if (env->lines[i]->istate < 1) {
					while (env->lines[i]->text[count].codepoint == ' ') {
						if (charrel(count) != ' ') goto _nope;
						count++;
					}
					break;
				}
			}
			if (charrel(count) == '`' && charrel(count+1) == '`' && charrel(count+2) == '`' && charrel(count+3) == -1) {
				paint(count+3,FLAG_STRING);
				return -1;
			}
		}
_nope:
		if (state->state == 1) {
			while (charat() != -1) paint(1, FLAG_STRING);
			return 1;
		} else if (state->state < 199) {
			nest(syn_c_calculate, 100);
		} else if (state->state < 299) {
			nest(syn_py_calculate, 200);
		} else if (state->state < 399) {
			nest(syn_java_calculate, 300);
		} else if (state->state < 499) {
			nest(syn_json_calculate, 400);
		} else if (state->state < 599) {
			nest(syn_xml_calculate, 500);
		} else if (state->state < 699) {
			nest(syn_make_calculate, 600);
		} else if (state->state < 799) {
			nest(syn_diff_calculate, 700);
		} else {
			nest(syn_rust_calculate, 800);
		}
	}
	return -1;
}

static char * markdown_ext[] = {".md",".markdown",NULL};
static char * syn_json_keywords[] = {
	"true","false","null",
	NULL
};

static int syn_json_calculate(struct syntax_state * state) {
	while (charat() != -1) {
		if (charat() == '"') {
			int backtrack = state->i;
			paint_simple_string(state);
			int backtrack_end = state->i;
			while (charat() == ' ') skip();
			if (charat() == ':') {
				/* This is dumb. */
				state->i = backtrack;
				paint(1, FLAG_ESCAPE);
				while (state->i < backtrack_end-1) {
					paint(1, FLAG_KEYWORD);
				}
				if (charat() == '"') {
					paint(1, FLAG_ESCAPE);
				}
			}
			return 0;
		} else if (charat() == '-' || isdigit(charat())) {
			if (charat() == '-') paint(1, FLAG_NUMERAL);
			if (charat() == '0') {
				paint(1, FLAG_NUMERAL);
			} else {
				while (isdigit(charat())) paint(1, FLAG_NUMERAL);
			}
			if (charat() == '.') {
				paint(1, FLAG_NUMERAL);
				while (isdigit(charat())) paint(1, FLAG_NUMERAL);
			}
			if (charat() == 'e' || charat() == 'E') {
				paint(1, FLAG_NUMERAL);
				if (charat() == '+' || charat() == '-') {
					paint(1, FLAG_NUMERAL);
				}
				while (isdigit(charat())) paint(1, FLAG_NUMERAL);
			}
		} else if (find_keywords(state,syn_json_keywords,FLAG_NUMERAL,c_keyword_qualifier)) {
			/* ... */
		} else {
			skip();
			return 0;
		}
	}
	return -1;
}

static char * json_ext[] = {".json",NULL}; // TODO other stuff that uses json

static int syn_xml_calculate(struct syntax_state * state) {
	switch (state->state) {
		case -1:
		case 0:
			if (charat() == -1) return -1;
			if (charat() != '<') {
				skip();
				return 0;
			}
			/* Opening brace */
			if (charat() == '<' && nextchar() == '!' && charrel(2) == '-' && charrel(3) == '-') {
				paint(4, FLAG_COMMENT);
				goto _comment;
			}
			paint(1, FLAG_TYPE);
			/* Fall through */
		case 1:
			/* State 1: We saw an opening brace. */
			while (charat() != -1) {
				if (charat() == '/') paint(1, FLAG_TYPE);
				if (charat() == '?') paint(1, FLAG_TYPE);
				if (charat() == ' ' || charat() == '\t') skip();
				if (isalnum(charat())) {
					while (isalnum(charat()) || charat() == '-') paint(1, FLAG_KEYWORD);
					if (charat() == -1) return 2;
					goto _in_tag;
				} else {
					paint(1, FLAG_TYPE);
				}
			}
			return -1;
_in_tag:
		case 2:
			while (charat() != -1) {
				if (charat() == '>') {
					paint(1, FLAG_TYPE);
					return 0;
				} else if (charat() == '"') {
					paint_simple_string(state);
					if (charat() == -1 && lastchar() != '"') {
						return 3;
					}
				} else {
					paint(1, FLAG_TYPE);
				}
			}
			return 2;
		case 3:
			/* In a string in tag */
			if (charat() == '"') {
				paint(1, FLAG_STRING);
				return 2;
			} else {
				paint_simple_string(state);
				if (charat() == -1 && lastchar() != '"') {
					return 3;
				}
			}
			break;
_comment:
		case 4:
			while (charat() != -1) {
				if (charat() == '-' && nextchar() == '-' && charrel(2) == '>') {
					paint(3, FLAG_COMMENT);
					return 0;
				} else {
					if (common_comment_buzzwords(state)) continue;
					else paint(1, FLAG_COMMENT);
				}
			}
			return 4;
	}
	return -1;
}

static char * xml_ext[] = {".xml",".htm",".html",NULL}; // TODO other stuff that uses xml (it's a lot!); FIXME htm/html are temporary; make dedicated SGML ones for this

static char * syn_proto_keywords[] = {
	"syntax","import","option","package","message","group","oneof",
	"optional","required","repeated","default","extend","extensions","to","max","reserved",
	"service","rpc","returns","stream",
	NULL
};

static char * syn_proto_types[] = {
	"int32","int64","uint32","uint64","sint32","sint64",
	"fixed32","fixed64","sfixed32","sfixed64",
	"float","double","bool","string","bytes",
	"enum",
	NULL
};

static char * syn_proto_special[] = {
	"true","false",
	NULL
};

static int syn_proto_calculate(struct syntax_state * state) {
	if (state->state < 1) {
		if (charat() == '/' && nextchar() == '/') {
			paint_comment(state);
		} else if (charat() == '/' && nextchar() == '*') {
			if (paint_c_comment(state) == 1) return 1;
			return 0;
		} else if (find_keywords(state, syn_proto_keywords, FLAG_KEYWORD, c_keyword_qualifier)) {
			return 0;
		} else if (find_keywords(state, syn_proto_types, FLAG_TYPE, c_keyword_qualifier)) {
			return 0;
		} else if (find_keywords(state, syn_proto_special, FLAG_NUMERAL, c_keyword_qualifier)) {
			return 0;
		} else if (charat() == '"') {
			paint_simple_string(state);
			return 0;
		} else if (!c_keyword_qualifier(lastchar()) && isdigit(charat())) {
			paint_c_numeral(state);
			return 0;
		} else if (charat() != -1) {
			skip();
			return 0;
		}
		return -1;
	} else {
		if (paint_c_comment(state) == 1) return 1;
		return 0;
	}
}

static char * proto_ext[] = {".proto",NULL};

static int esh_variable_qualifier(int c) {
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (c == '_');
}

static int paint_esh_variable(struct syntax_state * state) {
	if (charat() == '{') {
		paint(1, FLAG_TYPE);
		while (charat() != '}') paint(1, FLAG_TYPE);
		if (charat() == '}') paint(1, FLAG_TYPE);
	} else {
		if (charat() == '?' || charat() == '$' || charat() == '#') {
			paint(1, FLAG_TYPE);
		} else {
			while (esh_variable_qualifier(charat())) paint(1, FLAG_TYPE);
		}
	}
	return 0;
}

static int paint_esh_string(struct syntax_state * state) {
	int last = -1;
	while (charat() != -1) {
		if (last != '\\' && charat() == '"') {
			paint(1, FLAG_STRING);
			return 0;
		} else if (charat() == '$') {
			paint(1, FLAG_TYPE);
			paint_esh_variable(state);
			last = -1;
		} else if (charat() != -1) {
			last = charat();
			paint(1, FLAG_STRING);
		}
	}
	return 2;
}

static int paint_esh_single_string(struct syntax_state * state) {
	int last = -1;
	while (charat() != -1) {
		if (last != '\\' && charat() == '\'') {
			paint(1, FLAG_STRING);
			return 0;
		} else if (charat() != -1) {
			last = charat();
			paint(1, FLAG_STRING);
		}
	}
	return 1;
}

static int esh_keyword_qualifier(int c) {
	return (isalnum(c) || c == '?' || c == '_' || c == '-'); /* technically anything that isn't a space should qualify... */
}

static char * esh_keywords[] = {
	"cd","exit","export","help","history","if","empty?",
	"equals?","return","export-cmd","source","exec","not","while",
	"then","else","echo",
	NULL
};

static int syn_esh_calculate(struct syntax_state * state) {
	if (state->state == 1) {
		return paint_esh_single_string(state);
	} else if (state->state == 2) {
		return paint_esh_string(state);
	}
	if (charat() == '#') {
		while (charat() != -1) {
			if (common_comment_buzzwords(state)) continue;
			else paint(1, FLAG_COMMENT);
		}
		return -1;
	} else if (charat() == '$') {
		paint(1, FLAG_TYPE);
		paint_esh_variable(state);
		return 0;
	} else if (charat() == '\'') {
		paint(1, FLAG_STRING);
		return paint_esh_single_string(state);
	} else if (charat() == '"') {
		paint(1, FLAG_STRING);
		return paint_esh_string(state);
	} else if (match_and_paint(state, "export", FLAG_KEYWORD, esh_keyword_qualifier)) {
		while (charat() == ' ') skip();
		while (esh_keyword_qualifier(charat())) paint(1, FLAG_TYPE);
		return 0;
	} else if (match_and_paint(state, "export-cmd", FLAG_KEYWORD, esh_keyword_qualifier)) {
		while (charat() == ' ') skip();
		while (esh_keyword_qualifier(charat())) paint(1, FLAG_TYPE);
		return 0;
	} else if (find_keywords(state, esh_keywords, FLAG_KEYWORD, esh_keyword_qualifier)) {
		return 0;
	} else if (isdigit(charat())) {
		while (isdigit(charat())) paint(1, FLAG_NUMERAL);
		return 0;
	} else if (charat() != -1) {
		skip();
		return 0;
	}
	return -1;
}

/* Only enable esh highlighting by default on ToaruOS */
static char * esh_ext[] = {
#ifdef __toaru__
	".sh",
#endif
	".eshrc",".yutanirc",
	NULL
};

static char * syn_bash_keywords[] = {
	/* Actual bash keywords */
	"if","then","else","elif","fi","case","esac","for","coproc",
	"select","while","until","do","done","in","function","time",
	/* Other keywords */
	"exit","return","source","function","export","alias","complete","shopt","local","eval",
	/* Common Unix utilities */
	"echo","cd","pushd","popd","printf","sed","rm","mv",
	NULL
};

static int bash_pop_state(int state) {
	int new_state = state / 100;
	return new_state * 10;
}

static int bash_push_state(int state, int new) {
	return state * 10 + new;
}

static int bash_paint_tick(struct syntax_state * state, int out_state) {
	int last = -1;
	while (charat() != -1) {
		if (last != '\\' && charat() == '\'') {
			paint(1, FLAG_STRING);
			return bash_pop_state(out_state);
		} else if (last == '\\') {
			paint(1, FLAG_STRING);
			last = -1;
		} else if (charat() != -1) {
			last = charat();
			paint(1, FLAG_STRING);
		}
	}
	return out_state;
}

static int bash_paint_braced_variable(struct syntax_state * state) {
	while (charat() != -1) {
		if (charat() == '}') {
			paint(1, FLAG_NUMERAL);
			return 0;
		}
		paint(1, FLAG_NUMERAL);
	}
	return 0;
}

static int bash_special_variable(int c) {
	return (c == '@' || c == '?');
}

static int bash_paint_string(struct syntax_state * state, char terminator, int out_state, int color) {
	int last = -1;
	state->state = out_state;
	while (charat() != -1) {
		if (last != '\\' && charat() == terminator) {
			paint(1, color);
			return bash_pop_state(state->state);
		} else if (last == '\\') {
			paint(1, color);
			last = -1;
		} else if (terminator != '`' && charat() == '`') {
			paint(1, FLAG_ESCAPE);
			state->state = bash_paint_string(state,'`',bash_push_state(out_state, 20),FLAG_ESCAPE);
		} else if (terminator != ')' && charat() == '$' && nextchar() == '(') {
			paint(2, FLAG_TYPE);
			state->state = bash_paint_string(state,')',bash_push_state(out_state, 30),FLAG_TYPE);
		} else if (charat() == '$' && nextchar() == '{') {
			paint(2, FLAG_NUMERAL);
			bash_paint_braced_variable(state);
		} else if (charat() == '$') {
			paint(1, FLAG_NUMERAL);
			if (bash_special_variable(charat())) { paint(1, FLAG_NUMERAL); continue; }
			while (c_keyword_qualifier(charat())) paint(1, FLAG_NUMERAL);
		} else if (terminator != '"' && charat() == '"') {
			paint(1, FLAG_STRING);
			state->state = bash_paint_string(state,'"',bash_push_state(out_state, 40),FLAG_STRING);
		} else if (terminator != '"' && charat() == '\'') { /* No single quotes in regular quotes */
			paint(1, FLAG_STRING);
			state->state = bash_paint_tick(state, out_state);
		} else if (charat() != -1) {
			last = charat();
			paint(1, color);
		}
	}
	return state->state;
}

static int syn_bash_calculate(struct syntax_state * state) {
	if (state->state < 1) {
		if (charat() == '#') {
			while (charat() != -1) {
				if (common_comment_buzzwords(state)) continue;
				else paint(1, FLAG_COMMENT);
			}
			return -1;
		} else if (charat() == '\'') {
			paint(1, FLAG_STRING);
			return bash_paint_tick(state, 10);
		} else if (charat() == '`') {
			paint(1, FLAG_ESCAPE);
			return bash_paint_string(state,'`',20,FLAG_ESCAPE);
		} else if (charat() == '$' && nextchar() == '(') {
			paint(2, FLAG_TYPE);
			return bash_paint_string(state,')',30,FLAG_TYPE);
		} else if (charat() == '"') {
			paint(1, FLAG_STRING);
			return bash_paint_string(state,'"',40,FLAG_STRING);
		} else if (charat() == '$' && nextchar() == '{') {
			paint(2, FLAG_NUMERAL);
			bash_paint_braced_variable(state);
			return 0;
		} else if (charat() == '$') {
			paint(1, FLAG_NUMERAL);
			if (bash_special_variable(charat())) { paint(1, FLAG_NUMERAL); return 0; }
			while (c_keyword_qualifier(charat())) paint(1, FLAG_NUMERAL);
			return 0;
		} else if (find_keywords(state, syn_bash_keywords, FLAG_KEYWORD, c_keyword_qualifier)) {
			return 0;
		} else if (charat() == ';') {
			paint(1, FLAG_KEYWORD);
			return 0;
		} else if (c_keyword_qualifier(charat())) {
			for (int i = 0; charrel(i) != -1; ++i) {
				if (charrel(i) == ' ') break;
				if (charrel(i) == '=') {
					for (int j = 0; j < i; ++j) {
						paint(1, FLAG_TYPE);
					}
					skip(); /* equals sign */
					return 0;
				}
			}
			for (int i = 0; charrel(i) != -1; ++i) {
				if (charrel(i) == '(') {
					for (int j = 0; j < i; ++j) {
						paint(1, FLAG_TYPE);
					}
					return 0;
				}
				if (!c_keyword_qualifier(charrel(i)) && charrel(i) != '-' && charrel(i) != ' ') break;
			}
			skip();
			return 0;
		} else if (charat() != -1) {
			skip();
			return 0;
		}
	} else if (state->state < 10) {
		/*
		 * TODO: I have an idea of how to do up to `n` (here... 8?) heredocs
		 * by storing them in a static table and using the index into that table
		 * for the state, but it's iffy. It would work well in situations where
		 * someoen used the same heredoc repeatedly throughout their document.
		 */
	} else if (state->state >= 10) {
		/* Nested string states */
		while (charat() != -1) {
			int s = (state->state / 10) % 10;
			if (s == 1) {
				state->state = bash_paint_string(state,'\'',state->state,FLAG_STRING);
			} else if (s == 2) {
				state->state = bash_paint_string(state,'`',state->state,FLAG_ESCAPE);
			} else if (s == 3) {
				state->state = bash_paint_string(state,')',state->state,FLAG_TYPE);
			} else if (s == 4) {
				state->state = bash_paint_string(state,'"',state->state,FLAG_STRING);
			} else if (!s) {
				return -1;
			}
		}
		return state->state;
	}
	return -1;
}

static char * bash_ext[] = {
#ifndef __toaru__
	".sh",
#endif
	".bash",".bashrc",
	NULL
};

int syn_ctags_calculate(struct syntax_state * state) {
	if (state->i == 0) {
		if (charat() == '!') {
			paint_comment(state);
			return -1;
		} else {
			while (charat() != -1 && charat() != '\t') paint(1, FLAG_TYPE);
			if (charat() == '\t') skip();
			while (charat() != -1 && charat() != '\t') paint(1, FLAG_NUMERAL);
			if (charat() == '\t') skip();
			while (charat() != -1 && !(charat() == ';' && nextchar() == '"')) paint(1, FLAG_KEYWORD);
			return -1;
		}
	}
	return -1;
}

static char * ctags_ext[] = { "tags", NULL };

static char * syn_bimcmd_keywords[] = {
	"help","recalc","syntax","tabn","tabp","tabnew","theme","colorscheme",
	"tabs","tabstop","spaces","noh","clearyank","indent","noindent",
	"padding","hlparen","hlcurrent","relativenumber","cursorcolumn",
	"smartcase","split","splitpercent","unsplit","git","colorgutter",
	"tohtml","buffers","s/","e","w","q","qa","q!","qa!","history","crnl",
	"numbers",
	NULL
};

static int cmd_qualifier(int c) { return c != -1 && c != ' '; }

static int syn_bimcmd_calculate(struct syntax_state * state) {
	if (state->i == 0) {
		if (match_and_paint(state, "theme", FLAG_KEYWORD, cmd_qualifier) ||
			match_and_paint(state, "colorscheme", FLAG_KEYWORD, cmd_qualifier)) {
			while (charat() == ' ') skip();
			for (struct theme_def * s = themes; s->name; ++s) {
				if (match_and_paint(state, s->name, FLAG_TYPE, cmd_qualifier)) break;
			}
		} else if (match_and_paint(state, "syntax", FLAG_KEYWORD, cmd_qualifier)) {
			while (charat() == ' ') skip();
			for (struct syntax_definition * s = syntaxes; s->name; ++s) {
				if (match_and_paint(state, s->name, FLAG_TYPE, cmd_qualifier)) break;
			}
		} else if (charat() == 's' && !isalpha(nextchar())) {
			paint(1, FLAG_KEYWORD);
			char special = charat();
			paint(1, FLAG_TYPE);
			while (charat() != -1 && charat() != special) {
				paint(1, FLAG_DIFFMINUS);
			}
			if (charat() == special) paint(1, FLAG_TYPE);
			while (charat() != -1 && charat() != special) {
				paint(1, FLAG_DIFFPLUS);
			}
			if (charat() == special) paint(1, FLAG_TYPE);
			while (charat() != -1) paint(1, FLAG_NUMERAL);
		} else if (find_keywords(state, syn_bimcmd_keywords, FLAG_KEYWORD, cmd_qualifier)) {
			return -1;
		} else if (charat() == '!') {
			paint(1, FLAG_NUMERAL);
			nest(syn_bash_calculate, 1);
		} else if (charat() == '`') {
			paint(1, FLAG_NUMERAL);
			nest(syn_py_calculate, 1);
		} else if (isdigit(charat()) || charat() == '-' || charat() == '+') {
			paint(1, FLAG_NUMERAL);
			while (isdigit(charat())) paint(1, FLAG_NUMERAL);
			return -1;
		}
	}
	return -1;
}

static char * bimcmd_ext[] = {NULL}; /* no files */

struct syntax_definition syntaxes[] = {
	{"c",c_ext,syn_c_calculate,0},
	{"python",py_ext,syn_py_calculate,1},
	{"java",java_ext,syn_java_calculate,1},
	{"diff",diff_ext,syn_diff_calculate,0},
	{"conf",conf_ext,syn_conf_calculate,0},
	{"rust",rust_ext,syn_rust_calculate,1},
	{"bimrc",bimrc_ext,syn_bimrc_calculate,0},
	{"biminfo",biminfo_ext,syn_biminfo_calculate,0},
	{"gitcommit",gitcommit_ext,syn_gitcommit_calculate,0},
	{"gitrebase",gitrebase_ext,syn_gitrebase_calculate,0},
	{"make",make_ext,syn_make_calculate,0}, /* Definitely don't use spaces for Make */
	{"markdown",markdown_ext,syn_markdown_calculate,1},
	{"json",json_ext,syn_json_calculate,1},
	{"xml",xml_ext,syn_xml_calculate,1},
	{"protobuf",proto_ext,syn_proto_calculate,1},
	{"toarush",esh_ext,syn_esh_calculate,0},
	{"bash",bash_ext,syn_bash_calculate,0},
	{"ctags",ctags_ext,syn_ctags_calculate,0},
	{"bim-command",bimcmd_ext,syn_bimcmd_calculate,0},
	{NULL,NULL,NULL,0},
};


/**
 * Calculate syntax highlighting for the given line.
 */
void recalculate_syntax(line_t * line, int line_no) {
	/* Clear syntax for this line first */
	int is_original = 1;
	while (1) {
		for (int i = 0; i < line->actual; ++i) {
			line->text[i].flags = 0;
		}

		if (!env->syntax) {
			rehighlight_search(line);
			return;
		}

		/* Start from the line's stored in initial state */
		struct syntax_state state;
		state.line = line;
		state.line_no = line_no;
		state.state = line->istate;
		state.i = 0;

		while (1) {
			state.state = env->syntax->calculate(&state);

			if (state.state != 0) {
				if (line_no == -1) return;
				rehighlight_search(line);
				if (!is_original) {
					redraw_line(line_no);
				}
				if (line_no + 1 < env->line_count && env->lines[line_no+1]->istate != state.state) {
					line_no++;
					line = env->lines[line_no];
					line->istate = state.state;
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
 * TODO:
 *
 * The line editing functions should probably take a buffer_t *
 * so that they can act on buffers other than the active one.
 */

void recursive_history_free(history_t * root) {
	if (!root->next) return;

	history_t * n = root->next;
	recursive_history_free(n);

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
	root->next = NULL;
}

#define HIST_APPEND(e) do { \
		e->col = env->col_no; \
		e->line = env->line_no; \
		if (env->history) { \
			e->previous = env->history; \
			recursive_history_free(env->history); \
			env->history->next = e; \
			e->next = NULL; \
		} \
		env->history = e; \
	} while (0)

/**
 * Mark a point where a complete set of actions has ended.
 */
void set_history_break(void) {
	if (!global_config.history_enabled) return;

	if (env->history->type != HISTORY_BREAK && env->history->type != HISTORY_SENTINEL) {
		history_t * e = malloc(sizeof(history_t));
		e->type = HISTORY_BREAK;
		HIST_APPEND(e);
	}
}

/**
 * Insert a character into an existing line.
 */
line_t * line_insert(line_t * line, char_t c, int offset, int lineno) {

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
 * Delete a character from a line
 */
void line_delete(line_t * line, int offset, int lineno) {

	/* Can't delete character before start of line. */
	if (offset == 0) return;

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
 * Replace a character in a line
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
 * Remove a line from the active buffer
 */
line_t ** remove_line(line_t ** lines, int offset) {

	/* If there is only one line, clear it instead of removing it. */
	if (env->line_count == 1) {
		while (lines[offset]->actual > 0) {
			line_delete(lines[offset], lines[offset]->actual, offset);
		}
		return lines;
	}

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
 * Add a new line to the active buffer.
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
 * Replace a line with data from another line (used by paste to paste yanked lines)
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
 * Merge two consecutive lines.
 * lineb is the offset of the second line.
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
	while (lines[linea]->available < lines[linea]->actual + lines[lineb]->actual) {
		/* ... allocate more space until it fits */
		if (lines[linea]->available == 0) {
			lines[linea]->available = 8;
		} else {
			lines[linea]->available *= 2;
		}
		/* XXX why not just do this once after calculating appropriate size */
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
 * Split a line into two lines at the given column
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

	/* Allocate more space as needed */
	if (env->line_count == env->line_avail) {
		env->line_avail *= 2;
		lines = realloc(lines, sizeof(line_t *) * env->line_avail);
	}

	/* Shift later lines down */
	if (line < env->line_count) {
		memmove(&lines[line+2], &lines[line+1], sizeof(line_t *) * (env->line_count - line));
	}

	/* I have no idea what this is doing */
	int remaining = lines[line]->actual - split;

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
	return (line->text[i].codepoint == '{' || line->text[i].codepoint == ':');
}

int line_is_comment(line_t * line) {
	if (!env->syntax) return 0;

	if (!strcmp(env->syntax->name,"c")) {
		if (line->istate == 1) return 1;
	} else if (!strcmp(env->syntax->name,"java")) {
		if (line->istate == 1) return 1;
	} else if (!strcmp(env->syntax->name,"rust")) {
		if (line->istate > 0) return 1;
	}

	return 0;
}

/**
 * Add indentation from the previous (temporally) line
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
			for (int i = 0; i < env->lines[old_line]->actual; ++i) {
				if (old_line < new_line && i == env->lines[old_line]->actual - 3 &&
					env->lines[old_line]->text[i].codepoint == ' ' &&
					env->lines[old_line]->text[i+1].codepoint == '*' &&
					env->lines[old_line]->text[i+2].codepoint == '/') {
					break;
				} else if (env->lines[old_line]->text[i].codepoint == ' ' ||
					env->lines[old_line]->text[i].codepoint == '\t') {
					env->lines[new_line] = line_insert(env->lines[new_line],env->lines[old_line]->text[i],i,new_line);
					env->col_no++;
					changed = 1;
				} else {
					break;
				}
			}
		}
		if (old_line < new_line && !ignore_brace && line_ends_with_brace(env->lines[old_line])) {
			if (env->tabs) {
				char_t c;
				c.codepoint = '\t';
				c.display_width = env->tabstop;
				env->lines[new_line] = line_insert(env->lines[new_line], c, env->col_no-1, new_line);
				env->col_no++;
				changed = 1;
			} else {
				for (int j = 0; j < env->tabstop; ++j) {
					char_t c;
					c.codepoint = ' ';
					c.display_width = 1;
					c.flags = FLAG_SELECT;
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
 * Initialize a buffer with default values
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
	new.c_iflag &= (~ICRNL);
	new.c_lflag &= (~ICANON) & (~ECHO);
	new.c_cc[VINTR] = 0;
	new.c_cc[VLNEXT] = 0;
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
	if (codepoint > 256) {
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
 * Move the terminal cursor
 */
void place_cursor(int x, int y) {
	printf("\033[%d;%dH", y, x);
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
	printf("\033[22;23;24;");
	if (*bg == '@') {
		int _bg = atoi(bg+1);
		if (_bg < 10) {
			printf("4%d;", _bg);
		} else {
			printf("10%d;", _bg-10);
		}
	} else {
		printf("48;%s;", bg);
	}
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
 * Set just the foreground color
 *
 * (See set_colors above)
 */
void set_fg_color(const char * fg) {
	printf("\033[22;23;24;");
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
 * For terminals without bce,
 * prepaint the whole line, so we don't have to track
 * where the cursor is for everything. Inefficient,
 * but effective.
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
	}
}

/**
 * Stop mouse events
 */
void mouse_disable(void) {
	if (global_config.can_mouse) {
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
 * Get the name of just a file from a full path.
 * Returns a pointer within the original string.
 */
char * file_basename(char * file) {
	char * c = strrchr(file, '/');
	if (!c) return file;
	return (c+1);
}

/**
 * Print a tab name with fixed width and modifiers
 * into an output buffer and return the written width.
 *
 * TODO this isn't unicode/display-width aware, so it returns
 *      byte lengths and doesn't limit the width of the file
 *      properly if it has wide characters. FIXME
 */
int draw_tab_name(buffer_t * _env, char * out) {
	return sprintf(out, "%s %.40s ",
		_env->modified ? " +" : "",
		_env->file_name ? file_basename(_env->file_name) : "[No Name]");
}

/**
 * Redaw the tabbar, with a tab for each buffer.
 *
 * The active buffer is highlighted.
 */
void redraw_tabbar(void) {
	/* Hide cursor while rendering UI */
	hide_cursor();

	/* Move to upper left */
	place_cursor(1,1);

	paint_line(COLOR_TABBAR_BG);

	/* For each buffer... */
	int offset = 0;
	for (int i = 0; i < buffers_len; i++) {
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

		char title[64];
		int size = draw_tab_name(_env, title);

		if (offset + size >= global_config.term_width) {
			if (global_config.term_width - offset - 1 > 0) {
				printf("%*s", global_config.term_width - offset - 1, title);
			}
			break;
		} else {
			printf("%s", title);
		}

		offset += size;
	}

	/* Reset bold/underline */
	reset();
	/* Fill the rest of the tab bar */
	set_colors(COLOR_FG, COLOR_TABBAR_BG);
	clear_to_end();
}

/**
 * Braindead log10 implementation for the line numbers
 */
int log_base_10(unsigned int v) {
	int r = (v >= 1000000000) ? 9 : (v >= 100000000) ? 8 : (v >= 10000000) ? 7 :
		(v >= 1000000) ? 6 : (v >= 100000) ? 5 : (v >= 10000) ? 4 :
		(v >= 1000) ? 3 : (v >= 100) ? 2 : (v >= 10) ? 1 : 0;
	return r;
}

/**
 * Render a line of text
 *
 * This handles rendering the actual text content. A full line of text
 * also includes a line number and some padding.
 *
 * width: width of the text display region (term width - line number width)
 * offset: how many cells into the line to start rendering at
 */
void render_line(line_t * line, int width, int offset, int line_no) {
	int i = 0; /* Offset in char_t line data entries */
	int j = 0; /* Offset in terminal cells */

	const char * last_color = NULL;
	int was_selecting = 0, was_searching = 0;

	/* Set default text colors */
	set_colors(COLOR_FG, line->is_current ? COLOR_ALT_BG : COLOR_BG);

	/*
	 * When we are rendering in the middle of a wide character,
	 * we render -'s to fill the remaining amount of the 
	 * charater's width
	 */
	int remainder = 0;

	int is_spaces = 1;

	/* For each character in the line ... */
	while (i < line->actual) {

		/* If there is remaining text... */
		if (remainder) {

			/* If we should be drawing by now... */
			if (j >= offset) {
				/* Fill remainder with -'s */
				set_colors(COLOR_ALT_FG, COLOR_ALT_BG);
				printf("-");
				set_colors(COLOR_FG, COLOR_BG);
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
				set_colors(COLOR_SELECTFG, COLOR_SELECTBG);
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

			if ((env->mode == MODE_COL_SELECTION || env->mode == MODE_COL_INSERT) &&
				line_no >= ((env->start_line < env->line_no) ? env->start_line : env->line_no) &&
				line_no <= ((env->start_line < env->line_no) ? env->line_no : env->start_line) &&
				((j == env->sel_col) ||
				(j < env->sel_col && j + c.display_width > env->sel_col))) {
				set_colors(COLOR_SELECTFG, COLOR_SELECTBG);
				was_selecting = 1;
			}

#define _set_colors(fg,bg) \
	if (!(c.flags & FLAG_SELECT) && !(was_selecting)) { \
		set_colors(fg,(line->is_current && bg == COLOR_BG) ? COLOR_ALT_BG : bg); \
	}

			/* Render special characters */
			if (c.codepoint == '\t') {
				_set_colors(COLOR_ALT_FG, COLOR_ALT_BG);
				if (global_config.can_unicode) {
					printf("»");
					for (int i = 1; i < c.display_width; ++i) {
						printf("·");
					}
				} else {
					printf(">");
					for (int i = 1; i < c.display_width; ++i) {
						printf("-");
					}
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
				if (global_config.can_unicode) {
					printf("·");
				} else {
					printf("-");
				}
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

	if (env->mode != MODE_LINE_SELECTION) {
		if (line->is_current) {
			set_colors(COLOR_FG, COLOR_ALT_BG);
		} else {
			set_colors(COLOR_FG, COLOR_BG);
		}
	} else {
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

	if (env->left + env->width == global_config.term_width && global_config.can_bce) {
		clear_to_end();
	} else {
		/* Paint the rest of the line */
		for (; j - offset < width; ++j) {
			printf(" ");
		}
	}
}

/**
 * Get the width of the line number region
 */
int num_width(void) {
	if (!global_config.numbers) return -2; /* Accounts for the padding */
	int w = log_base_10(env->line_count) + 1;
	if (w < 2) return 2;
	return w;
}

/**
 * Draw the gutter and line numbers.
 */
void draw_line_number(int x) {
	if (!global_config.numbers) return;
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
	int num_size = num_width();
	for (int y = 0; y < num_size - log_base_10(x + 1); ++y) {
		printf(" ");
	}
	printf("%d%c", x + 1, (x+1 == env->line_no && env->coffset > 0) ? '<' : ' ');
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
			place_cursor(2 + env->left, (i)-env->offset+2);
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
	if (x - env->offset < 0 || x - env->offset > global_config.term_height - global_config.bottom_size - 2) {
		return;
	}

	/* Calculate offset in screen */
	int j = x - env->offset;

	/* Hide cursor when drawing */
	hide_cursor();

	/* Move cursor to upper left most cell of this line */
	place_cursor(1 + env->left,2 + j);

	/* Draw a gutter on the left. */
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

	draw_line_number(x);

	/*
	 * Draw the line text 
	 * If this is the active line, the current character cell offset should be used.
	 * (Non-active lines are not shifted and always render from the start of the line)
	 */
	render_line(env->lines[x], env->width - 3 - num_width(), (x + 1 == env->line_no) ? env->coffset : 0, x+1);

}

/**
 * Draw a ~ line where there is no buffer text.
 */
void draw_excess_line(int j) {
	place_cursor(1+env->left,2 + j);
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
	/* Hide cursor while rendering */
	hide_cursor();

	/* Figure out the available size of the text region */
	int l = global_config.term_height - global_config.bottom_size - 1;
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
 * Draw the status bar
 *
 * The status bar shows the name of the file, whether it has modifications,
 * and (in the future) what syntax highlighting mode is enabled.
 *
 * The right side of the tatus bar shows the line number and column.
 */
void redraw_statusbar(void) {
	/* Hide cursor while rendering */
	hide_cursor();

	/* Move cursor to the status bar line (second from bottom */
	place_cursor(1, global_config.term_height - 1);

	/* Set background colors for status line */
	paint_line(COLOR_STATUS_BG);
	set_colors(COLOR_STATUS_FG, COLOR_STATUS_BG);

	/* Print the file name */
	char status_bits[1024] = {0}; /* Sane maximum */
	char * s = status_bits;

	if (env->syntax) {
		s += snprintf(s, 100, "[%s]", env->syntax->name);
	}

	/* Print file status indicators */
	if (env->modified) {
		s += snprintf(s, 5, "[+]");
	}

	if (env->readonly) {
		s += snprintf(s, 6, "[ro]");
	}

	if (env->crnl) {
		s += snprintf(s, 7, "[crnl]");
	}

	if (env->tabs) {
		s += snprintf(s, 20, "[tabs]");
	} else {
		s += snprintf(s, 20, "[spaces=%d]", env->tabstop);
	}

	if (global_config.yanks) {
		s += snprintf(s, 20, "[y:%ld]", global_config.yank_count);
	}

	if (env->indent) {
		s += snprintf(s, 20, "[indent]");
	}

	/* Pre-render the right hand side of the status bar */
	char right_hand[1024];
	snprintf(right_hand, 1024, "Line %d/%d Col: %d ", env->line_no, env->line_count, env->col_no);

	if (env->file_name) {
		int len = strlen(env->file_name);
		int i = 0;
		while (len > 5 && len > (int)global_config.term_width - (int)strlen(right_hand) - (int)strlen(status_bits) - 5) {
			len--;
			i += 1;
		}
		printf("%s%s", i > 0 ? "<" : "", env->file_name + i);
	} else {
		printf("[No Name]");
	}

	printf(" ");

	printf("%s", status_bits);

	/* Clear the rest of the status bar */
	clear_to_end();

	/* Move the cursor appropriately to draw it */
	place_cursor(global_config.term_width - strlen(right_hand), global_config.term_height - 1);
	/* TODO: What if we're localized and this has wide chars? */
	printf("%s",right_hand);
}

/**
 * Redraw the navigation numbers on the right side of the command line
 */
void redraw_nav_buffer() {
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
	char buf[1024];

	/* Process format string */
	vsnprintf(buf, 1024, message, args);
	va_end(args);

	/* Hide cursor while rendering */
	hide_cursor();

	/* Move cursor to the last line */
	place_cursor(1, global_config.term_height);

	/* Set background color */
	paint_line(COLOR_BG);
	set_colors(COLOR_FG, COLOR_BG);

	printf("%s", buf);

	/* Clear the rest of the status bar */
	clear_to_end();

	redraw_nav_buffer();
}

/**
 * Draw all screen elements
 */
void redraw_all(void) {
	redraw_tabbar();
	redraw_text();
	if (left_buffer) {
		redraw_alt_buffer(left_buffer == env ? right_buffer : left_buffer);
	}
	redraw_statusbar();
	redraw_commandline();
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
	/* varargs setup */
	va_list args;
	va_start(args, message);
	char buf[1024];

	/* Process format string */
	vsnprintf(buf, 1024, message, args);
	va_end(args);

	/* Hide cursor while rendering */
	hide_cursor();

	/* Move cursor to the status bar line (second from bottom */
	place_cursor(1, global_config.term_height - 1);

	/* Set background colors for status line */
	paint_line(COLOR_STATUS_BG);
	set_colors(COLOR_STATUS_FG, COLOR_STATUS_BG);

	printf("%s", buf);

	/* Clear the rest of the status bar */
	clear_to_end();
}

/**
 * Draw an errormessage to the command line.
 */
void render_error(char * message, ...) {
	/* varargs setup */
	va_list args;
	va_start(args, message);
	char buf[1024];

	/* Process format string */
	vsnprintf(buf, 1024, message, args);
	va_end(args);

	/* Hide cursor while rendering */
	hide_cursor();

	/* Move cursor to the command line */
	place_cursor(1, global_config.term_height);

	/* Set appropriate error message colors */
	set_colors(COLOR_ERROR_FG, COLOR_ERROR_BG);

	/* Draw the message */
	printf("%s", buf);
}

char * paren_pairs = "()[]{}<>";
void find_matching_paren(int * out_line, int * out_col, int in_col);

int is_paren(int c) {
	char * p = paren_pairs;
	while (*p) {
		if (c == *p) return 1;
		p++;
	}
	return 0;
}

#define _rehighlight_parens() do { \
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
	int num_size = num_width() + 3;
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

	while (y > global_config.term_height - global_config.bottom_size - global_config.cursor_padding) {
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
	place_cursor(x+env->left,y);

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
	unset_alternate_screen();
	fflush(stdout);

	signal(SIGTSTP, SIG_DFL);
	raise(SIGTSTP);
}

void SIGCONT_handler(int sig) {
	(void)sig;
	set_alternate_screen();
	set_unbuffered();
	update_screen_size();
	mouse_enable();
	redraw_all();
	signal(SIGCONT, SIGCONT_handler);
	signal(SIGTSTP, SIGTSTP_handler);
}

void try_to_center() {
	int half_a_screen = (global_config.term_height - 3) / 2;
	if (half_a_screen < env->line_no) {
		env->offset = env->line_no - half_a_screen;
	} else {
		env->offset = 0;
	}
}

/**
 * Move the cursor to a specific line.
 */
void goto_line(int line) {

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

/**
 * Processs (part of) a file and add it to a buffer.
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
 * Find a syntax highlighter for the given filename.
 */
struct syntax_definition * match_syntax(char * file) {
	for (struct syntax_definition * s = syntaxes; s->name; ++s) {
		for (char ** ext = s->ext; *ext; ++ext) {
			int i = strlen(file);
			int j = strlen(*ext);

			do {
				if (file[i] != (*ext)[j]) break;
				if (j == 0) return s;
				if (i == 0) break;
				i--;
				j--;
			} while (1);
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
				env->lines[i]->text[j].flags = 0;
			}
		}
		redraw_all();
		return;
	}
	for (struct syntax_definition * s = syntaxes; s->name; ++s) {
		if (!strcmp(name,s->name)) {
			env->syntax = s;
			for (int i = 0; i < env->line_count; ++i) {
				env->lines[i]->istate = -1;
			}
			for (int i = 0; i < env->line_count; ++i) {
				recalculate_syntax(env->lines[i],i);
			}
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

		struct stat statbuf;
		if (!stat(file, &statbuf) && S_ISDIR(statbuf.st_mode)) {
			DIR * dirp = opendir(file);
			if (!dirp) {
				env->loading = 0;
				return;
			}
			struct dirent * ent = readdir(dirp);
			while (ent) {
				add_buffer((unsigned char*)ent->d_name, strlen(ent->d_name));
				add_buffer((unsigned char*)"\n",1);
				ent = readdir(dirp);
			}
			closedir(dirp);
			env->file_name = strdup(file);
			env->readonly = 1;
			env->loading = 0;
			return;
		}
		f = fopen(file, "r");
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
		if (!env->syntax && global_config.syntax_fallback) {
			set_syntax_by_name(global_config.syntax_fallback);
		}
		for (int i = 0; i < env->line_count; ++i) {
			recalculate_syntax(env->lines[i],i);
		}
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
	}

	/* TODO figure out tabstop for spaces? */

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

	fclose(f);
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
	unset_alternate_screen();
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
void previous_tab(void) {
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
void next_tab(void) {
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
#ifndef __toaru__
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
					 * Note that to_line is one lower than the affacted line, so we don't need to mes with indexes.
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
					for (int i = 1; i < to_count-1; ++i) {
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
#endif
	return 0;
}


/**
 * Write active buffer to file
 */
void write_file(char * file) {
	if (!file) {
		render_error("Need a file to write to.");
		return;
	}

	FILE * f = fopen(file, "w+");

	if (!f) {
		render_error("Failed to open file for writing.");
		return;
	}

	/* Go through each line and convert it back to UTF-8 */
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

/**
 * Move the cursor down one line in the text region
 */
void cursor_down(void) {
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
		if (env->line_no > env->offset + global_config.term_height - global_config.bottom_size - 1 - global_config.cursor_padding) {
			env->offset += 1;

			/* Tell terminal to scroll */
			if (global_config.can_scroll && !left_buffer) {
				shift_up(1);

				/* A new line appears on screen at the bottom, draw it */
				int l = global_config.term_height - global_config.bottom_size - 1;
				if (env->offset + l < env->line_count + 1) {
					redraw_line(env->offset + l-1);
				} else {
					draw_excess_line(l - 1);
				}
			} else {
				redraw_text();
			}
			redraw_tabbar();
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

/**
 * Move the cursor up one line in the text region
 */
void cursor_up(void) {
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
				shift_down(1);

				/*
				 * The line at the top of the screen should always be real
				 * so we can just call redraw_line here
				 */
				redraw_line(env->offset);
			} else {
				redraw_text();
			}
			redraw_tabbar();
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

/**
 * Move the cursor one column left.
 */
void cursor_left(void) {
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

/**
 * Move the cursor one column right.
 */
void cursor_right(void) {

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

/**
 * Move the cursor to the fron the of the line
 */
void cursor_home(void) {
	env->col_no = 1;
	set_history_break();
	set_preferred_column();

	/* Update the status bar */
	redraw_statusbar();

	/* Place the terminal cursor again */
	place_cursor_actual();
}

/**
 * Move the cursor to the end of the line.
 *
 * In INSERT mode, moves one cell right of the end of the line.
 * In NORMAL mode, moves the cursor to the last occupied cell.
 */
void cursor_end(void) {
	env->col_no = env->lines[env->line_no-1]->actual+!!(env->mode == MODE_INSERT);
	set_history_break();
	set_preferred_column();

	/* Update the status bar */
	redraw_statusbar();

	/* Place the terminal cursor again */
	place_cursor_actual();
}

/**
 * Leave INSERT mode
 *
 * If the cursor is too far right, adjust it.
 * Redraw the command line.
 */
void leave_insert(void) {
	if (env->col_no > env->lines[env->line_no-1]->actual) {
		env->col_no = env->lines[env->line_no-1]->actual;
		if (env->col_no == 0) env->col_no = 1;
		set_preferred_column();
	}
	set_history_break();
	env->mode = MODE_NORMAL;
	redraw_commandline();
}

/**
 * Helper for handling smart case sensitivity.
 */
int search_matches(uint32_t a, uint32_t b, int mode) {
	if (mode == 0) {
		return a == b;
	} else if (mode == 1) {
		return tolower(a) == tolower(b);
	}
	return 0;
}

/**
 * Replace text on a given line with other text.
 */
void perform_replacement(int line_no, uint32_t * needle, uint32_t * replacement, int col, int ignorecase, int *out_col) {
	line_t * line = env->lines[line_no-1];
	int j = col;
	while (j < line->actual + 1) {
		int k = j;
		uint32_t * match = needle;
		while (k < line->actual + 1) {
			if (*match == '\0') {
				/* Perform replacement */
				for (uint32_t * n = needle; *n; ++n) {
					line_delete(line, j+1, line_no-1);
				}
				int t = 0;
				for (uint32_t * r = replacement; *r; ++r) {
					char_t _c;
					_c.codepoint = *r;
					_c.flags = 0;
					_c.display_width = codepoint_width(*r);
					line_t * nline = line_insert(line, _c, j + t, line_no -1);
					if (line != nline) {
						env->lines[line_no-1] = nline;
						line = nline;
					}
					t++;
				}

				*out_col = j + t;
				set_modified();
				return;
			}
			if (k == line->actual) break;
			if (!(search_matches(*match, line->text[k].codepoint, ignorecase))) break;
			match++;
			k++;
		}
		j++;
	}
	*out_col = -1;
}

#define COMMAND_HISTORY_MAX 255
unsigned char * command_history[COMMAND_HISTORY_MAX] = {NULL};

/**
 * Add a command to the history. If that command was
 * already in history, it is moved to the front of the list;
 * otherwise, the whole list is shifted backwards and
 * overflow is freed up.
 */
void insert_command_history(char * cmd) {
	/* See if this is already in the history. */
	size_t amount_to_shift = COMMAND_HISTORY_MAX - 1;
	for (int i = 0; i < COMMAND_HISTORY_MAX && command_history[i]; ++i) {
		if (!strcmp((char*)command_history[i], cmd)) {
			free(command_history[i]);
			amount_to_shift = i;
			break;
		}
	}

	/* Remove last entry that will roll off the stack */
	if (amount_to_shift == COMMAND_HISTORY_MAX - 1) {
		if (command_history[COMMAND_HISTORY_MAX-1]) free(command_history[COMMAND_HISTORY_MAX-1]);
	}

	/* Roll the history */
	memmove(&command_history[1], &command_history[0], sizeof(char *) * (amount_to_shift));

	command_history[0] = (unsigned char*)strdup(cmd);
}

/**
 * Add a raw string to a buffer. Convenience wrapper
 * for add_buffer for nil-terminated strings.
 */
static void add_string(char * string) {
	add_buffer((uint8_t*)string,strlen(string));
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

/**
 * Based on vim's :TOhtml
 * Convert syntax-highlighted buffer contents to HTML.
 */
void convert_to_html(void) {
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
		char tmp[10];
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
				sprintf(tmp, "<span class=\"s%d\">", c.flags & 0x1F);
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
	for (int i = 0; i < env->line_count; ++i) {
		recalculate_syntax(env->lines[i],i);
	}
	redraw_all();
}

/**
 * Process a user command.
 */
void process_command(char * cmd) {
	/* Special case ! to run shell commands without parsing tokens */
	int c;

	/* Add command to history */
	insert_command_history(cmd);

	if (*cmd == '!' || *cmd == '`') {
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
				if (*cmd == '!') {
					system(&cmd[1]); /* Yes we can just do this */
				} else {
					char * const args[] = {"python3","-c",&cmd[1],NULL};
					execvp("python3",args);
				}
				exit(1);
			} else if (child < 0) {
				render_error("Failed to fork");
				return;
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
			if (*cmd == '!') {
				system(&cmd[1]);
			} else {
				setenv("PYCMD",&cmd[1],1);
				system("python3 -c \"$PYCMD\"");
			}

			/* Return to the editor, wait for user to press enter. */
			set_unbuffered();
			printf("\n\nPress ENTER to continue.");
			while ((c = bim_getch(), c != ENTER_KEY && c != LINE_FEED));

			/* Redraw the screen */
			redraw_all();
		}

		/* Done processing command */
		return;
	}

	/* Arguments aren't really tokenized, but the first command before a space is extracted */
	char *argv[3]; /* If a specific command wants to tokenize further, it can do that later. */
	int argc = 0;
	if (*cmd) argc++;

	int i = 0;
	char tmp[512] = {0};

	/* Collect up until first space for argv[0] */
	for (char * c = cmd; *c; ++c) {
		if (i == 511) break;
		if (*c == ' ') {
			tmp[i] = '\0';
			argv[1] = c+1;
			if (*argv[1]) argc++;
			break;
		} else {
			tmp[i] = *c;
		}
		i++;
	}
	argv[0] = tmp;
	argv[argc] = NULL;

	if (argc < 1) {
		/* no op */
		return;
	}

	int all_lines = 0;

	if (argv[0][0] == '%') {
		all_lines = 1;
		argv[0]++;
	}

	if (!strcmp(argv[0], "e")) {
		/* e: edit file */
		if (argc > 1) {
			/* This actually opens a new tab */
			open_file(argv[1]);
			update_title();
		} else {
			if (env->modified) {
				render_error("File is modified, can not reload.");
				return;
			}

			buffer_t * old_env = env;
			open_file(env->file_name);
			buffer_t * new_env = env;
			env = old_env;

#define SWAP(T,a,b) do { T x = a; a = b; b = x; } while (0)
			SWAP(line_t **, env->lines, new_env->lines);
			SWAP(int, env->line_count, new_env->line_count);
			SWAP(int, env->line_avail, new_env->line_avail);

			buffer_close(new_env); /* Should probably also free, this needs editing. */
			redraw_all();
		}
	} else if (argv[0][0] == 's' && !isalpha(argv[0][1])) {
		if (!argv[0][1]) {
			render_error("expected substitution argument");
			return;
		}
		/* Substitution */
		int range_top, range_bot;
		if (env->mode == MODE_LINE_SELECTION) {
			range_top = env->start_line < env->line_no ? env->start_line : env->line_no;
			range_bot = env->start_line < env->line_no ? env->line_no : env->start_line;
		} else if (all_lines) {
			range_top = 1;
			range_bot = env->line_count;
		} else {
			range_top = env->line_no;
			range_bot = env->line_no;
		}

		/* Determine replacement parameters */
		char divider = cmd[1];

		char * needle = &cmd[2];
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
			return;
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
				perform_replacement(line, needle_c, replacement_c, col, case_insensitive, &col);
				if (col != -1) replacements++;
				if (!global) break;
			}
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
	} else if (!strcmp(argv[0], "tabnew")) {
		if (argc > 1) {
			open_file(argv[1]);
			update_title();
		} else {
			env = buffer_new();
			setup_buffer(env);
			redraw_all();
			update_title();
		}
	} else if (!strcmp(argv[0], "w")) {
		/* w: write file */
		if (argc > 1) {
			write_file(argv[1]);
		} else {
			write_file(env->file_name);
		}
	} else if (!strcmp(argv[0], "history")) {
		render_commandline_message(""); /* To clear command line */
		for (int i = COMMAND_HISTORY_MAX; i > 1; --i) {
			if (command_history[i-1]) render_commandline_message("%d:%s\n", i-1, command_history[i-1]);
		}
		render_commandline_message("\n");
		redraw_tabbar();
		redraw_commandline();
		int c;
		while ((c = bim_getch())== -1);
		bim_unget(c);
		redraw_all();
	} else if (!strcmp(argv[0], "wq")) {
		/* wq: write file and close buffer; if there's no file to write to, may do weird things */
		write_file(env->file_name);
		close_buffer();
	} else if (!strcmp(argv[0], "q")) {
		/* close buffer if unmodified */
		if (left_buffer && left_buffer == right_buffer) {
			unsplit();
			return;
		}
		if (env->modified) {
			render_error("No write since last change. Use :q! to force exit.");
		} else {
			close_buffer();
		}
		update_title();
	} else if (!strcmp(argv[0], "q!")) {
		/* close buffer without warning if unmodified */
		close_buffer();
		update_title();
	} else if (!strcmp(argv[0], "qa") || !strcmp(argv[0], "qall")) {
		/* Close all */
		try_quit();
	} else if (!strcmp(argv[0], "qa!")) {
		/* Forcefully exit editor */
		while (buffers_len) {
			buffer_close(buffers[0]);
		}
		quit(NULL);
	} else if (!strcmp(argv[0], "tabp")) {
		/* Next tab */
		previous_tab();
		update_title();
	} else if (!strcmp(argv[0], "tabn")) {
		/* Previous tab */
		next_tab();
		update_title();
	} else if (!strcmp(argv[0], "git")) {
		if (argc < 2) {
			render_status_message("git=%d", env->checkgitstatusonwrite);
		} else {
			env->checkgitstatusonwrite = !!atoi(argv[1]);
			if (env->checkgitstatusonwrite && !env->modified && env->file_name) {
				git_examine(env->file_name);
				redraw_text();
			}
		}
	} else if (!strcmp(argv[0], "colorgutter")) {
		if (argc < 2) {
			render_status_message("colorgutter=%d", global_config.color_gutter);
		} else {
			global_config.color_gutter = !!atoi(argv[1]);
			redraw_text();
		}
	} else if (!strcmp(argv[0], "indent")) {
		env->indent = 1;
		redraw_statusbar();
	} else if (!strcmp(argv[0], "noindent")) {
		env->indent = 0;
		redraw_statusbar();
	} else if (!strcmp(argv[0], "cursorcolumn")) {
		render_status_message("cursorcolumn=%d", env->preferred_column);
	} else if (!strcmp(argv[0], "noh")) {
		if (global_config.search) {
			free(global_config.search);
			global_config.search = NULL;
			for (int i = 0; i < env->line_count; ++i) {
				recalculate_syntax(env->lines[i],i);
			}
			redraw_text();
		}
	} else if (!strcmp(argv[0], "help")) {
		/*
		 * The repeated calls to redraw_commandline here make use
		 * of scrolling to draw this multiline help message on
		 * the same background as the command line.
		 */
		render_commandline_message(""); /* To clear command line */
		render_commandline_message("\n");
		#ifdef __toaru__
		render_commandline_message(" \033[1mbim - The standard ToaruOS Text Editor\033[22m\n");
		#else
		render_commandline_message(" \033[1mbim - a text editor \033[22m\n");
		#endif
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
		render_commandline_message(" %s\n", BIM_COPYRIGHT);
		render_commandline_message("\n");
		/* Redrawing the tabbar makes it look like we just shifted the whole view up */
		redraw_tabbar();
		redraw_commandline();
		/* Wait for a character so we can redraw the screen before continuing */
		int c;
		while ((c = bim_getch())== -1);
		/* Make sure that key press actually gets used */
		bim_unget(c);
		/*
		 * Redraw everything to hide the help message and get the
		 * upper few lines of text on screen again
		 */
		redraw_all();
	} else if (!strcmp(argv[0], "theme") || !strcmp(argv[0],"colorscheme")) {
		if (argc < 2) {
			render_status_message("theme=%s", current_theme);
			return;
		}
		for (struct theme_def * d = themes; d->name; ++d) {
			if (!strcmp(argv[1], d->name)) {
				d->load();
				redraw_all();
				return;
			}
		}
	} else if (!strcmp(argv[0], "splitpercent")) {
		if (argc < 2) {
			render_status_message("splitpercent=%d", global_config.split_percent);
			return;
		} else {
			global_config.split_percent = atoi(argv[1]);
			if (left_buffer) {
				update_split_size();
				redraw_all();
			}
		}
	} else if (!strcmp(argv[0], "split")) {
		/* Force split the current buffer; will become unsplit under certain circumstances */
		buffer_t * original = env;
		if (argc > 1) {
			int is_not_number = 0;
			for (char * c = argv[1]; *c; ++c) is_not_number |= !isdigit(*c);
			if (is_not_number) {
				open_file(argv[1]);
				right_buffer = buffers[buffers_len-1];
			} else {
				int other = atoi(argv[1]);
				if (other >= buffers_len || other < 0) {
					render_error("Invalid buffer number: %d", other);
					return;
				}
				right_buffer = buffers[other];
			}
		} else {
			right_buffer = original;
		}
		left_buffer = original;
		update_split_size();
		redraw_all();
	} else if (!strcmp(argv[0], "unsplit")) {
		unsplit();
	} else if (!strcmp(argv[0], "syntax")) {
		if (argc < 2) {
			render_status_message("syntax=%s", env->syntax ? env->syntax->name : "none");
			return;
		}
		set_syntax_by_name(argv[1]);
	} else if (!strcmp(argv[0], "recalc")) {
		for (int i = 0; i < env->line_count; ++i) {
			env->lines[i]->istate = -1;
		}
		env->loading = 1;
		for (int i = 0; i < env->line_count; ++i) {
			recalculate_syntax(env->lines[i],i);
		}
		env->loading = 0;
		redraw_all();
	} else if (!strcmp(argv[0], "tabs")) {
		env->tabs = 1;
		redraw_statusbar();
	} else if (!strcmp(argv[0], "spaces")) {
		env->tabs = 0;
		redraw_statusbar();
	} else if (!strcmp(argv[0], "tabstop")) {
		if (argc < 2) {
			render_status_message("tabstop=%d", env->tabstop);
		} else {
			int t = atoi(argv[1]);
			if (t > 0 && t < 32) {
				env->tabstop = t;
				for (int i = 0; i < env->line_count; ++i) {
					recalculate_tabs(env->lines[i]);
				}
				redraw_all();
			} else {
				render_error("Invalid tabstop: %s", argv[1]);
			}
		}
	} else if (!strcmp(argv[0], "clearyank")) {
		if (global_config.yanks) {
			for (unsigned int i = 0; i < global_config.yank_count; ++i) {
				free(global_config.yanks[i]);
			}
			free(global_config.yanks);
			global_config.yanks = NULL;
			global_config.yank_count = 0;
			redraw_statusbar();
		}
	} else if (!strcmp(argv[0], "padding")) {
		if (argc < 2) {
			render_status_message("padding=%d", global_config.cursor_padding);
		} else {
			global_config.cursor_padding = atoi(argv[1]);
			place_cursor_actual();
		}
	} else if (!strcmp(argv[0], "smartcase")) {
		if (argc < 2) {
			render_status_message("smartcase=%d", global_config.smart_case);
		} else {
			global_config.smart_case = atoi(argv[1]);
			place_cursor_actual();
		}
	} else if (!strcmp(argv[0], "hlparen")) {
		if (argc < 2) {
			render_status_message("hlparen=%d", global_config.highlight_parens);
		} else {
			global_config.highlight_parens = atoi(argv[1]);
			for (int i = 0; i < env->line_count; ++i) {
				recalculate_syntax(env->lines[i],i);
			}
			redraw_text();
			place_cursor_actual();
		}
	} else if (!strcmp(argv[0], "hlcurrent")) {
		if (argc < 2) {
			render_status_message("hlcurrent=%d", global_config.highlight_current_line);
		} else {
			global_config.highlight_current_line = atoi(argv[1]);
			if (!global_config.highlight_current_line) {
				for (int i = 0; i < env->line_count; ++i) {
					env->lines[i]->is_current = 0;
				}
			}
			redraw_text();
			place_cursor_actual();
		}
	} else if (!strcmp(argv[0], "crnl")) {
		if (argc < 2) {
			render_status_message("crnl=%d", env->crnl);
		} else {
			env->crnl = !!atoi(argv[1]);
			redraw_statusbar();
		}
	} else if (!strcmp(argv[0], "numbers")) {
		if (argc < 2) {
			render_status_message("numbers=%d", global_config.numbers);
		} else {
			global_config.numbers = !!atoi(argv[1]);
			redraw_all();
		}
	} else if (!strcmp(argv[0], "relativenumber")) {
		if (argc < 2) {
			render_status_message("relativenumber=%d", global_config.relative_lines);
		} else {
			global_config.relative_lines = atoi(argv[1]);
			if (!global_config.relative_lines) {
				for (int i = 0; i < env->line_count; ++i) {
					env->lines[i]->is_current = 0;
				}
			}
			redraw_text();
			place_cursor_actual();
		}
	} else if (!strcmp(argv[0],"TOhtml") || !strcmp(argv[0],"tohtml")) { /* TOhtml is for vim compatibility */
		convert_to_html();
	} else if (!strcmp(argv[0],"buffers")) {
		for (int i = 0; i < buffers_len; ++i) {
			render_commandline_message("%d: %s\n", i, buffers[i]->file_name ? buffers[i]->file_name : "(no name)");
		}
		redraw_tabbar();
		redraw_commandline();
		int c;
		while ((c = bim_getch())== -1);
		bim_unget(c);
		redraw_all();
	} else if (argv[0][0] == '-' && isdigit(argv[0][1])) {
		global_config.break_from_selection = 1;
		goto_line(env->line_no-atoi(&argv[0][1]));
	} else if (argv[0][0] == '+' && isdigit(argv[0][1])) {
		global_config.break_from_selection = 1;
		goto_line(env->line_no+atoi(&argv[0][1]));
	} else if (isdigit(*argv[0])) {
		/* Go to line number */
		global_config.break_from_selection = 1;
		goto_line(atoi(argv[0]));
	} else {
		/* Unrecognized command */
		render_error("Not an editor command: %s", argv[0]);
	}
}

/**
 * Wrap strcmp for use with qsort.
 */
int compare_str(const void * a, const void * b) {
	return strcmp(*(const char **)a, *(const char **)b);
}

/**
 * Tab completion for command mode.
 */
void command_tab_complete(char * buffer) {
	/* Figure out which argument this is and where it starts */
	int arg = 0;
	char * buf = strdup(buffer);
	char * b = buf;

	char * args[32];

	int candidate_count= 0;
	int candidate_space = 4;
	char ** candidates = malloc(sizeof(char*)*candidate_space);

	/* Accept whitespace before first argument */
	while (*b == ' ') b++;
	char * start = b;
	args[0] = start;
	while (*b && *b != ' ') b++;
	while (*b) {
		while (*b == ' ') {
			*b = '\0';
			b++;
		}
		start = b;
		arg++;
		if (arg < 32) {
			args[arg] = start;
		}
		while (*b && *b != ' ') b++;
	}

	/**
	 * Check a possible candidate and add it to the
	 * candidates list, expanding as necessary,
	 * if it matches for the current argument.
	 */
#define add_candidate(candidate) \
	do { \
		char * _arg = args[arg]; \
		int r = strncmp(_arg, candidate, strlen(_arg)); \
		if (!r) { \
			if (candidate_count == candidate_space) { \
				candidate_space *= 2; \
				candidates = realloc(candidates,sizeof(char *) * candidate_space); \
			} \
			candidates[candidate_count] = strdup(candidate); \
			candidate_count++; \
		} \
	} while (0)

	if (arg == 0) {
		/* Complete command names */
		for (char ** c = syn_bimcmd_keywords; *c; ++c) {
			add_candidate(*c);
		}
		goto _accept_candidate;
	}

	if (arg == 1 && !strcmp(args[0], "syntax")) {
		/* Complete syntax options */
		add_candidate("none");
		for (struct syntax_definition * s = syntaxes; s->name; ++s) {
			add_candidate(s->name);
		}
		goto _accept_candidate;
	}

	if (arg == 1 && (!strcmp(args[0], "theme") || !strcmp(args[0], "colorscheme"))) {
		/* Complete color theme names */
		for (struct theme_def * s = themes; s->name; ++s) {
			add_candidate(s->name);
		}
		goto _accept_candidate;
	}

	if (arg == 1 && (!strcmp(args[0], "e") || !strcmp(args[0], "tabnew") || !strcmp(args[0],"split") || !strcmp(args[0],"w"))) {
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
				dirp = opendir(tmp);
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

		struct dirent * ent = readdir(dirp);
		while (ent != NULL) {
			if (ent->d_name[0] != '.' || (last_slash ? (last_slash[1] == '.') : (tmp[0] == '.'))) {
				struct stat statbuf;
				/* Figure out if this file is a directory */
				if (last_slash) {
					char * x = malloc(strlen(tmp) + 1 + strlen(ent->d_name) + 1);
					snprintf(x, strlen(tmp) + 1 + strlen(ent->d_name) + 1, "%s/%s",tmp,ent->d_name);
					stat(x, &statbuf);
					free(x);
				} else {
					stat(ent->d_name, &statbuf);
				}

				/* Build the complete argument name to tab complete */
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
				}
				add_candidate(s);
			}
			ent = readdir(dirp);
		}
		closedir(dirp);
		free(tmp);
		goto _accept_candidate;
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
		for (unsigned int i = 0; i < strlen(candidates[0]); ++i) {
			*cstart = candidates[0][i];
			cstart++;
		}
		*cstart = '\0';
	} else {
		/* Sort candidates */
		qsort(candidates, candidate_count, sizeof(candidates[0]), compare_str);
		/* Print candidates in status bar */
		char * tmp = malloc(global_config.term_width+1);
		memset(tmp, 0, global_config.term_width+1);
		int offset = 0;
		for (int i = 0; i < candidate_count; ++i) {
			if (offset + 1 + (signed)strlen(candidates[i]) > global_config.term_width - 5) {
				strcat(tmp, "...");
				break;
			}
			if (offset > 0) {
				strcat(tmp, " ");
				offset++;
			}
			strcat(tmp, candidates[i]);
			offset += strlen(candidates[i]);
		}
		render_status_message("%s", tmp);
		free(tmp);

		/* Complete to longest common substring */
		char * cstart = (buffer) + (start - buf);
		for (int i = 0; i < 1023 /* max length of command */; i++) {
			for (int j = 1; j < candidate_count; ++j) {
				if (candidates[0][i] != candidates[j][i]) goto _reject;
			}
			*cstart = candidates[0][i];
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
		free(candidates[i]);
	}

done:
	free(candidates);
	free(buf);
}

/**
 * Handle complex keyboard escapes when taking in a command.
 * This allows us to not muck up the command input and also
 * handle things like up/down arrow keys to go through history.
 */
int handle_command_escape(int * this_buf, int * timeout, int c, int * arg) {
	if (*timeout >=  1 && this_buf[*timeout-1] == '\033' && c == '\033') {
		this_buf[*timeout] = c;
		(*timeout)++;
		return 1;
	}
	if (*timeout >= 1 && this_buf[*timeout-1] == '\033' && c != '[') {
		*timeout = 0;
		bim_unget(c);
		return 1;
	}
	if (*timeout >= 1 && this_buf[*timeout-1] == '\033' && c == '[') {
		*timeout = 1;
		this_buf[*timeout] = c;
		(*timeout)++;
		return 0;
	}
	if (*timeout >= 2 && this_buf[0] == '\033' && this_buf[1] == '[' &&
			(isdigit(c) || c == ';')) {
		this_buf[*timeout] = c;
		(*timeout)++;
		return 0;
	}
	if (*timeout >= 2 && this_buf[0] == '\033' && this_buf[1] == '[') {
		int out = 0;
		if (*timeout >= 3) *arg = this_buf[*timeout-1];
		switch (c) {
			case 'M':
				out = 2;
				break;

			case 'A': // up
			case 'B': // down
			case 'C': // right
			case 'D': // left
			case 'H': // home
			case 'F': // end
				out = c;
				break;
		}
		*timeout = 0;
		return out;
	}

	*timeout = 0;
	return 0;

}

/**
 * Macros for use in command mode.
 */
#define _syn_command() do { env->syntax = _syn_bim; } while (0)
#define _syn_restore() do { env->syntax = _syn; } while (0)
#define _place_cursor() do { \
	int x = 2 + _left_gutter - offset; \
	for (int i = 0; i < col_no - 1; ++i) { \
		char_t * c = &command_buffer->text[i]; \
		x += c->display_width; \
	} \
	if (x > global_config.term_width - 1) { \
		int diff = x - (global_config.term_width - 1); \
		offset += diff; \
		x -= diff; \
		redraw = 1; \
	} \
	if (x < 2 + _left_gutter) { \
		int diff = (2 + _left_gutter) - x; \
		offset -= diff; \
		x += diff; \
		redraw = 1; \
	} \
	place_cursor(40, global_config.term_height); \
	place_cursor(x, global_config.term_height); \
	show_cursor(); \
} while (0)

#define _restore_history(point) do { \
	unsigned char * t = command_history[point]; \
	col_no = 1; \
	command_buffer->actual = 0; \
	_syn_command(); \
	state = 0; \
	while (*t) { \
		if (!decode(&state, &c, *t)) { \
			char_t _c = {codepoint_width(c), 0, c}; \
			line_insert(command_buffer, _c, col_no - 1, -1); \
			col_no++; \
		} else if (state == UTF8_REJECT) state = 0; \
		t++; \
	} \
	_syn_restore(); \
} while (0)

/**
 * Draw the command buffer and any prefix.
 */
#define _set_cmdline() do { \
	place_cursor(1, global_config.term_height); \
	paint_line(COLOR_BG); \
	set_colors(COLOR_ALT_FG, COLOR_BG); \
	_left_gutter = 0; \
	if (env->mode == MODE_LINE_SELECTION) { \
		_left_gutter = printf("(LINE %d:%d)", \
			(env->start_line < env->line_no) ? env->start_line : env->line_no, \
			(env->start_line < env->line_no) ? env->line_no : env->start_line); \
	} else if (env->mode == MODE_COL_SELECTION) { \
		_left_gutter = printf("(COL %d:%d %d)", \
			(env->start_line < env->line_no) ? env->start_line : env->line_no, \
			(env->start_line < env->line_no) ? env->line_no : env->start_line, \
			(env->sel_col)); \
	} else if (env->mode == MODE_CHAR_SELECTION) { \
		_left_gutter = printf("(CHAR)"); \
	} \
	if (offset) { \
		set_colors(COLOR_ALT_FG, COLOR_ALT_BG); \
		printf("<"); \
	} else { \
		set_colors(COLOR_FG, COLOR_BG); \
		printf(":"); \
	} \
	render_line(command_buffer, global_config.term_width-1-_left_gutter, offset, -1); \
	redraw = 0; \
	_place_cursor(); \
} while (0)

/**
 * Command Mode
 *
 * Accept a command to run.
 * !-prefixed commands will be run with the system shell.
 *
 * Supports tab completion of filenames, arguments, etc.
 * Full line editing with syntax highlighting.
 */
void command_mode(void) {
	int offset = 0;
	int col_no = 1;
	int redraw = 0;
	int _left_gutter;

	line_t * command_buffer = calloc(sizeof(line_t)+sizeof(char_t)*32,1);
	command_buffer->available = 32;

	_set_cmdline();

	struct syntax_definition * _syn = env->syntax;
	struct syntax_definition * _syn_bim = NULL;
	for (struct syntax_definition * s = syntaxes; s->name; ++s) {
		if (!strcmp(s->name,"bim-command")) {
			_syn_bim = s;
			break;
		}
	}

	int cin;
	uint32_t c;
	int timeout = 0;
	int this_buf[20];
	uint32_t istate = 0;

	int history_point = -1;

	while ((cin = bim_getch_timeout((redraw ? 10 : 200)))) {
		if (cin == -1) {
			if (redraw) {
				_set_cmdline();
			}
			if (timeout && this_buf[timeout-1] == '\033') {
				goto _leave;
			}
			timeout = 0;
			continue;
		}
		if (!decode(&istate, &c, cin)) {
			if (timeout == 0) {
				switch (c) {
					case '\033':
						if (timeout == 0) {
							this_buf[timeout] = c;
							timeout++;
						}
						break;
					case 3:
						goto _leave;
					case DELETE_KEY:
					case BACKSPACE_KEY:
						if (col_no <= 1) {
							if (command_buffer->actual == 0) goto _leave;
							else break;
						}
						_syn_command();
						line_delete(command_buffer, col_no - 1, -1);
						_syn_restore();
						col_no--;
						offset = 0;
						redraw = 1;
						break;
					case ENTER_KEY:
					case LINE_FEED:
						{
							/* First count how many bytes we need */
							size_t size = 0;
							for (int i = 0; i < command_buffer->actual; ++i) {
								char tmp[8] = {0};
								size += to_eight(command_buffer->text[i].codepoint, tmp);
							}
							char * tmp = malloc(size + 8); /* for overflow from to_eight */
							char * t = tmp;
							for (int i = 0; i < command_buffer->actual; ++i) {
								t += to_eight(command_buffer->text[i].codepoint, t);
							}
							*t = '\0';
							free(command_buffer);
							process_command(tmp);
							free(tmp);
						}
						return;
					case 23: /* ^W */
						_syn_command();
						while (col_no > 1 &&
						       (command_buffer->text[col_no-2].codepoint == ' ' ||
						        command_buffer->text[col_no-2].codepoint == '/')) {
							line_delete(command_buffer, col_no - 1, -1);
							col_no--;
						}
						while (col_no > 1 &&
						       command_buffer->text[col_no-2].codepoint != ' ' &&
						       command_buffer->text[col_no-2].codepoint != '/') {
							line_delete(command_buffer, col_no - 1, -1);
							col_no--;
						}
						_syn_restore();
						redraw = 1;
						break;
					case '\t':
						{
							char * tmp = calloc(1024,1);
							char * t = tmp;
							for (int i = 0; i < col_no-1; ++i) {
								t += to_eight(command_buffer->text[i].codepoint, t);
							}
							*t = '\0';
							_syn_command();
							while (col_no > 1) {
								line_delete(command_buffer, col_no - 1, -1);
								col_no--;
							}
							_syn_restore();
							command_tab_complete(tmp);
							_syn_command();
							uint32_t state = 0, c= 0;
							t = tmp;
							while (*t) {
								if (!decode(&state, &c, *t)) {
									char_t _c = {codepoint_width(c), 0, c};
									command_buffer = line_insert(command_buffer, _c, col_no - 1, -1);
									col_no++;
								}
								t++;
							}
							_syn_restore();
							free(tmp);
							redraw = 1;
						}
						break;
					default:
						{
							char_t _c = {codepoint_width(c), 0, c};
							_syn_command();
							command_buffer = line_insert(command_buffer, _c, col_no - 1, -1);
							_syn_restore();
							col_no++;
						}
						redraw = 1;
						break;
				}
			} else {
				int arg = 0;
				switch (handle_command_escape(this_buf,&timeout,c,&arg)) {
					case 1:
						bim_unget(c);
						goto _leave;
					case 2:
						/* It would be nice to handle the mouse here... */
						bim_getch();
						bim_getch();
						bim_getch();
						break;
					case 'A':
						/* Load from history */
						if (command_history[history_point+1]) {
							_restore_history(history_point+1);
							history_point++;
							redraw = 1;
						}
						break;
					case 'B':
						if (history_point > 0) {
							history_point--;
							_restore_history(history_point);
							redraw = 1;
						} else {
							history_point = -1;
							col_no = 1;
							command_buffer->actual = 0;
							redraw = 1;
						}
						break;
					case 'D':
						if (arg == '5') {
							if (col_no > 1) {
								redraw = 1;
								do {
									col_no--;
								} while (isspace(command_buffer->text[col_no-1].codepoint) && col_no > 1);
								if (col_no == 1) break;
								do {
									col_no--;
								} while (!isspace(command_buffer->text[col_no-1].codepoint) && col_no > 1);
								if (isspace(command_buffer->text[col_no-1].codepoint) && col_no < command_buffer->actual) col_no++;
							}
						} else {
							if (col_no > 1) col_no--;
							redraw = 1;
						}
						break;
					case 'C':
						if (arg == '5') {
							if (col_no < command_buffer->actual) {
								redraw = 1;
								do {
									col_no++;
									if (col_no > command_buffer->actual) { col_no = command_buffer->actual+1; break; }
								} while (!isspace(command_buffer->text[col_no-1].codepoint) && col_no <= command_buffer->actual);
								do {
									col_no++;
									if (col_no > command_buffer->actual) { col_no = command_buffer->actual+1; break; }
								} while (isspace(command_buffer->text[col_no-1].codepoint) && col_no <= command_buffer->actual);
								if (col_no > command_buffer->actual) { col_no = command_buffer->actual+1; }
							}
						} else {
							if (col_no < command_buffer->actual+1) col_no++;
							redraw = 1;
						}
						break;
					case 'H':
						col_no = 1;
						redraw = 1;
						break;
					case 'F':
						col_no = command_buffer->actual + 1;
						redraw = 1;
						break;
				}
			}
		} else if (istate == UTF8_REJECT) {
			istate = 0;
		}
	}

_leave:
	free(command_buffer);
	redraw_all();
	place_cursor_actual();
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
void find_match(int from_line, int from_col, int * out_line, int * out_col, uint32_t * str) {
	int col = from_col;

	int ignorecase = smart_case(str);

	for (int i = from_line; i <= env->line_count; ++i) {
		line_t * line = env->lines[i - 1];

		int j = col - 1;
		while (j < line->actual + 1) {
			int k = j;
			uint32_t * match = str;
			while (k < line->actual + 1) {
				if (*match == '\0') {
					*out_line = i;
					*out_col = j + 1;
					return;
				}
				if (k == line->actual) break;
				if (!(search_matches(*match, line->text[k].codepoint, ignorecase))) break;
				match++;
				k++;
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
			int k = j;
			uint32_t * match = str;
			while (k < line->actual + 1) {
				if (*match == '\0') {
					*out_line = i;
					*out_col = j + 1;
					return;
				}
				if (k == line->actual) break;
				if (!(search_matches(*match, line->text[k].codepoint, ignorecase))) break;
				match++;
				k++;
			}
			j--;
		}
		col = (i > 1) ? (env->lines[i-2]->actual) : -1;
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
	int ignorecase = smart_case(global_config.search);
	while (j < line->actual) {
		int k = j;
		uint32_t * match = global_config.search;
		while (k < line->actual+1) {
			if (*match == '\0') {
				for (int i = j; i < k; ++i) {
					line->text[i].flags |= FLAG_SEARCH;
				}
				break;
			}
			if (k == line->actual) break;
			if (!(search_matches(*match, line->text[k].codepoint, ignorecase))) break;
			match++;
			k++;
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
	int line = -1, col = -1, _line = 1, _col = 1;
	do {
		find_match(_line, _col, &line, &col, buffer);
		if (line != -1) {
			line_t * l = env->lines[line-1];
			uint32_t * t = buffer;
			for (int i = col; *t; ++i, ++t) {
				l->text[i-1].flags |= FLAG_SEARCH;
			}
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
	if (redraw_buffer != -1) {
		printf(redraw_buffer == 1 ? "/" : "?");
		uint32_t * c = buffer;
		while (*c) {
			char tmp[7] = {0}; /* Max six bytes, use 7 to ensure last is always nil */
			to_eight(*c, tmp);
			printf("%s", tmp);
			c++;
		}
	}
}

/**
 * Search mode
 *
 * Search text for substring match.
 */
void search_mode(int direction) {
	uint32_t c;
	uint32_t buffer[1024] = {0};
	int  buffer_len = 0;

	/* utf-8 decoding */

	/* Remember where the cursor is so we can cancel */
	int prev_line = env->line_no;
	int prev_col  = env->col_no;
	int prev_coffset = env->coffset;
	int prev_offset = env->offset;

	redraw_commandline();
	printf(direction == 1 ? "/" : "?");
	if (global_config.search) {
		store_cursor();
		set_colors(COLOR_ALT_FG, COLOR_BG);
		uint32_t * c = global_config.search;
		while (*c) {
			char tmp[7] = {0}; /* Max six bytes, use 7 to ensure last is always nil */
			to_eight(*c, tmp);
			printf("%s", tmp);
			c++;
		}
		restore_cursor();
		set_colors(COLOR_FG, COLOR_BG);
	}
	show_cursor();

	uint32_t state = 0;
	int cin;

	while ((cin = bim_getch())) {
		if (cin == -1) {
			/* Time out */
			continue;
		}
		if (!decode(&state, &c, cin)) {
			if (c == '\033' || c == 3) {
				/* Cancel search */
				env->line_no = prev_line;
				env->col_no  = prev_col;
				/* Unhighlight search matches */
				for (int i = 0; i < env->line_count; ++i) {
					for (int j = 0; j < env->lines[i]->actual; ++j) {
						env->lines[i]->text[j].flags &= (~FLAG_SEARCH);
					}
					rehighlight_search(env->lines[i]);
				}
				redraw_all();
				break;
			} else if (c == ENTER_KEY || c == LINE_FEED) {
				/* Exit search */
				if (!buffer_len) {
					if (global_config.search) {
						search_next();
					}
					break;
				}
				if (global_config.search) {
					free(global_config.search);
				}
				global_config.search = malloc((buffer_len + 1) * sizeof(uint32_t));
				memcpy(global_config.search, buffer, (buffer_len + 1) * sizeof(uint32_t));
				break;
			} else if (c == BACKSPACE_KEY || c == DELETE_KEY) {
				/* Backspace, delete last character in search buffer */
				if (buffer_len > 0) {
					buffer_len -= 1;
					buffer[buffer_len] = '\0';
					/* Search from beginning to find first match */
					int line = -1, col = -1;
					if (direction == 1) {
						find_match(prev_line, prev_col, &line, &col, buffer);
					} else {
						find_match_backwards(prev_line, prev_col, &line, &col, buffer);
					}

					if (line != -1) {
						env->col_no = col;
						env->line_no = line;
						set_preferred_column();
					}

					draw_search_match(buffer, direction);

				} else {
					/* If backspaced through entire search term, cancel search */
					redraw_commandline();
					env->coffset = prev_coffset;
					env->offset = prev_offset;
					env->col_no = prev_col;
					set_preferred_column();
					env->line_no = prev_line;
					redraw_all();
					break;
				}
			} else {
				/* Regular character */
				buffer[buffer_len] = c;
				buffer_len++;
				buffer[buffer_len] = '\0';
				char tmp[7] = {0}; /* Max six bytes, use 7 to ensure last is always nil */
				to_eight(c, tmp);
				printf("%s", tmp);

				/* Find the next search match */
				int line = -1, col = -1;
				if (direction == 1) {
					find_match(prev_line, prev_col, &line, &col, buffer);
				} else {
					find_match_backwards(prev_line, prev_col, &line, &col, buffer);
				}

				if (line != -1) {
					env->col_no = col;
					env->line_no = line;
					set_preferred_column();
				} else {
					env->coffset = prev_coffset;
					env->offset = prev_offset;
					env->col_no = prev_col;
					set_preferred_column();
					env->line_no = prev_line;
				}
				draw_search_match(buffer, direction);
			}
			show_cursor();
		} else if (state == UTF8_REJECT) {
			state = 0;
		}
	}
}

/**
 * Find the next search result, or loop back around if at the end.
 */
void search_next(void) {
	if (!global_config.search) return;
	if (env->coffset) env->coffset = 0;
	int line = -1, col = -1;
	find_match(env->line_no, env->col_no+1, &line, &col, global_config.search);

	if (line == -1) {
		find_match(1,1, &line, &col, global_config.search);
		if (line == -1) return;
	}

	env->col_no = col;
	env->line_no = line;
	set_preferred_column();
	draw_search_match(global_config.search, -1);
}

/**
 * Find the previous search result, or loop to the end of the file.
 */
void search_prev(void) {
	if (!global_config.search) return;
	if (env->coffset) env->coffset = 0;
	int line = -1, col = -1;
	find_match_backwards(env->line_no, env->col_no-1, &line, &col, global_config.search);

	if (line == -1) {
		find_match_backwards(env->line_count, env->lines[env->line_count-1]->actual, &line, &col, global_config.search);
		if (line == -1) return;
	}

	env->col_no = col;
	env->line_no = line;
	set_preferred_column();
	draw_search_match(global_config.search, -1);
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
void use_left_buffer(void) {
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
void use_right_buffer(void) {
	if (left_buffer == right_buffer && env->left == 0) {
		view_left_offset = env->offset;
		env->left = env->width;
		env->width = global_config.term_width - env->width;
		env->offset = view_right_offset;
	}
	env = right_buffer;
	update_title();
}

/**
 * Handle mouse event
 */
void handle_mouse(void) {
	int buttons = bim_getch() - 32;
	int x = bim_getch() - 32;
	int y = bim_getch() - 32;

	if (buttons == 64) {
		/* Scroll up */
		if (global_config.shift_scrolling) {
			env->loading = 1;
			int shifted = 0;
			for (int i = 0; i < global_config.scroll_amount; ++i) {
				if (env->offset > 0) {
					env->offset--;
					if (env->line_no > env->offset + global_config.term_height - global_config.bottom_size - 1 - global_config.cursor_padding) {
						cursor_up();
					}
					shifted++;
				}
			}
			env->loading = 0;
			if (!shifted) return;
			if (global_config.can_scroll && !left_buffer) {
				shift_down(shifted);
				for (int i = 0; i < shifted; ++i) {
					redraw_line(env->offset+i);
				}
			} else {
				redraw_text();
			}
			redraw_tabbar();
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
				shift_up(shifted);
				int l = global_config.term_height - global_config.bottom_size - 1;
				for (int i = 0; i < shifted; ++i) {
					if (env->offset + l - i < env->line_count + 1) {
						redraw_line(env->offset + l-1-i);
					} else {
						draw_excess_line(l - 1 - i);
					}
				}
			} else {
				redraw_text();
			}
			redraw_tabbar();
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

		if (y == 1) {
			/* Pick from tabs */
			int _x = 0;
			for (int i = 0; i < buffers_len; i++) {
				buffer_t * _env = buffers[i];
				char tmp[64];
				_x += draw_tab_name(_env, tmp);
				if (_x >= x) {
					if (left_buffer && buffers[i] != left_buffer && buffers[i] != right_buffer) unsplit();
					env = buffers[i];
					redraw_all();
					update_title();
					return;
				}
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
		int line_no = y + env->offset - 1;
		int col_no = -1;

		if (line_no > env->line_count) {
			line_no = env->line_count;
		}

		if (line_no != env->line_no) {
			env->coffset = 0;
		}

		/* Account for the left hand gutter */
		int num_size = num_width() + 3;
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
 * Append a character at the current cursor point.
 */
void insert_char(unsigned int c) {
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
	set_modified();
}

/**
 * Replace a single character at the current cursor point
 */
void replace_char(unsigned int c) {
	if (env->col_no < 1 || env->col_no > env->lines[env->line_no-1]->actual) return;

	char_t _c;
	_c.codepoint = c;
	_c.flags = 0;
	_c.display_width = codepoint_width(c);

	line_replace(env->lines[env->line_no-1], _c, env->col_no-1, env->line_no-1);

	redraw_line(env->line_no-1);
	set_modified();
}

/**
 * Undo a history entry.
 */
void undo_history(void) {
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
	for (int i = 0; i < env->line_count; ++i) {
		recalculate_syntax(env->lines[i],i);
	}
	place_cursor_actual();
	update_title();
	redraw_all();
	render_commandline_message("%d character%s, %d line%s changed",
			count_chars, (count_chars == 1) ? "" : "s",
			count_lines, (count_lines == 1) ? "" : "s");
}

/**
 * Replay a history entry.
 */
void redo_history(void) {
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
	for (int i = 0; i < env->line_count; ++i) {
		recalculate_syntax(env->lines[i],i);
	}
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

/**
 * Move the cursor the start of the previous word.
 */
void word_left(void) {
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

/**
 * Move cursor to the start of the previous "WORD".
 */
void big_word_left(void) {
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

/**
 * Word right
 */
void word_right(void) {
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

/**
 * Word right
 */
void big_word_right(void) {
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

/**
 * Backspace from the current cursor position.
 */
void delete_at_cursor(void) {
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

/**
 * Delete a "word"; the logic here is a bit complex, but it attempts to do
 * what vim does when you hit ^W (and it's what we bind ^W to as well)
 */
void delete_word(void) {
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

/**
 * Break the current line in two at the current cursor position.
 */
void insert_line_feed(void) {
	if (env->col_no == env->lines[env->line_no - 1]->actual + 1) {
		env->lines = add_line(env->lines, env->line_no);
	} else {
		env->lines = split_line(env->lines, env->line_no-1, env->col_no - 1);
	}
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

/**
 * Yank lines between line start and line end (which may be in either order)
 */
void yank_lines(int start, int end) {
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

/**
 * Delete forward in the column selection or insert modes.
 */
void delete_at_column(int direction) {
	if (direction == -1 && env->sel_col <= 0) return;

	int prev_width = 0;
	for (int i = env->line_no; i <= env->start_line; i++) {
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


/**
 * Handle shared escape keys (mostly navigation)
 */
int handle_escape(int * this_buf, int * timeout, int c) {
	if (*timeout >=  1 && this_buf[*timeout-1] == '\033' && c == '\033') {
		this_buf[*timeout] = c;
		(*timeout)++;
		return 1;
	}
	if (*timeout >= 1 && this_buf[0] == '\033' && c == 'O') {
		this_buf[*timeout] = c;
		(*timeout)++;
		return 0;
	}
	if (*timeout >= 2 && this_buf[0] == '\033' && this_buf[1] == 'O') {
		switch (c) {
			case 'P': /* F1 */
				/* Toggle display of line numbers */
				global_config.numbers = !global_config.numbers;
				redraw_all();
				place_cursor_actual();
				break;
			case 'Q': /* F2 */
				/* Toggle indent mode (why F2? personal vim config) */
				env->indent = !env->indent;
				redraw_statusbar();
				place_cursor_actual();
				break;
		}
		*timeout = 0;
		return 0;
	}
	if (*timeout >= 1 && this_buf[*timeout-1] == '\033' && c != '[') {
		*timeout = 0;
		bim_unget(c);
		return 1;
	}
	if (*timeout >= 1 && this_buf[*timeout-1] == '\033' && c == '[') {
		*timeout = 1;
		this_buf[*timeout] = c;
		(*timeout)++;
		return 0;
	}
	if (*timeout >= 2 && this_buf[0] == '\033' && this_buf[1] == '[' &&
			(isdigit(c) || c == ';')) {
		this_buf[*timeout] = c;
		(*timeout)++;
		return 0;
	}
	if (*timeout >= 2 && this_buf[0] == '\033' && this_buf[1] == '[') {
		switch (c) {
			case 'M':
				handle_mouse();
				break;
			case 'A': // up
				cursor_up();
				break;
			case 'B': // down
				cursor_down();
				break;
			case 'C': // right
				if (this_buf[*timeout-1] == '5') { // ctrl
					big_word_right();
				} else if (this_buf[*timeout-1] == '2') { //shift
					word_right();
				} else if (this_buf[*timeout-1] == '3') { //alt
					global_config.split_percent += 1;
					update_split_size();
					redraw_all();
				} else if (this_buf[*timeout-1] == '4') { // alt shift
					use_right_buffer();
					redraw_all();
				} else {
					cursor_right();
				}
				break;
			case 'D': // left
				if (this_buf[*timeout-1] == '5') { // ctrl
					big_word_left();
				} else if (this_buf[*timeout-1] == '2') { // shift
					word_left();
				} else if (this_buf[*timeout-1] == '3') { // alt
					global_config.split_percent -= 1;
					update_split_size();
					redraw_all();
				} else if (this_buf[*timeout-1] == '4') { // alt-shift
					use_left_buffer();
					redraw_all();
				} else {
					cursor_left();
				}
				break;
			case 'H': // home
				cursor_home();
				break;
			case 'F': // end
				cursor_end();
				break;
			case 'I':
				goto_line(env->line_no - (global_config.term_height - 6));
				break;
			case 'G':
				goto_line(env->line_no + global_config.term_height - 6);
				break;
			case 'Z':
				/* Shift tab */
				if (env->mode == MODE_LINE_SELECTION) {
					*timeout = 0;
					return 'Z';
				}
				break;
			case '~':
				switch (this_buf[*timeout-1]) {
					case '1':
						cursor_home();
						break;
					case '3':
						if (env->mode == MODE_INSERT || env->mode == MODE_REPLACE) {
							if (env->col_no < env->lines[env->line_no - 1]->actual + 1) {
								line_delete(env->lines[env->line_no - 1], env->col_no, env->line_no - 1);
								redraw_line(env->line_no-1);
								set_modified();
								redraw_statusbar();
								place_cursor_actual();
							} else if (env->line_no < env->line_count) {
								merge_lines(env->lines, env->line_no);
								redraw_text();
								set_modified();
								redraw_statusbar();
								place_cursor_actual();
							}
						}
						break;
					case '4':
						cursor_end();
						break;
					case '5':
						goto_line(env->line_no - (global_config.term_height - 6));
						break;
					case '6':
						goto_line(env->line_no + global_config.term_height - 6);
						break;
				}
				break;
			default:
				render_error("Unrecognized escape sequence identifier: %c", c);
				break;
		}
		*timeout = 0;
		return 0;
	}

	*timeout = 0;
	return 0;
}

/**
 * Search for the word under the cursor
 */
void search_under_cursor(void) {
	/* Figure out size */
	int c_before = 0;
	int c_after = 0;
	int i = env->col_no;
	while (i > 0) {
		if (!c_keyword_qualifier(env->lines[env->line_no-1]->text[i-1].codepoint)) break;
		c_before++;
		i--;
	}
	i = env->col_no+1;
	while (i < env->lines[env->line_no-1]->actual+1) {
		if (!c_keyword_qualifier(env->lines[env->line_no-1]->text[i-1].codepoint)) break;
		c_after++;
		i++;
	}
	if (!c_before && !c_after) return;

	/* Populate with characters */
	if (global_config.search) free(global_config.search);
	global_config.search = malloc(sizeof(uint32_t) * (c_before+c_after+1));
	int j = 0;
	while (c_before) {
		global_config.search[j] = env->lines[env->line_no-1]->text[env->col_no-c_before].codepoint;
		c_before--;
		j++;
	}
	int x = 0;
	while (c_after) {
		global_config.search[j] = env->lines[env->line_no-1]->text[env->col_no+x].codepoint;
		j++;
		x++;
		c_after--;
	}
	global_config.search[j] = 0;

	/* Find it */
	search_next();
}

/**
 * Handler for f,F,t,T
 * Find the selected character based on the search requirement:
 * f: forward, stop on character
 * F: backward, stop on character
 * t: forward, stop before character
 * T: backward, stop after character
 */
void find_character(int type, int c) {
	if (type == 'f' || type == 't') {
		for (int i = env->col_no+1; i <= env->lines[env->line_no-1]->actual; ++i) {
			if (env->lines[env->line_no-1]->text[i-1].codepoint == c) {
				env->col_no = i - !!(type == 't');
				place_cursor_actual();
				set_preferred_column();
				return;
			}
		}
	} else if (type == 'F' || type == 'T') {
		for (int i = env->col_no-1; i >= 1; --i) {
			if (env->lines[env->line_no-1]->text[i-1].codepoint == c) {
				env->col_no = i + !!(type == 'T');
				place_cursor_actual();
				set_preferred_column();
				return;
			}
		}
	}
}

/**
 * Clear the navigation number buffer
 */
void reset_nav_buffer(int c) {
	if (nav_buffer && (c < '0' || c > '9')) {
		nav_buffer = 0;
		redraw_commandline();
	}
}

/**
 * Performs action with repitions if nav_buffer is set;
 * otherwise once. With reps, set loading so that actions
 * don't redraw screen several times.
 */
#define with_reps(stuff) \
	if (reps) { \
		env->loading = 1; \
		for (int i = 0; i < reps; ++i) { \
			stuff; \
		} \
		env->loading = 0; \
		redraw_all(); \
	} else { \
		stuff; \
	}

/**
 * Standard navigation shared by normal, line, and char selection.
 */
void handle_navigation(int c) {
	int reps = (nav_buffer) ? atoi(nav_buf) : 1;
	switch (c) {
		case 2: /* ctrl-b = page up */
			with_reps(goto_line(env->line_no - (global_config.term_height - 6)));
			break;
		case 6: /* ctrl-f = page down */
			with_reps(goto_line(env->line_no + global_config.term_height - 6));
			break;
		case ':': /* Switch to command mode */
			command_mode();
			break;
		case '/': /* Switch to search mode */
			search_mode(1);
			break;
		case '?': /* Switch to search mode */
			search_mode(0);
			break;
		case 'n': /* Jump to next search result */
			with_reps(search_next());
			break;
		case 'N': /* Jump backwards to previous search result */
			with_reps(search_prev());
			break;
		case 'j': /* Move cursor down */
			with_reps(cursor_down());
			break;
		case 'k': /* Move cursor up */
			with_reps(cursor_up());
			break;
		case 'h': /* Move cursor left */
			with_reps(cursor_left());
			break;
		case 'l': /* Move cursor right*/
			with_reps(cursor_right());
			break;
		case 'b': /* Move cursor one word left */
			with_reps(word_left());
			break;
		case 'w': /* Move cursor one word right */
			with_reps(word_right());
			break;
		case 'B': /* Move cursor one WORD left */
			with_reps(big_word_left());
			break;
		case 'W': /* Move cursor one WORD right */
			with_reps(big_word_right());
			break;
		case 'f': /* Find character forward */
		case 'F': /* ... backward */
		case 't': /* ... forward but stop before */
		case 'T': /* ... backwards but stop before */
			{
				char tmp[2] = {c,'\0'};
				int cin = read_one_character(tmp);
				if (cin != -1) {
					with_reps(find_character(c, cin));
				}
				redraw_commandline();
			}
			break;
		case 'G': /* Go to line or end of file */
			if (nav_buffer) {
				goto_line(atoi(nav_buf));
				reset_nav_buffer(0);
			} else {
				goto_line(env->line_count);
			}
			break;
		case '*': /* Search for word under cursor */
			search_under_cursor();
			break;
		case ' ': /* Jump forward several lines */
			with_reps(goto_line(env->line_no + global_config.term_height - 6));
			break;
		case '%': /* Jump to matching brace/bracket */
			if (env->mode == MODE_LINE_SELECTION || env->mode == MODE_CHAR_SELECTION) {
				/* These modes need to recalculate syntax as find_matching_brace uses it to find appropriate match */
				for (int i = 0; i < env->line_count; ++i) {
					recalculate_syntax(env->lines[i],i);
				}
			}
			{
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
			break;
		case '{': /* Jump to previous blank line */
			env->col_no = 1;
			if (env->line_no == 1) break;
			do {
				env->line_no--;
				if (env->lines[env->line_no-1]->actual == 0) break;
			} while (env->line_no > 1);
			set_preferred_column();
			redraw_statusbar();
			break;
		case '}': /* Jump to next blank line */
			env->col_no = 1;
			if (env->line_no == env->line_count) break;
			do {
				env->line_no++;
				if (env->lines[env->line_no-1]->actual == 0) break;
			} while (env->line_no < env->line_count);
			set_preferred_column();
			redraw_statusbar();
			break;
		case '$': /* Move cursor to end of line */
			cursor_end();
			break;
		case '|':
		case '0': /* Move cursor to beginning of line */
			if (c == '0' && nav_buffer) break;
			cursor_home();
			break;
		case '\r': /* first non-whitespace of next line */
			if (env->line_no < env->line_count) {
				env->line_no++;
				env->col_no = 1;
			} else {
				break;
			}
			/* fall through */
		case '^': /* first non-whitespace */
			for (int i = 0; i < env->lines[env->line_no-1]->actual; ++i) {
				if (!is_whitespace(env->lines[env->line_no-1]->text[i].codepoint)) {
					env->col_no = i + 1;
					break;
				}
			}
			set_preferred_column();
			redraw_statusbar();
			break;
	}

	/* Otherwise, numbers go into the number buffer */
	if ((c >= '1' && c <= '9') || (c == '0' && nav_buffer)) {
		if (nav_buffer < NAV_BUFFER_MAX) {
			/* Up to NAV_BUFFER_MAX=10 characters; that should be enough for most tasks */
			nav_buf[nav_buffer] = c;
			nav_buf[nav_buffer+1] = 0;
			nav_buffer++;
			/* Print the number buffer */
			redraw_commandline();
		}
	}
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
			recalculate_syntax(env->lines[(line)-1],(line)-1); \
		} else { \
			for (int j = 0; j < env->lines[(line)-1]->actual; ++j) { \
				env->lines[(line)-1]->text[j].flags |= FLAG_SELECT; \
			} \
		} \
		redraw_line((line)-1); \
	} while (0)

/**
 * Adjust indentation on selected lines.
 */
void adjust_indent(int start_line, int direction) {
	int lines_to_cover = 0;
	int start_point = 0;
	if (start_line <= env->line_no) {
		start_point = start_line - 1;
		lines_to_cover = env->line_no - start_line + 1;
	} else {
		start_point = env->line_no - 1;
		lines_to_cover = start_line - env->line_no + 1;
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
				for (int j = 0; j < env->tabstop; ++j) {
					if (env->lines[start_point + i]->text[0].codepoint == ' ') {
						line_delete(env->lines[start_point + i],1,start_point+i);
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

/**
 * LINE SELECTION mode
 *
 * Equivalent to visual line in vim; selects lines of texts.
 */
void line_selection_mode(void) {
	env->start_line = env->line_no;
	int prev_line  = env->start_line;

	env->mode = MODE_LINE_SELECTION;
	redraw_commandline();

	int c;
	int timeout = 0;
	int this_buf[20];

	for (int j = 0; j < env->lines[env->line_no-1]->actual; ++j) {
		env->lines[env->line_no-1]->text[j].flags |= FLAG_SELECT;
	}
	redraw_line(env->line_no-1);

	while ((c = bim_getch())) {
		if (c == -1) {
			if (timeout && this_buf[timeout-1] == '\033') {
				goto _leave_select_line;
			}
			timeout = 0;
			continue;
		} else {
			if (timeout == 0) {
				switch (c) {
					case '\033':
						if (timeout == 0) {
							this_buf[timeout] = c;
							timeout++;
						}
						break;
					case DELETE_KEY:
					case BACKSPACE_KEY:
						cursor_left();
						break;
					case '\t':
						if (env->readonly) goto _readonly;
						adjust_indent(env->start_line, 1);
						break;
					case 'V':
						goto _leave_select_line;
					case 'y':
						yank_lines(env->start_line, env->line_no);
						goto _leave_select_line;
					case 'D':
					case 'd':
					case 'x':
					case 's':
						if (env->readonly) goto _readonly;
						yank_lines(env->start_line, env->line_no);
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
						if (c == 's') {
							env->lines = add_line(env->lines, env->line_no-1);
							redraw_text();
							env->mode = MODE_INSERT;
							return;
						}
						goto _leave_select_line;
					case 'r':
						{
							int c = read_one_character("LINE SELECTION r");
							if (c == -1) break;
							char_t _c = {codepoint_width(c), 0, c};
							int start_point = env->start_line < env->line_no ? env->start_line : env->line_no;
							int end_point = env->start_line < env->line_no ? env->line_no : env->start_line;
							for (int line = start_point; line <= end_point; ++line) {
								for (int i = 0; i < env->lines[line-1]->actual; ++i) {
									line_replace(env->lines[line-1], _c, i, line-1);
								}
							}
						}
						goto _leave_select_line;
					case ':': /* Handle command mode specially for redraw */
						global_config.break_from_selection = 0;
						command_mode();
						if (global_config.break_from_selection) break;
						goto _leave_select_line;
					default:
						handle_navigation(c);
						break;
				}
			} else {
				switch (handle_escape(this_buf,&timeout,c)) {
					case 1:
						bim_unget(c);
						goto _leave_select_line;
					case 'Z':
						/* Unindent */
						if (env->readonly) goto _readonly;
						adjust_indent(env->start_line, -1);
						break;
				}
			}

			reset_nav_buffer(c);

			/* Mark current line */
			_redraw_line(env->line_no,0);
			_redraw_line(env->start_line,1);

			/* Properly mark everything in the span we just moved through */
			if (prev_line < env->line_no) {
				for (int i = prev_line; i < env->line_no; ++i) {
					_redraw_line(i,0);
				}
				prev_line = env->line_no;
			} else if (prev_line > env->line_no) {
				for (int i = env->line_no + 1; i <= prev_line; ++i) {
					_redraw_line(i,0);
				}
				prev_line = env->line_no;
			}
			redraw_commandline();
			place_cursor_actual();
			continue;
_readonly:
			render_error("Buffer is read-only");
		}
	}

_leave_select_line:
	set_history_break();
	env->mode = MODE_NORMAL;
	for (int i = 0; i < env->line_count; ++i) {
		recalculate_syntax(env->lines[i],i);
	}
	redraw_all();
}

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

/**
 * COL INSERT MODE
 *
 * Allows entering text on multiple lines simultaneously.
 * A full multi-cursor insert mode would be way cooler, but
 * this is all we need for my general use case of vim's BLOCK modes.
 */
void col_insert_mode(void) {
	if (env->start_line < env->line_no) {
		/* swap */
		int tmp = env->line_no;
		env->line_no = env->start_line;
		env->start_line = tmp;
	}

	/* Set column to preferred_column */
	env->mode = MODE_COL_INSERT;
	redraw_commandline();
	place_cursor_actual();

	int cin;
	uint32_t c;

	int timeout = 0;
	int this_buf[20];
	uint32_t istate = 0;
	int redraw = 0;
	while ((cin = bim_getch_timeout((redraw ? 10 : 200)))) {
		if (cin == -1) {
			if (redraw) {
				if (redraw & 2) {
					redraw_text();
				} else {
					redraw_line(env->line_no-1);
				}
				redraw_statusbar();
				place_cursor_actual();
				redraw = 0;
			}
			if (timeout && this_buf[timeout-1] == '\033') {
				return;
			}
			timeout = 0;
			continue;
		}
		if (!decode(&istate, &c, cin)) {
			if (timeout == 0) {
				switch (c) {
					case '\033':
						if (timeout == 0) {
							this_buf[timeout] = c;
							timeout++;
						}
						break;
					case 3: /* ^C */
						return;
					case DELETE_KEY:
					case BACKSPACE_KEY:
						delete_at_column(-1);
						break;
					case ENTER_KEY:
					case LINE_FEED:
						/* do nothing in these cases */
						break;
					case 23: /* ^W */
						break;
					case 22: /* ^V */
						render_commandline_message("^V");
						while ((cin = bim_getch()) == -1);
						c = cin;
						redraw_commandline();
						/* fallthrough */
					default:
						/* Okay, this is going to duplicate a lot of insert_char */
						if (c) {
							char_t _c;
							_c.codepoint = c;
							_c.flags = 0;
							_c.display_width = codepoint_width(c);

							int inserted_width = 0;

							/* For each line */
							for (int i = env->line_no; i <= env->start_line; i++) {
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
							}

							env->sel_col += inserted_width;
							env->col_no++;
							redraw_text();

						}
				}
			} else {
				/* Ignore escape sequences for now, but handle them nicely */
				int arg = 0;
				switch (handle_command_escape(this_buf,&timeout,c,&arg)) {
					case 1:
						bim_unget(c);
						return;
				}
			}
		} else if (istate == UTF8_REJECT) {
			istate = 0;
		}
	}
}

/**
 * COL SELECTION mode
 *
 * Limited selection mode for doing inserts on multiple lines.
 * Experimental. Based on how I usually use vim's VISUAL BLOCK mode.
 */
void col_selection_mode(void) {
	env->start_line = env->line_no;
	env->sel_col = env->preferred_column;
	int prev_line = env->start_line;

	env->mode = MODE_COL_SELECTION;
	redraw_commandline();

	int c;
	int timeout = 0;
	int this_buf[20];

	while ((c = bim_getch())) {
		if (c == -1) {
			if (timeout && this_buf[timeout-1] == '\033') {
				goto _leave_select_col;
			}
			timeout = 0;
			continue;
		} else {
			if (timeout == 0) {
				switch (c) {
					case '\033':
						if (timeout == 0) {
							this_buf[timeout] = c;
							timeout++;
						}
						break;
					case 'I':
						if (env->readonly) goto _readonly;
						col_insert_mode();
						goto _leave_select_col;
					case 'a':
						if (env->readonly) goto _readonly;
						env->sel_col += 1;
						redraw_text();
						col_insert_mode();
						goto _leave_select_col;
					case 'd':
						if (env->start_line < env->line_no) {
							int tmp = env->line_no;
							env->line_no = env->start_line;
							env->start_line = tmp;
						}
						delete_at_column(1);
						goto _leave_select_col;
					case ':':
						global_config.break_from_selection = 0;
						command_mode();
						if (global_config.break_from_selection) break;
						goto _leave_select_col;
					default:
						handle_navigation(c);
						break;
				}
			} else {
				switch (handle_escape(this_buf,&timeout,c)) {
					case 1:
						bim_unget(c);
						goto _leave_select_col;
					/* Doesn't support anything else. */
				}
			}
		}

		reset_nav_buffer(c);

		_redraw_line_col(env->line_no, 0);
		/* Properly mark everything in the span we just moved through */
		if (prev_line < env->line_no) {
			for (int i = prev_line; i < env->line_no; ++i) {
				_redraw_line_col(i,0);
			}
			prev_line = env->line_no;
		} else if (prev_line > env->line_no) {
			for (int i = env->line_no + 1; i <= prev_line; ++i) {
				_redraw_line_col(i,0);
			}
			prev_line = env->line_no;
		}

		/* prev_line... */
		redraw_commandline();
		place_cursor_actual();
		continue;
_readonly:
		render_error("Buffer is read-only.");
	}

_leave_select_col:
	set_history_break();
	env->mode = MODE_NORMAL;
	redraw_all();
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

#define _redraw_line_char(line, force_start_line) \
	do { \
		if (!(force_start_line) && (line) == start_line) break; \
		if ((line) > env->line_count + 1) { \
			if ((line) - env->offset - 1 < global_config.term_height - global_config.bottom_size - 1) { \
				draw_excess_line((line) - env->offset - 1); \
			} \
			break; \
		} \
		if ((env->line_no < start_line  && ((line) < env->line_no || (line) > start_line)) || \
			(env->line_no > start_line  && ((line) > env->line_no || (line) < start_line)) || \
			(env->line_no == start_line && (line) != start_line)) { \
			/* Line is completely outside selection */ \
			recalculate_syntax(env->lines[(line)-1],(line)-1); \
		} else { \
			if ((line) == start_line || (line) == env->line_no) { \
				recalculate_syntax(env->lines[(line)-1],(line)-1); \
			} \
			for (int j = 0; j < env->lines[(line)-1]->actual; ++j) { \
				if (point_in_range(start_line, env->line_no,start_col, env->col_no, (line), j+1)) { \
					env->lines[(line)-1]->text[j].flags |= FLAG_SELECT; \
				} \
			} \
		} \
		redraw_line((line)-1); \
	} while (0)

/**
 * CHAR SELECTION mode.
 */
void char_selection_mode(void) {
	int start_line = env->line_no;
	int start_col  = env->col_no;
	int prev_line  = start_line;

	env->mode = MODE_CHAR_SELECTION;
	redraw_commandline();

	int c;
	int timeout = 0;
	int this_buf[20];

	/* Select single character */
	env->lines[env->line_no-1]->text[env->col_no-1].flags |= FLAG_SELECT;
	redraw_line(env->line_no-1);

	while ((c = bim_getch())) {
		if (c == -1) {
			if (timeout && this_buf[timeout-1] == '\033') {
				goto _leave_select_char;
			}
			timeout = 0;
			continue;
		} else {
			if (timeout == 0) {
				switch (c) {
					case '\033':
						if (timeout == 0) {
							this_buf[timeout] = c;
							timeout++;
						}
						break;
					case DELETE_KEY:
					case BACKSPACE_KEY:
						cursor_left();
						break;
					case 'v':
						goto _leave_select_char;
					case 'y':
						{
							int end_line = env->line_no;
							int end_col  = env->col_no;
							if (start_line == end_line) {
								if (start_col > end_col) {
									int tmp = start_col;
									start_col = end_col;
									end_col = tmp;
								}
							} else if (start_line > end_line) {
								int tmp = start_line;
								start_line = end_line;
								end_line = tmp;
								tmp = start_col;
								start_col = end_col;
								end_col = tmp;
							}
							yank_text(start_line, start_col, end_line, end_col);
						}
						goto _leave_select_char;
					case 'D':
					case 'd':
					case 'x':
					case 's':
						if (env->readonly) goto _readonly;
						{
							int end_line = env->line_no;
							int end_col  = env->col_no;
							if (start_line == end_line) {
								if (start_col > end_col) {
									int tmp = start_col;
									start_col = end_col;
									end_col = tmp;
								}
								yank_text(start_line, start_col, end_line, end_col);
								for (int i = start_col; i <= end_col; ++i) {
									line_delete(env->lines[start_line-1], start_col, start_line - 1);
								}
								env->col_no = start_col;
							} else {
								if (start_line > end_line) {
									int tmp = start_line;
									start_line = end_line;
									end_line = tmp;
									tmp = start_col;
									start_col = end_col;
									end_col = tmp;
								}
								/* Copy lines */
								yank_text(start_line, start_col, end_line, end_col);
								/* Delete lines */
								for (int i = start_line+1; i < end_line; ++i) {
									env->lines = remove_line(env->lines, start_line);
								} /* end_line is no longer valid; should be start_line+1*/
								/* Delete from start_col forward */
								int tmp = env->lines[start_line-1]->actual;
								for (int i = start_col; i <= tmp; ++i) {
									line_delete(env->lines[start_line-1], start_col, start_line - 1);
								}
								for (int i = 1; i <= end_col; ++i) {
									line_delete(env->lines[start_line], 1, start_line);
								}
								/* Merge start and end lines */
								merge_lines(env->lines, start_line);
								env->line_no = start_line;
								env->col_no = start_col;
							}
						}
						if (env->line_no > env->line_count) {
							env->line_no = env->line_count;
						}
						set_preferred_column();
						set_modified();
						if (c == 's') {
							redraw_text();
							env->mode = MODE_INSERT;
							return; /* When returning from char selection mode, normal mode will check for MODE_INSERT */
						}
						goto _leave_select_char;
					case 'r':
						{
							int c = read_one_character("CHAR SELECTION r");
							if (c == -1) break;
							char_t _c = {codepoint_width(c), 0, c};
							/* This should probably be a function line "do_over_range" or something */
							if (start_line == env->line_no) {
								int s = (start_col < env->col_no) ? start_col : env->col_no;
								int e = (start_col < env->col_no) ? env->col_no : start_col;
								for (int i = s; i <= e; ++i) {
									line_replace(env->lines[start_line-1], _c, i-1, start_line-1);
								}
								redraw_text();
							} else {
								if (start_line < env->line_no) {
									for (int s = start_col-1; s < env->lines[start_line-1]->actual; ++s) {
										line_replace(env->lines[start_line-1], _c, s, start_line-1);
									}
									for (int line = start_line + 1; line < env->line_no; ++line) {
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
									for (int line = env->line_no + 1; line < start_line; ++line) {
										for (int i = 0; i < env->lines[line-1]->actual; ++i) {
											line_replace(env->lines[line-1], _c, i, line-1);
										}
									}
									for (int s = 0; s < start_col; ++s) {
										line_replace(env->lines[start_line-1], _c, s, start_line-1);
									}
								}
							}
						}
						goto _leave_select_char;
					case 'A':
						for (int i = 0; i < env->line_count; ++i) {
							recalculate_syntax(env->lines[i],i);
						}
						redraw_text();
						env->col_no = env->col_no > start_col ? env->col_no + 1 : start_col + 1;
						env->mode = MODE_INSERT;
						return;
					case ':':
						global_config.break_from_selection = 0;
						command_mode();
						if (global_config.break_from_selection) break;
						goto _leave_select_char;
					default:
						handle_navigation(c);
						break;
				}
			} else {
				switch (handle_escape(this_buf,&timeout,c)) {
					case 1:
						bim_unget(c);
						goto _leave_select_char;
				}
			}

			reset_nav_buffer(c);

			/* Mark current line */
			_redraw_line_char(env->line_no,1);

			/* Properly mark everything in the span we just moved through */
			if (prev_line < env->line_no) {
				for (int i = prev_line; i < env->line_no; ++i) {
					_redraw_line_char(i,1);
				}
				prev_line = env->line_no;
			} else if (prev_line > env->line_no) {
				for (int i = env->line_no + 1; i <= prev_line; ++i) {
					_redraw_line_char(i,1);
				}
				prev_line = env->line_no;
			}
			place_cursor_actual();
			continue;
_readonly:
			render_error("Buffer is read-only");
		}
	}

_leave_select_char:
	set_history_break();
	env->mode = MODE_NORMAL;
	for (int i = 0; i < env->line_count; ++i) {
		recalculate_syntax(env->lines[i],i);
	}
	redraw_all();
}

struct completion_match {
	char * string;
	char * file;
};

void free_completion_match(struct completion_match * match) {
	if (match->string) free(match->string);
	if (match->file) free(match->file);
}

/**
 * Read ctags file to find matches for a symbol
 */
int read_tags(uint32_t * comp, struct completion_match **matches, int * matches_count) {
	int matches_len = 4;
	*matches_count = 0;
	*matches = malloc(sizeof(struct completion_match) * (matches_len));

	FILE * tags = fopen("tags","r");
	if (!tags) return 1;
	char tmp[4096]; /* max line */
	while (!feof(tags) && fgets(tmp, 4096, tags)) {
		if (tmp[0] == '!') continue;
		int i = 0;
		while (comp[i] && comp[i] == (unsigned int)tmp[i]) i++;
		if (comp[i] == '\0') {
			int j = i;
			while (tmp[j] != '\t' && tmp[j] != '\n' && tmp[j] != '\0') j++;
			tmp[j] = '\0'; j++;
			char * file = &tmp[j];
			while (tmp[j] != '\t' && tmp[j] != '\n' && tmp[j] != '\0') j++;
			tmp[j] = '\0'; j++;

			/* Dedup */
			#if 0
			int match_found = 0;
			for (int i = 0; i < *matches_count; ++i) {
				if (!strcmp((*matches)[i].string, tmp)) {
					match_found = 1;
					break;
				}
			}
			if (match_found) continue;
			#endif

			if (*matches_count == matches_len) {
				matches_len *= 2;
				*matches = realloc(*matches, sizeof(struct completion_match) * (matches_len));
			}
			(*matches)[*matches_count].string = strdup(tmp);
			(*matches)[*matches_count].file = strdup(file);
			(*matches_count)++;
		}
	}
	fclose(tags);
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
	int cursor_y = env->line_no - env->offset + 1;
	int max_y = global_config.term_height - 2 - cursor_y;

	/* Find a good place to put the box horizontally */
	int num_size = num_width() + 3;
	int x = num_size + 1 - env->coffset;

	/* Determine where the cursor is physically */
	for (int i = 0; i < env->col_no - 1 - original_length; ++i) {
		char_t * c = &env->lines[env->line_no-1]->text[i];
		x += c->display_width;
	}

	int box_width = max_width;
	int box_x = x;
	int box_y = cursor_y+1;
	if (max_width > env->width - num_width()) {
		box_width = env->width - num_width();
		box_x = 1;
	} else if (env->width - x < max_width) {
		box_width = max_width;
		box_x = env->width - max_width;
	}

	int max_count = (max_y < matches_count) ? max_y - 1 : matches_count;

	for (int x = index; x < max_count+index; ++x) {
		int i = x % matches_count;
		place_cursor(box_x, box_y+x-index);
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
		place_cursor(box_x, box_y);
		set_colors(COLOR_STATUS_FG, COLOR_STATUS_BG);
		printf(" (no matches) ");
	} else if (max_count != matches_count) {
		place_cursor(box_x, box_y+max_count);
		set_colors(COLOR_STATUS_FG, COLOR_STATUS_BG);
		printf(" (%d more) ", matches_count-max_count);
	}
}

/**
 * Autocomplete words (function/variable names, etc.) in input mode.
 */
int omni_complete(void) {
	int c;

	/* Pull the word from before the cursor */
	int c_before = 0;
	int i = env->col_no-1;
	while (i > 0) {
		if (!c_keyword_qualifier(env->lines[env->line_no-1]->text[i-1].codepoint)) break;
		c_before++;
		i--;
	}

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
	if (read_tags(tmp, &matches, &matches_count)) goto _completion_done;

	/* Draw box with matches at cursor-width(tmp) */
	draw_completion_matches(tmp, matches, matches_count, 0);

	int retval = 0;
	int index = 0;

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
 * INSERT mode
 *
 * Accept input into the text buffer.
 */
void insert_mode(void) {
	int cin;
	uint32_t c;

	/* Set mode line */
	env->mode = MODE_INSERT;
	redraw_commandline();

	/* Place the cursor in the text area */
	place_cursor_actual();

	int timeout = 0;
	int this_buf[20];
	uint32_t istate = 0;
	int redraw = 0;
	while ((cin = bim_getch_timeout((redraw ? 10 : 200)))) {
		if (cin == -1) {
			if (redraw) {
				if (redraw & 2) {
					redraw_text();
				} else {
					redraw_line(env->line_no-1);
				}
				redraw_statusbar();
				place_cursor_actual();
				redraw = 0;
			}
			if (timeout && this_buf[timeout-1] == '\033') {
				leave_insert();
				return;
			}
			timeout = 0;
			continue;
		}
		if (!decode(&istate, &c, cin)) {
			if (timeout == 0) {
				switch (c) {
					case '\033':
						if (timeout == 0) {
							this_buf[timeout] = c;
							timeout++;
						}
						break;
					case 3: /* ^C */
						leave_insert();
						return;
					case DELETE_KEY:
					case BACKSPACE_KEY:
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
								redraw |= 2;
								break; /* out of case */
							}
						}
						delete_at_cursor();
						break;
					case ENTER_KEY:
					case LINE_FEED:
						if (env->indent) {
							if ((env->lines[env->line_no-1]->text[env->col_no-2].flags & 0x1F) == FLAG_COMMENT &&
								(env->lines[env->line_no-1]->text[env->col_no-2].codepoint == ' ') &&
								(env->col_no > 3) &&
								(env->lines[env->line_no-1]->text[env->col_no-3].codepoint == '*')) {
								delete_at_cursor();
							}
						}
						insert_line_feed();
						redraw |= 2;
						break;
					case 15: /* ^O */
						while (omni_complete() == 1);
						break;
					case 22: /* ^V */
						/* Insert next byte raw */
						{
							/* Indicate we're in literal mode */
							render_commandline_message("^V");
							/* Put the cursor back into the text field */
							place_cursor_actual();
							/* Get next character */
							while ((cin = bim_getch()) == -1);
							/* Insert literal */
							insert_char(cin);
							/* Redraw INSERT */
							redraw_commandline();
							/* Draw text */
							redraw |= 1;
						}
						break;
					case 23: /* ^W */
						delete_word();
						set_preferred_column();
						break;
					case '\t':
						if (env->tabs) {
							insert_char('\t');
						} else {
							for (int i = 0; i < env->tabstop; ++i) {
								insert_char(' ');
							}
						}
						redraw |= 1;
						set_preferred_column();
						break;
					case '/':
						if (env->indent) {
							if ((env->lines[env->line_no-1]->text[env->col_no-2].flags & 0x1F) == FLAG_COMMENT &&
								(env->lines[env->line_no-1]->text[env->col_no-2].codepoint == ' ') &&
								(env->col_no > 3) &&
								(env->lines[env->line_no-1]->text[env->col_no-3].codepoint == '*')) {
								env->col_no--;
								replace_char('/');
								env->col_no++;
								place_cursor_actual();
								break;
							}
						}
						goto _just_insert;
					case '}':
						if (env->indent) {
							int was_whitespace = 1;
							for (int i = 0; i < env->lines[env->line_no-1]->actual; ++i) {
								if (env->lines[env->line_no-1]->text[i].codepoint != ' ' &&
									env->lines[env->line_no-1]->text[i].codepoint != '\t') {
									was_whitespace = 0;
									break;
								}
							}
							insert_char('}');
							if (was_whitespace) {
								int line = -1, col = -1;
								env->col_no--;
								find_matching_paren(&line,&col, 1);
								if (line != -1) {
									while (env->lines[env->line_no-1]->actual) {
										line_delete(env->lines[env->line_no-1], env->lines[env->line_no-1]->actual, env->line_no-1);
									}
									add_indent(env->line_no-1,line-1,1);
									env->col_no = env->lines[env->line_no-1]->actual + 1;
									insert_char('}');
								}
							}
							set_preferred_column();
							redraw |= 1;
							break;
						}
						/* fallthrough */
					default:
_just_insert:
						insert_char(c);
						set_preferred_column();
						redraw |= 1;
						break;
				}
			} else {
				if (handle_escape(this_buf,&timeout,c)) {
					bim_unget(c);
					leave_insert();
					return;
				}
			}
		} else if (istate == UTF8_REJECT) {
			istate = 0;
		}
	}
}

/**
 * REPLACE mode
 *
 * Like insert, but replaces characters.
 */
void replace_mode(void) {
	int cin;
	uint32_t c;

	/* Set mode line */
	env->mode = MODE_REPLACE;
	redraw_commandline();

	/* Place the cursor in the text area */
	place_cursor_actual();

	int timeout = 0;
	int this_buf[20];
	uint32_t istate = 0;
	while ((cin = bim_getch())) {
		if (cin == -1) {
			if (timeout && this_buf[timeout-1] == '\033') {
				leave_insert();
				return;
			}
			timeout = 0;
			continue;
		}
		if (!decode(&istate, &c, cin)) {
			if (timeout == 0) {
				switch (c) {
					case '\033':
						if (timeout == 0) {
							this_buf[timeout] = c;
							timeout++;
						}
						break;
					case DELETE_KEY:
					case BACKSPACE_KEY:
						if (env->line_no > 1 && env->col_no == 1) {
							env->line_no--;
							env->col_no = env->lines[env->line_no-1]->actual;
							set_preferred_column();
							place_cursor_actual();
						} else {
							cursor_left();
						}
						break;
					case ENTER_KEY:
					case LINE_FEED:
						insert_line_feed();
						redraw_text();
						set_modified();
						redraw_statusbar();
						place_cursor_actual();
						break;
					default:
						if (env->col_no <= env->lines[env->line_no - 1]->actual) {
							replace_char(c);
							env->col_no += 1;
						} else {
							insert_char(c);
							redraw_line(env->line_no-1);
						}
						set_preferred_column();
						redraw_statusbar();
						place_cursor_actual();
						break;
				}
			} else {
				if (handle_escape(this_buf,&timeout,c)) {
					bim_unget(c);
					leave_insert();
					return;
				}
			}
		} else if (istate == UTF8_REJECT) {
			istate = 0;
		}
	}
}

/**
 * Handler for 'r'; takes in input to replace a single
 * character in the document. Handles Unicode, so we
 * can replace a character with complex input. Also
 * handles ^V so we can replace with escape sequences
 * we would otherwise gobble up.
 *
 * Does not actually do the replacement; returns -1
 * or the codepoint to replace with.
 */
int read_one_character(char * message) {
	/* Read one character and replace */
	render_commandline_message(message);
	uint32_t state = 0;
	int cin;
	uint32_t c = -1;
	while ((cin = bim_getch())) {
		if (cin == -1) continue;
		if (!decode(&state, &c, cin)) {
			if (c == '\033') {
				c = -1;
				goto _done;
			} else if (c == 22) { /* ctrl-v */
				render_commandline_message(message);
				printf(" ^V");
				fflush(stdout);
				while ((cin = bim_getch()) == -1);
				c = cin;
				goto _done;
			} else {
				goto _done;
			}
		}
	}

_done:
	redraw_commandline();
	return c;
}

/**
 * NORMAL mode
 *
 * Default editor mode - just cursor navigation and keybinds
 * to enter the other modes.
 */
void normal_mode(void) {

	while (1) {
		place_cursor_actual();
		int c;
		int timeout = 0;
		int this_buf[20];
		while ((c = bim_getch())) {
			if (c == -1) {
				/* getch timed out, nothing to do in normal mode */
				continue;
			}
			if (timeout == 0) {
				switch (c) {
					case '\033':
						if (timeout == 0) {
							this_buf[timeout] = c;
							timeout++;
						}
						break;
					case DELETE_KEY:
					case BACKSPACE_KEY:
						if (env->line_no > 1 && env->col_no == 1) {
							env->line_no--;
							env->col_no = env->lines[env->line_no-1]->actual;
							set_preferred_column();
							place_cursor_actual();
						} else {
							cursor_left();
						}
						break;
					case 'V': /* Enter LINE SELECTION mode */
						line_selection_mode();
						if (env->mode == MODE_INSERT) goto _insert;
						break;
					case 'v': /* Enter CHAR SELECTION mode */
						char_selection_mode();
						if (env->mode == MODE_INSERT) goto _insert;
						break;
					case 22: /* ctrl-v, enter COL SELECTION mode */
						set_preferred_column();
						col_selection_mode();
						break;
					case 'O': /* Append line before and enter INSERT mode */
						{
							if (env->readonly) goto _readonly;
							env->lines = add_line(env->lines, env->line_no-1);
							env->col_no = 1;
							add_indent(env->line_no-1,env->line_no,0);
							if (env->highlighting_paren && env->highlighting_paren > env->line_no) env->highlighting_paren++;
							redraw_text();
							set_preferred_column();
							set_modified();
							place_cursor_actual();
							goto _insert;
						}
					case 'o': /* Append line after and enter INSERT mode */
						{
							if (env->readonly) goto _readonly;
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
							goto _insert;
						}
					case 'a': /* Enter INSERT mode with cursor after current position */
						if (env->col_no < env->lines[env->line_no-1]->actual + 1) {
							env->col_no += 1;
						}
						goto _insert;
					case 's':
					case 'x':
						if (env->col_no <= env->lines[env->line_no-1]->actual) {
							line_delete(env->lines[env->line_no-1], env->col_no, env->line_no-1);
							redraw_text();
						}
						if (c == 's') goto _insert;
						set_history_break();
						break;
					case 'P': /* Paste before */
					case 'p': /* Paste after */
						if (env->readonly) goto _readonly;
						if (global_config.yanks) {
							if (!global_config.yank_is_full_lines) {
								/* Handle P for paste before, p for past after */
								int target_column = (c == 'P' ? (env->col_no) : (env->col_no+1));
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
									env->lines = add_line(env->lines, env->line_no - (c == 'P' ? 1 : 0));
								}
								for (unsigned int i = 0; i < global_config.yank_count; ++i) {
									replace_line(env->lines, env->line_no - (c == 'P' ? 1 : 0) + i, global_config.yanks[i]);
								}
							}
							/* Recalculate whole document syntax */
							for (int i = 0; i < env->line_count; ++i) {
								env->lines[i]->istate = 0;
							}
							for (int i = 0; i < env->line_count; ++i) {
								recalculate_syntax(env->lines[i],i);
							}
							if (c == 'p') {
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
						break;
					case 'r': /* Replace with next */
						{
							int c = read_one_character("r");
							if (c != -1) {
								replace_char(c);
							}
						}
						break;
					case 'A':
						env->col_no = env->lines[env->line_no-1]->actual+1;
						goto _insert;
					case 'u': /* Undo one block of history */
						undo_history();
						break;
					case 18: /* ^R - Redo one block of history */
						redo_history();
						break;
					case 12: /* ^L - Repaint the whole screen */
						redraw_all();
						break;
					case 'i': /* Enter INSERT mode */
_insert:
						if (env->readonly) goto _readonly;
						insert_mode();
						redraw_statusbar();
						redraw_commandline();
						timeout = 0;
						break;
					case 'R': /* Enter REPLACE mode */
						if (env->readonly) goto _readonly;
						replace_mode();
						redraw_statusbar();
						redraw_commandline();
						timeout = 0;
						break;
_readonly:
						render_error("Buffer is read-only");
						break;
					default:
						handle_navigation(c);
						break;
				}
			} else {
				handle_escape(this_buf,&timeout,c);
			}
			reset_nav_buffer(c);
			place_cursor_actual();
		}
	}

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
			" -O     " _s "set various options:" _e
			"        noscroll    " _s "disable terminal scrolling" _e
			"        noaltscreen " _s "disable alternate screen buffer" _e
			"        nomouse     " _s "disable mouse support" _e
			"        nounicode   " _s "disable unicode display" _e
			"        nobright    " _s "disable bright next" _e
			"        nohideshow  " _s "disable togglging cursor visibility" _e
			"        nosyntax    " _s "disable syntax highlighting on load" _e
			"        notitle     " _s "disable title-setting escapes" _e
			"        history     " _s "enable experimental undo/redo" _e
			" -c,-C  " _s "print file to stdout with syntax highlighting" _e
			"        " _s "-C includes line numbers, -c does not" _e
			" -u     " _s "override bimrc file" _e
			" -?     " _s "show this help text" _e
			" --version " _s "show version information and available plugins" _e
			"\n", argv[0], argv[0]);
#undef _e
#undef _s
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

	/* Try to open the file */
	FILE * bimrc = fopen(tmp, "r");
	free(tmp);

	if (!bimrc) {
		/* No bimrc, or bad permissions */
		return;
	}

	/* Parse through lines */
	char line[1024];
	while (!feof(bimrc)) {
		char * l = fgets(line, 1023, bimrc);

		/* Ignore bad lines */
		if (!l) break;
		if (!*l) continue;
		if (*l == '\n') continue;

		/* Ignore comment lines */
		if (*l == '#') continue;

		/* Remove linefeed at the end */
		char *nl = strstr(l,"\n");
		if (nl) *nl = '\0';

		/* Extract value from keypair, if available
		 * (I foresee options without values in the future) */
		char *value= strstr(l,"=");
		if (value) {
			*value = '\0';
			value++;
		}

		/* theme=... */
		if (!strcmp(l,"theme") && value) {
			/* Examine available themes for a match. */
			for (struct theme_def * d = themes; d->name; ++d) {
				if (!strcmp(value, d->name)) {
					d->load();
					break;
				}
			}
		}

		/* enable history (experimental) */
		if (!strcmp(l,"history")) {
			global_config.history_enabled = (value ? atoi(value) : 1);
		}

		/* padding= */
		if (!strcmp(l,"padding") && value) {
			global_config.cursor_padding = atoi(value);
		}

		if (!strcmp(l,"hlparen") && value) {
			global_config.highlight_parens = atoi(value);
		}

		/* Disable highlighting of current line */
		if (!strcmp(l,"hlcurrent") && value) {
			global_config.highlight_current_line = atoi(value);
		}

		/* Relative line numbers */
		if (!strcmp(l,"relativenumber") && value) {
			global_config.relative_lines = atoi(value);
		}

		if (!strcmp(l,"splitpercent") && value) {
			global_config.split_percent = atoi(value);
		}

		if (!strcmp(l,"shiftscrolling")) {
			global_config.shift_scrolling = (value ? atoi(value) : 1);
		}

		if (!strcmp(l,"scrollamount") && value) {
			global_config.scroll_amount = atoi(value);
		}

		if (!strcmp(l,"git") && value) {
			global_config.check_git = !!atoi(value);
		}

		if (!strcmp(l,"colorgutter") && value) {
			global_config.color_gutter = !!atoi(value);
		}

		if (!strcmp(l,"numbers") && value) {
			global_config.numbers = !!atoi(value);
		}
	}

	fclose(bimrc);
}

/**
 * Set some default values when certain terminals are detected.
 */
void detect_weird_terminals(void) {

	char * term = getenv("TERM");
	if (term && !strcmp(term,"linux")) {
		/* Linux VTs can't scroll. */
		global_config.can_scroll = 0;
	}
	if (term && !strcmp(term,"cons25")) {
		/* Dragonfly BSD console */
		global_config.can_hideshow = 0;
		global_config.can_altscreen = 0;
		global_config.can_mouse = 0;
		global_config.can_unicode = 0;
		global_config.can_bright = 0;
	}
	if (term && !strcmp(term,"sortix")) {
		/* sortix will spew title escapes to the screen, no good */
		global_config.can_title = 0;
	}
	if (term && strstr(term,"tmux") == term) {
		global_config.can_scroll = 0;
		global_config.can_bce = 0;
	}
	if (term && strstr(term,"screen") == term) {
		/* unfortunately */
		global_config.can_24bit = 0;
		global_config.can_italic = 0;
	}
	if (term && strstr(term,"toaru-vga") == term) {
		global_config.can_24bit = 0; /* Also not strictly true */
		global_config.can_256color = 0; /* Not strictly true */
	}

}

/**
 * Run global initialization tasks
 */
void initialize(void) {
	setlocale(LC_ALL, "");

	detect_weird_terminals();
	load_colorscheme_ansi();
	load_bimrc();

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
	update_screen_size();
	get_initial_termios();
	set_unbuffered();
	mouse_enable();

	signal(SIGWINCH, SIGWINCH_handler);
	signal(SIGCONT,  SIGCONT_handler);
	signal(SIGTSTP,  SIGTSTP_handler);
}

int main(int argc, char * argv[]) {
	int opt;
	while ((opt = getopt(argc, argv, "?c:C:u:RS:O:-:")) != -1) {
		switch (opt) {
			case 'R':
				global_config.initial_file_is_read_only = 1;
				break;
			case 'c':
			case 'C':
				/* Print file to stdout using our syntax highlighting and color theme */
				initialize();
				global_config.go_to_line = 0;
				open_file(optarg);
				for (int i = 0; i < env->line_count; ++i) {
					if (opt == 'C') {
						draw_line_number(i);
					}
					render_line(env->lines[i], 6 * (env->lines[i]->actual + 1), 0, i + 1);
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
				if (!strcmp(optarg,"noaltscreen"))     global_config.can_altscreen = 0;
				else if (!strcmp(optarg,"noscroll"))   global_config.can_scroll = 0;
				else if (!strcmp(optarg,"nomouse"))    global_config.can_mouse = 0;
				else if (!strcmp(optarg,"nounicode"))  global_config.can_unicode = 0;
				else if (!strcmp(optarg,"nobright"))   global_config.can_bright = 0;
				else if (!strcmp(optarg,"nohideshow")) global_config.can_hideshow = 0;
				else if (!strcmp(optarg,"nosyntax"))   global_config.highlight_on_open = 0;
				else if (!strcmp(optarg,"nohistory"))  global_config.history_enabled = 0;
				else if (!strcmp(optarg,"notitle"))    global_config.can_title = 0;
				else if (!strcmp(optarg,"nobce"))      global_config.can_bce = 0;
				else {
					fprintf(stderr, "%s: unrecognized -O option: %s\n", argv[0], optarg);
					return 1;
				}
				break;
			case '-':
				if (!strcmp(optarg,"version")) {
					fprintf(stderr, "bim %s %s\n", BIM_VERSION, BIM_COPYRIGHT);
					fprintf(stderr, " Available syntax highlighters:");
					for (struct syntax_definition * s = syntaxes; s->name; ++s) {
						fprintf(stderr, " %s", s->name);
					}
					fprintf(stderr, "\n");
					fprintf(stderr, " Available color themes:");
					for (struct theme_def * d = themes; d->name; ++d) {
						fprintf(stderr, " %s", d->name);
					}
					fprintf(stderr, "\n");
					return 0;
				} else if (!strcmp(optarg,"help")) {
					show_usage(argv);
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
