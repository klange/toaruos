/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Copyright (C) 2012-2018 K. Lange
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
 *
 * bim - Text editor
 *
 * Bim is inspired by vim, and its name is short for "Bad IMitation".
 *
 * Bim supports syntax highlighting, extensive editing, line selection
 * and copy-paste, undo/redo stack, forward and backward search, and can
 * be built for ToaruOS, Sortix, Linux, macOS, and BSDs.
 *
 * Future goals:
 * - Character selection
 */
#define _XOPEN_SOURCE 500
#define _DARWIN_C_SOURCE 1
#define _BSD_SOURCE
#define _DEFAULT_SOURCE
#define _SORTIX_SOURCE
#define __BSD_VISIBLE 1
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
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#define BLOCK_SIZE 4096
#define ENTER_KEY     '\n'
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
const char * current_theme = "none";

/**
 * Syntax highlighting flags.
 */
#define FLAG_NONE     0
#define FLAG_KEYWORD  1
#define FLAG_STRING   2
#define FLAG_COMMENT  3
#define FLAG_TYPE     4
#define FLAG_PRAGMA   5
#define FLAG_NUMERAL  6
#define FLAG_SELECT   7

#define FLAG_NORM_MAX 15

#define FLAG_COMMENT_ML 16
#define FLAG_STRING_ML1 17
#define FLAG_STRING_ML2 18

/**
 * Convert syntax hilighting flag to color code
 */
const char * flag_to_color(int flag) {
	switch (flag) {
		case FLAG_KEYWORD:
			return COLOR_KEYWORD;
		case FLAG_STRING:
		case FLAG_STRING_ML1:
		case FLAG_STRING_ML2:
			return COLOR_STRING;
		case FLAG_COMMENT:
		case FLAG_COMMENT_ML:
			return COLOR_COMMENT;
		case FLAG_TYPE:
			return COLOR_TYPE;
		case FLAG_NUMERAL:
			return COLOR_NUMERAL;
		case FLAG_PRAGMA:
			return COLOR_PRAGMA;
		case FLAG_SELECT:
			return COLOR_FG;
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
	uint32_t display_width:5;
	uint32_t flags:6;
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
	char_t   text[0];
} line_t;

/**
 * Global configuration state
 */
struct {
	/* Terminal size */
	int term_width, term_height;
	int bottom_size;

	/* Command-line parameters */
	int hilight_on_open;
	int initial_file_is_read_only;

	line_t ** yanks;
	size_t    yank_count;

	int tty_in;

	const char * bimrc_path;
	
	int can_scroll;
	int can_hideshow;
	int can_altscreen;
	int can_mouse;
	int can_unicode;
	int can_bright;
	int history_enabled;
	int can_title;
} global_config = {
	0, /* term_width */
	0, /* term_height */
	2, /* bottom_size */
	1, /* hilight_on_open */
	0, /* initial_file_is_read_only */
	NULL, /* yanks */
	0, /* yank_count */
	STDIN_FILENO, /* tty_in */
	"~/.bimrc", /* bimrc_path */
	1,
	1,
	1,
	1,
	1,
	1,
	0,
	1,
};

void redraw_line(int j, int x);

/**
 * Special implementation of getch with a timeout
 */
int _bim_unget = -1;

void bim_unget(int c) {
	_bim_unget = c;
}

int bim_getch(void) {
	if (_bim_unget != -1) {
		int out = _bim_unget;
		_bim_unget = -1;
		return out;
	}
	struct pollfd fds[1];
	fds[0].fd = global_config.tty_in;
	fds[0].events = POLLIN;
	int ret = poll(fds,1,200);
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
	};
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
	short  loading:1;
	short  tabs:1;
	short  modified:1;
	short  readonly:1;
	short  indent:1;

	short  mode;
	short  tabstop;

	char * file_name;
	int    offset;
	int    coffset;
	int    line_no;
	int    line_count;
	int    line_avail;
	int    col_no;
	uint32_t * search;
	struct syntax_definition * syntax;
	line_t ** lines;

	history_t * history;
	history_t * last_save_history;
} buffer_t;

/**
 * Pointer to current active buffer
 */
buffer_t * env;

/**
 * Editor modes (like in vim)
 */
#define MODE_NORMAL 0
#define MODE_INSERT 1
#define MODE_LINE_SELECTION 2
#define MODE_REPLACE 3

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

	/* Allocate a new buffer */
	buffers[buffers_len] = malloc(sizeof(buffer_t));
	memset(buffers[buffers_len], 0x00, sizeof(buffer_t));
	buffers_len++;

	return buffers[buffers_len-1];
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

/* Based on the wombat256 theme for vim */
void load_colorscheme_wombat(void) {
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
	COLOR_COMMENT   = "5;102;3";
	COLOR_TYPE      = "5;185";
	COLOR_PRAGMA    = "5;173";
	COLOR_NUMERAL   = COLOR_PRAGMA;

	COLOR_ERROR_FG  = "5;15";
	COLOR_ERROR_BG  = "5;196";
	COLOR_SEARCH_FG = "5;234";
	COLOR_SEARCH_BG = "5;226";

	COLOR_SELECTFG  = "5;235";
	COLOR_SELECTBG  = "5;230";

	current_theme = "wombat";
}

/* "City Lights" based on citylights.xyz */
void load_colorscheme_citylights(void) {
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

	current_theme = "citylights";
}

/* Solarized Dark, popular theme */
void load_colorscheme_solarized_dark(void) {
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

	current_theme = "solarized-dark";
}

/* Custom theme */
void load_colorscheme_sunsmoke(void) {
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

	current_theme = "sunsmoke";
}

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

	current_theme = "ansi";
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
	{NULL, NULL}
};


/**
 * Syntax definition for C
 */
int syn_c_iskeywordchar(int c) {
	if (isalnum(c)) return 1;
	if (c == '_') return 1;
	return 0;
}

static char * syn_c_keywords[] = {
	"while","if","for","continue","return","break","switch","case","sizeof",
	"struct","union","typedef","do","default","else","goto",
	"alignas","alignof","offsetof",
	/* C++ stuff */
	"public","private","class","using","namespace",
	NULL
};

static char * syn_c_types[] = {
	"static","int","char","short","float","double","void","unsigned","volatile","const",
	"register","long","inline","restrict","enum","auto","extern","bool","complex",
	"uint8_t","uint16_t","uint32_t","uint64_t",
	"int8_t","int16_t","int32_t","int64_t",
	NULL
};

static int syn_c_extended(line_t * line, int i, int c, int last, int * out_left) {
	if (i == 0 && c == '#') {
		*out_left = line->actual+1;
		return FLAG_PRAGMA;
	}

	if ((!last || !syn_c_iskeywordchar(last)) && (i < line->actual - 3) &&
		line->text[i].codepoint == 'N' &&
		line->text[i+1].codepoint == 'U' &&
		line->text[i+2].codepoint == 'L' &&
		line->text[i+3].codepoint == 'L' &&
		(i == line->actual - 4 || !syn_c_iskeywordchar(line->text[i+4].codepoint))) {
		*out_left = 3;
		return FLAG_NUMERAL;
	}

	if ((!last || !syn_c_iskeywordchar(last)) && isdigit(c)) {
		if (c == '0' && i < line->actual - 1 && line->text[i+1].codepoint == 'x') {
			int j = 2;
			for (; i + j < line->actual && isxdigit(line->text[i+j].codepoint); ++j);
			if (i + j < line->actual && syn_c_iskeywordchar(line->text[i+j].codepoint)) {
				return FLAG_NONE;
			}
			*out_left = j - 1;
			return FLAG_NUMERAL;
		} else {
			int j = 1;
			while (i + j < line->actual && isdigit(line->text[i+j].codepoint)) {
				j++;
			}
			if (i + j < line->actual && syn_c_iskeywordchar(line->text[i+j].codepoint)) {
				return FLAG_NONE;
			}
			*out_left = j - 1;
			return FLAG_NUMERAL;
		}
	}

	if (c == '/') {
		if (i < line->actual - 1 && line->text[i+1].codepoint == '/') {
			*out_left = (line->actual + 1) - i;
			return FLAG_COMMENT;
		}

		if (i < line->actual - 1 && line->text[i+1].codepoint == '*') {
			int last = 0;
			for (int j = i + 2; j < line->actual; ++j) {
				int c = line->text[j].codepoint;
				if (c == '/' && last == '*') {
					*out_left = j - i;
					return FLAG_COMMENT;
				}
				last = c;
			}
			/* TODO multiline - update next */
			*out_left = (line->actual + 1) - i;
			return FLAG_COMMENT_ML;
		}
	}

	if (c == '\'') {
		if (i < line->actual - 3 && line->text[i+1].codepoint == '\\' &&
			line->text[i+3].codepoint == '\'') {
			*out_left = 3;
			return FLAG_NUMERAL;
		}
		if (i < line->actual - 2 && line->text[i+2].codepoint == '\'') {
			*out_left = 2;
			return FLAG_NUMERAL;
		}
	}

	if (c == '"') {
		int last = 0;
		for (int j = i+1; j < line->actual; ++j) {
			int c = line->text[j].codepoint;
			if (last != '\\' && c == '"') {
				*out_left = j - i;
				return FLAG_STRING;
			}
			if (last == '\\' && c == '\\') {
				last = 0;
			}
			last = c;
		}
		*out_left = (line->actual + 1) - i; /* unterminated string */
		return FLAG_STRING;
	}

	return 0;
}

char * syn_c_ext[] = {".c",".h",".cpp",".hpp",".c++",".h++",NULL};

static int syn_c_finish(line_t * line, int * left, int state) {
	if (state == FLAG_COMMENT_ML) {
		int last = 0;
		for (int i = 0; i < line->actual; ++i) {
			if (line->text[i].codepoint == '/' && last == '*') {
				*left = i+2;
				return FLAG_COMMENT;
			}
			last = line->text[i].codepoint;
		}
		return FLAG_COMMENT_ML;
	}
	return 0;
}

/**
 * Syntax definition for Python
 */
static char * syn_py_keywords[] = {
	"class","def","return","del","if","else","elif",
	"for","while","continue","break","assert",
	"as","and","or","except","finally","from",
	"global","import","in","is","lambda","with",
	"nonlocal","not","pass","raise","try","yield",
	NULL
};

static char * syn_py_types[] = {
	"True","False","None",
	"object","set","dict","int","str","bytes",
	NULL
};

static int syn_py_extended(line_t * line, int i, int c, int last, int * out_left) {

	if (i == 0 && c == 'i') {
		/* Check for import */
		char * import = "import ";
		for (int j = 0; j < line->actual + 1; ++j) {
			if (import[j] == '\0') {
				*out_left = j - 2;
				return FLAG_PRAGMA;
			}
			if (line->text[j].codepoint != import[j]) break;
		}
	}

	if (c == '#') {
		*out_left = (line->actual + 1) - i;
		return FLAG_COMMENT;
	}

	if (c == '@') {
		for (int j = i+1; j < line->actual + 1; ++j) {
			if (!syn_c_iskeywordchar(line->text[j].codepoint)) {
				*out_left = j - i - 1;
				return FLAG_PRAGMA;
			}
			*out_left = (line->actual + 1) - i;
			return FLAG_PRAGMA;
		}
	}

	if ((!last || !syn_c_iskeywordchar(last)) && isdigit(c)) {
		if (c == '0' && i < line->actual - 1 && line->text[i+1].codepoint == 'x') {
			int j = 2;
			for (; i + j < line->actual && isxdigit(line->text[i+j].codepoint); ++j);
			if (i + j < line->actual && syn_c_iskeywordchar(line->text[i+j].codepoint)) {
				return FLAG_NONE;
			}
			*out_left = j - 1;
			return FLAG_NUMERAL;
		} else {
			int j = 1;
			while (i + j < line->actual && isdigit(line->text[i+j].codepoint)) {
				j++;
			}
			if (i + j < line->actual && syn_c_iskeywordchar(line->text[i+j].codepoint)) {
				return FLAG_NONE;
			}
			*out_left = j - 1;
			return FLAG_NUMERAL;
		}
	}

	if (line->text[i].codepoint == '\'') {
		if (i + 2 < line->actual && line->text[i+1].codepoint == '\'' && line->text[i+2].codepoint == '\'') {
			/* Begin multiline */
			for (int j = i + 3; j < line->actual - 2; ++j) {
				if (line->text[j].codepoint == '\'' &&
					line->text[j+1].codepoint == '\'' &&
					line->text[j+2].codepoint == '\'') {
					*out_left = (j+2) - i;
					return FLAG_STRING;
				}
			}
			return FLAG_STRING_ML1;
		}

		int last = 0;
		for (int j = i+1; j < line->actual; ++j) {
			int c = line->text[j].codepoint;
			if (last != '\\' && c == '\'') {
				*out_left = j - i;
				return FLAG_STRING;
			}
			if (last == '\\' && c == '\\') {
				last = 0;
			}
			last = c;
		}
		*out_left = (line->actual + 1) - i; /* unterminated string */
		return FLAG_STRING;
	}

	if (line->text[i].codepoint == '"') {
		if (i + 2 < line->actual && line->text[i+1].codepoint == '"' && line->text[i+2].codepoint == '"') {
			/* Begin multiline */
			for (int j = i + 3; j < line->actual - 2; ++j) {
				if (line->text[j].codepoint == '"' &&
					line->text[j+1].codepoint == '"' &&
					line->text[j+2].codepoint == '"') {
					*out_left = (j+2) - i;
					return FLAG_STRING;
				}
			}
			return FLAG_STRING_ML2;
		}

		int last = 0;
		for (int j = i+1; j < line->actual; ++j) {
			int c = line->text[j].codepoint;
			if (last != '\\' && c == '"') {
				*out_left = j - i;
				return FLAG_STRING;
			}
			if (last == '\\' && c == '\\') {
				last = 0;
			}
			last = c;
		}
		*out_left = (line->actual + 1) - i; /* unterminated string */
		return FLAG_STRING;
	}

	return 0;
}

static int syn_py_finish(line_t * line, int * left, int state) {
	/* TODO support multiline quotes */
	if (state == FLAG_STRING_ML1) {
		for (int j = 0; j < line->actual - 2; ++j) {
			if (line->text[j].codepoint == '\'' &&
				line->text[j+1].codepoint == '\'' &&
				line->text[j+2].codepoint == '\'') {
				*left = (j+3);
				return FLAG_STRING;
			}
		}
		return FLAG_STRING_ML1;
	}
	if (state == FLAG_STRING_ML2) {
		for (int j = 0; j < line->actual - 2; ++j) {
			if (line->text[j].codepoint == '"' &&
				line->text[j+1].codepoint == '"' &&
				line->text[j+2].codepoint == '"') {
				*left = (j+3);
				return FLAG_STRING;
			}
		}
		return FLAG_STRING_ML2;
	}
	return 0;
}

char * syn_py_ext[] = {".py",NULL};

/**
 * Syntax definition for ToaruOS shell
 */
static char * syn_sh_keywords[] = {
	"cd","exit","export","help","history","if",
	"empty?","equals?","return","export-cmd",
	"source","exec","not","while","then","else",
	NULL,
};

static int syn_sh_extended(line_t * line, int i, int c, int last, int * out_left) {
	(void)last;

	if (c == '#') {
		*out_left = (line->actual + 1) - i;
		return FLAG_COMMENT;
	}

	if (line->text[i].codepoint == '\'') {
		int last = 0;
		for (int j = i+1; j < line->actual + 1; ++j) {
			int c = line->text[j].codepoint;
			if (last != '\\' && c == '\'') {
				*out_left = j - i;
				return FLAG_STRING;
			}
			if (last == '\\' && c == '\\') {
				last = 0;
			}
			last = c;
		}
		*out_left = (line->actual + 1) - i; /* unterminated string */
		return FLAG_STRING;
	}

	if (line->text[i].codepoint == '"') {
		int last = 0;
		for (int j = i+1; j < line->actual + 1; ++j) {
			int c = line->text[j].codepoint;
			if (last != '\\' && c == '"') {
				*out_left = j - i;
				return FLAG_STRING;
			}
			if (last == '\\' && c == '\\') {
				last = 0;
			}
			last = c;
		}
		*out_left = (line->actual + 1) - i; /* unterminated string */
		return FLAG_STRING;
	}

	return 0;
}

static int syn_sh_iskeywordchar(int c) {
	if (isalnum(c)) return 1;
	if (c == '-') return 1;
	if (c == '_') return 1;
	if (c == '?') return 1;
	return 0;
}

static char * syn_sh_ext[] = {".sh",".eshrc",".esh",NULL};

static char * syn_make_ext[] = {"Makefile","makefile","GNUmakefile",".mak",NULL};

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

static int syn_make_extended(line_t * line, int i, int c, int last, int * out_left) {
	(void)last;

	if (c == '#') {
		*out_left = (line->actual + 1) - i;
		return FLAG_COMMENT;
	}

	if (c == '\t') {
		*out_left = (line->actual + 1) - i;
		return FLAG_NUMERAL;
	}

	if (i == 0) {
		int j = 0;
		for (; j < line->actual; ++j) {
			/* Handle leading spaces */
			if (line->text[j].codepoint != ' ') break;
		}
		for (int s = 0; syn_make_commands[s]; ++s) {
			int d = 0;
			while (j + d < line->actual && line->text[j+d].codepoint == syn_make_commands[s][d]) d++;
			if (syn_make_commands[s][d] == '\0') {
				*out_left = j+d;
				return FLAG_PRAGMA;
			}
		}
	}

	if (last == '(' && i > 1) {
		if (line->text[i-2].codepoint == '$') {
			int j = i;
			for (int s = 0; syn_make_functions[s]; ++s) {
				int d = 0;
				while (j + d < line->actual && line->text[j+d].codepoint == syn_make_functions[s][d]) d++;
				if (syn_make_functions[s][d] == '\0') {
					*out_left = d;
					return FLAG_KEYWORD;
				}
			}
		}
	}

	if (i == 0) {
		int j = 0;
		for (; j < line->actual; ++j) {
			if (line->text[j].codepoint == '=') {
				*out_left = j;
				return FLAG_TYPE;
			}
			if (line->text[j].codepoint == ':') {
				*out_left = j;
				return FLAG_TYPE;
			}
		}
	}


	return FLAG_NONE;
}

static char * syn_bimrc_keywords[] = {
	"theme",
	NULL,
};

static int syn_bimrc_extended(line_t * line, int i, int c, int last, int * out_left) {
	(void)last;
	if (i == 0 && c == '#') {
		*out_left = line->actual+1;
		return FLAG_COMMENT;
	}
	return FLAG_NONE;
}

static char * syn_bimrc_ext[] = {".bimrc",NULL};

static int syn_gitcommit_extended(line_t * line, int i, int c, int last, int * out_left) {
	(void)last;

	if (c == '#') {
		*out_left = (line->actual + 1) - i;
		return FLAG_COMMENT;
	}

	return FLAG_NONE;
}

static char * syn_gitcommit_ext[] = {"COMMIT_EDITMSG",NULL};

/**
 * Syntax hilighting definition database
 */
struct syntax_definition {
	char * name;
	char ** ext;
	char ** keywords;
	char ** types;
	int (*extended)(line_t *, int, int, int, int *);
	int (*iskwchar)(int);
	int (*finishml)(line_t *, int *, int);
} syntaxes[] = {
	{"c",syn_c_ext,syn_c_keywords,syn_c_types,syn_c_extended,syn_c_iskeywordchar,syn_c_finish},
	{"python",syn_py_ext,syn_py_keywords,syn_py_types,syn_py_extended,syn_c_iskeywordchar,syn_py_finish},
	{"esh",syn_sh_ext,syn_sh_keywords,NULL,syn_sh_extended,syn_sh_iskeywordchar,NULL},
	{"make",syn_make_ext,NULL,NULL,syn_make_extended,NULL,NULL},
	{"bimrc",syn_bimrc_ext,syn_bimrc_keywords,NULL,syn_bimrc_extended,syn_c_iskeywordchar,NULL},
	{"gitcommit",syn_gitcommit_ext,NULL,NULL,syn_gitcommit_extended,NULL,NULL},
	{NULL}
};

/**
 * Checks whether the character pointed to by `c` is the start of a match for
 * keyword or type name `str`.
 */
int check_line(line_t * line, int c, char * str, int last) {
	if (env->syntax->iskwchar(last)) return 0;
	for (int i = c; i < line->actual; ++i, ++str) {
		if (*str == '\0' && !env->syntax->iskwchar(line->text[i].codepoint)) return 1;
		if (line->text[i].codepoint == *str) continue;
		return 0;
	}
	if (*str == '\0') return 1;
	return 0;
}

/**
 * Calculate syntax hilighting for the given line.
 */
void recalculate_syntax(line_t * line, int offset) {
	if (!env->syntax) {
		for (int i = 0; i < line->actual; ++i) {
			line->text[i].flags = 0;
		}
		return;
	}

	/* Start from the line's stored in initial state */
	int state = line->istate;
	int left  = 0;
	int last  = 0;

	if (state) {
		/*
		 * If we are already highlighting coming in, then we need to check
		 * for a finishing sequence for the curent state.
		 */
		state = env->syntax->finishml(line,&left,state);

		if (state > FLAG_NORM_MAX) {
			/* The finish check said that this multiline state continues. */
			for (int i = 0; i < line->actual; i++) {
				/* Set the entire line to draw with this state */
				line->text[i].flags = state;
			}

			/* Recalculate later lines if needed */
			goto _multiline;
		}
	}

	for (int i = 0; i < line->actual; last = line->text[i++].codepoint) {
		if (!left) state = 0;

		if (state) {
			/* Currently hilighting, have `left` characters remaining with this state */
			left--;
			line->text[i].flags = state;

			if (!left) {
				/* Done hilighting this state, go back to parsing on next character */
				state = 0;
			}

			/* If we are hilighting something, don't parse */
			continue;
		}

		int c = line->text[i].codepoint;
		line->text[i].flags = FLAG_NONE;

		/* Language-specific syntax hilighting */
		if (env->syntax->extended) {
			int s = env->syntax->extended(line,i,c,last,&left);
			if (s) {
				state = s;
				if (state > FLAG_NORM_MAX) {
					/* A multiline state was returned. Fill the rest of the line */
					for (; i < line->actual; i++) {
						line->text[i].flags = state;
					}
					/* And recalculate later lines if needed */
					goto _multiline;
				}
				goto _continue;
			}
		}

		/* Keywords */
		if (env->syntax->keywords) {
			for (char ** kw = env->syntax->keywords; *kw; kw++) {
				int c = check_line(line, i, *kw, last);
				if (c == 1) {
					left = strlen(*kw)-1;
					state = FLAG_KEYWORD;
					goto _continue;
				}
			}
		}

		/* Type names */
		if (env->syntax->types) {
			for (char ** kw = env->syntax->types; *kw; kw++) {
				int c = check_line(line, i, *kw, last);
				if (c == 1) {
					left = strlen(*kw)-1;
					state = FLAG_TYPE;
					goto _continue;
				}
			}
		}

_continue:
		line->text[i].flags = state;
	}

	state = 0;

_multiline:
	/*
	 * If the next line's initial state does not match the state we ended on,
	 * then it needs to be recalculated (and redraw). This may lead to multiple
	 * recursive calls until a match is found.
	 */
	if (offset + 1 < env->line_count && env->lines[offset+1]->istate != state) {
		/* Set the next line's initial state to our ending state */
		env->lines[offset+1]->istate = state;

		/* Recursively recalculate */
		recalculate_syntax(env->lines[offset+1],offset+1);

		/*
		 * Determine if this is an on-screen line so we can redraw it;
		 * this ends up drawing from bottom to top when multiple lines
		 * need to be redrawn by a recursive call.
		 */
		if (offset+1 >= env->offset && offset+1 < env->offset + global_config.term_height - global_config.bottom_size - 1) {
			redraw_line(offset + 1 - env->offset,offset+1);
		}
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
			free(n->remove_replace_line.contents);
			/* fall-through */
		case HISTORY_REMOVE_LINE:
			free(n->remove_replace_line.old_contents);
			break;
		default:
			/* Nothing extra to free */
			break;
	}

	free(n);
	root->next = NULL;
}

#define HIST_APPEND(e) do { \
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

	if (!env->loading && global_config.history_enabled) {
		history_t * e = malloc(sizeof(history_t));
		e->type = HISTORY_INSERT;
		e->insert_delete_replace.lineno = lineno;
		e->insert_delete_replace.offset = offset;
		e->insert_delete_replace.codepoint = c.codepoint;
		HIST_APPEND(e);
	}

	/* If there is not enough space... */
	if (line->actual == line->available) {
		/* Expand the line buffer */
		line->available *= 2;
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

	if (!env->loading && global_config.history_enabled) {
		history_t * e = malloc(sizeof(history_t));
		e->type = HISTORY_DELETE;
		e->insert_delete_replace.lineno = lineno;
		e->insert_delete_replace.offset = offset;
		e->insert_delete_replace.old_codepoint = line->text[offset-1].codepoint;
		HIST_APPEND(e);
	}

	/* If this isn't the last character, we need to move all subsequent characters backwards */
	if (offset < line->actual) {
		memmove(&line->text[offset-1], &line->text[offset], sizeof(char_t) * (line->actual - offset));
	}

	/* The line is one character shorter */
	line->actual -= 1;

	recalculate_tabs(line);
	recalculate_syntax(line, lineno);
}

/**
 * Replace a character in a line
 */
void line_replace(line_t * line, char_t _c, int offset, int lineno) {

	if (!env->loading && global_config.history_enabled) {
		history_t * e = malloc(sizeof(history_t));
		e->type = HISTORY_REPLACE;
		e->insert_delete_replace.lineno = lineno;
		e->insert_delete_replace.offset = offset;
		e->insert_delete_replace.codepoint = _c.codepoint;
		e->insert_delete_replace.old_codepoint = line->text[offset].codepoint;
		HIST_APPEND(e);
	}

	line->text[offset] = _c;

	if (!env->loading) {
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
		e->remove_replace_line.lineno = offset;
		e->remove_replace_line.old_contents = malloc(sizeof(line_t) + sizeof(char_t) * lines[offset]->available);
		memcpy(e->remove_replace_line.old_contents, lines[offset], sizeof(line_t) + sizeof(char_t) * lines[offset]->available);
		HIST_APPEND(e);
	}

	/* Otherwise, free the data used by the line */
	free(lines[offset]);

	/* Move other lines up */
	if (offset < env->line_count) {
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
		e->add_merge_split_lines.lineno = offset;
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
	lines[offset] = malloc(sizeof(line_t) + sizeof(char_t) * 32);
	lines[offset]->available = 32;
	lines[offset]->actual    = 0;
	lines[offset]->istate    = 0;

	/* There is one new line */
	env->line_count += 1;
	env->lines = lines;

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
		e->remove_replace_line.lineno = offset;
		e->remove_replace_line.old_contents = malloc(sizeof(line_t) + sizeof(char_t) * lines[offset]->available);
		memcpy(e->remove_replace_line.old_contents, lines[offset], sizeof(line_t) + sizeof(char_t) * lines[offset]->available);
		e->remove_replace_line.contents = malloc(sizeof(line_t) + sizeof(char_t) * replacement->available);
		memcpy(e->remove_replace_line.contents, replacement, sizeof(line_t) + sizeof(char_t) * replacement->available);
		HIST_APPEND(e);
	}

	if (lines[offset]->available < replacement->actual) {
		lines[offset] = realloc(lines[offset], sizeof(line_t) + sizeof(char_t) * replacement->available);
		lines[offset]->available = replacement->available;
	}
	lines[offset]->actual = replacement->actual;
	memcpy(&lines[offset]->text, &replacement->text, sizeof(char_t) * replacement->actual);

	if (!env->loading) {
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
		e->add_merge_split_lines.lineno = lineb;
		e->add_merge_split_lines.split = env->lines[linea]->actual;
		HIST_APPEND(e);
	}

	/* If there isn't enough space in linea hold both... */
	while (lines[linea]->available < lines[linea]->actual + lines[lineb]->actual) {
		/* ... allocate more space until it fits */
		lines[linea]->available *= 2;
		/* XXX why not just do this once after calculating appropriate size */
		lines[linea] = realloc(lines[linea], sizeof(line_t) + sizeof(char_t) * lines[linea]->available);
	}

	/* Copy the second line into the first line */
	memcpy(&lines[linea]->text[lines[linea]->actual], &lines[lineb]->text, sizeof(char_t) * lines[lineb]->actual);

	/* The first line is now longer */
	lines[linea]->actual = lines[linea]->actual + lines[lineb]->actual;

	if (!env->loading) {
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
void render_error(char * message, ...);

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
		e->add_merge_split_lines.lineno = line;
		e->add_merge_split_lines.split  = split;
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
	lines[line+1] = malloc(sizeof(line_t) + sizeof(char_t) * v);
	lines[line+1]->available = v;
	lines[line+1]->actual = remaining;
	lines[line+1]->istate = 0;

	/* Move the data from the old line into the new line */
	memmove(lines[line+1]->text, &lines[line]->text[split], sizeof(char_t) * remaining);
	lines[line]->actual = split;

	if (!env->loading) {
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
 * Add indentation from the previous (temporally) line
 */
void add_indent(int new_line, int old_line) {
	if (env->indent) {
		int changed = 0;
		for (int i = 0; i < env->lines[old_line]->actual; ++i) {
			if (env->lines[old_line]->text[i].codepoint == ' ' ||
				env->lines[old_line]->text[i].codepoint == '\t') {
				env->lines[new_line] = line_insert(env->lines[new_line],env->lines[old_line]->text[i],i,new_line);
				env->col_no++;
				changed = 1;
			} else {
				break;
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
	env->lines[0] = malloc(sizeof(line_t) + sizeof(char_t) * 32);
	env->lines[0]->available = 32;
	env->lines[0]->actual    = 0;
	env->lines[0]->istate    = 0;
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
	new.c_lflag &= (~ICANON & ~ECHO);
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
	fflush(stdout);
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
	printf("\033[22;23;");
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
	fflush(stdout);
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
	fflush(stdout);
}

/**
 * Clear the rest of this line
 */
void clear_to_end(void) {
	printf("\033[K");
	fflush(stdout);
}

/**
 * Enable bold text display
 */
void set_bold(void) {
	printf("\033[1m");
	fflush(stdout);
}

/**
 * Enable underlined text display
 */
void set_underline(void) {
	printf("\033[4m");
	fflush(stdout);
}

/**
 * Reset text display attributes
 */
void reset(void) {
	printf("\033[0m");
	fflush(stdout);
}

/**
 * Clear the entire screen
 */
void clear_screen(void) {
	printf("\033[H\033[2J");
	fflush(stdout);
}

/**
 * Hide the cursor
 */
void hide_cursor(void) {
	if (global_config.can_hideshow) {
		printf("\033[?25l");
	}
	fflush(stdout);
}

/*
 * Show the cursor
 */
void show_cursor(void) {
	if (global_config.can_hideshow) {
		printf("\033[?25h");
	}
	fflush(stdout);
}

/**
 * Request mouse events
 */
void mouse_enable(void) {
	if (global_config.can_mouse) {
		printf("\033[?1000h");
	}
	fflush(stdout);
}

/**
 * Stop mouse events
 */
void mouse_disable(void) {
	if (global_config.can_mouse) {
		printf("\033[?1000l");
	}
	fflush(stdout);
}

/**
 * Shift the screen up one line
 */
void shift_up(void) {
	printf("\033[1S");
}

/**
 * Shift the screen down one line.
 */
void shift_down(void) {
	printf("\033[1T");
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
 * Redaw the tabbar, with a tab for each buffer.
 *
 * The active buffer is highlighted.
 */
void redraw_tabbar(void) {
	/* Hide cursor while rendering UI */
	hide_cursor();

	/* Move to upper left */
	place_cursor(1,1);

	/* For each buffer... */
	for (int i = 0; i < buffers_len; i++) {
		buffer_t * _env = buffers[i];

		if (_env == env) {
			/* If this is the active buffer, hilight it */
			reset();
			set_colors(COLOR_FG, COLOR_BG);
			set_bold();
		} else {
			/* Otherwise use default tab color */
			reset();
			set_colors(COLOR_FG, COLOR_TAB_BG);
			set_underline();
		}

		/* If this buffer is modified, indicate that with a prepended + */
		if (_env->modified) {
			printf(" +");
		}

		/* Print the filename */
		if (_env->file_name) {
			printf(" %s ", _env->file_name);
		} else {
			printf(" [No Name] ");
		}
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
void render_line(line_t * line, int width, int offset) {
	int i = 0; /* Offset in char_t line data entries */
	int j = 0; /* Offset in terminal cells */

	const char * last_color = NULL;

	/* Set default text colors */
	set_colors(COLOR_FG, COLOR_BG);

	/*
	 * When we are rendering in the middle of a wide character,
	 * we render -'s to fill the remaining amount of the 
	 * charater's width
	 */
	int remainder = 0;

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
				break;
			}

			/* Syntax hilighting */
			const char * color = flag_to_color(c.flags);
			if (c.flags == FLAG_SELECT) {
				set_colors(COLOR_SELECTFG, COLOR_SELECTBG);
			} else {
				if (!last_color || strcmp(color, last_color)) {
					set_fg_color(color);
					last_color = color;
				}
			}

#define _set_colors(fg,bg) if (c.flags != FLAG_SELECT) { set_colors(fg,bg); }

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
}

/**
 * Get the width of the line number region
 */
int num_width(void) {
	int w = log_base_10(env->line_count) + 1;
	if (w < 2) return 2;
	return w;
}

/**
 * Draw the gutter and line numbers.
 */
void draw_line_number(int x) {
	/* Draw the line number */
	set_colors(COLOR_NUMBER_FG, COLOR_NUMBER_BG);
	int num_size = num_width();
	for (int y = 0; y < num_size - log_base_10(x + 1); ++y) {
		printf(" ");
	}
	printf("%d%c", x + 1, (x+1 == env->line_no && env->coffset > 0) ? '<' : ' ');
}

/**
 * Redraw line.
 *
 * This draws the line number as well as the actual text.
 * j = screen-relative line offset.
 */
void redraw_line(int j, int x) {
	if (env->loading) return;
	/* Hide cursor when drawing */
	hide_cursor();

	/* Move cursor to upper left most cell of this line */
	place_cursor(1,2 + j);

	/* Draw a gutter on the left.
	 * TODO: The gutter can be used to show single-character
	 *       line annotations, such as collapse state, or
	 *       whether a search result was found on this line.
	 */
	set_colors(COLOR_NUMBER_FG, COLOR_ALT_FG);
	printf(" ");

	draw_line_number(x);

	/*
	 * Draw the line text 
	 * If this is the active line, the current character cell offset should be used.
	 * (Non-active lines are not shifted and always render from the start of the line)
	 */
	render_line(env->lines[x], global_config.term_width - 3 - num_width(), (x + 1 == env->line_no) ? env->coffset : 0);

	/* Clear the rest of the line */
	clear_to_end();
}

/**
 * Draw a ~ line where there is no buffer text.
 */
void draw_excess_line(int j) {
	place_cursor(1,2 + j);
	set_colors(COLOR_ALT_FG, COLOR_ALT_BG);
	printf("~");
	clear_to_end();
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
		redraw_line(j,x);
		j++;
	}

	/* Draw the rest of the text region as ~ lines */
	for (; j < l; ++j) {
		draw_excess_line(j);
	}
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
	set_colors(COLOR_STATUS_FG, COLOR_STATUS_BG);

	/* Print the file name */
	if (env->file_name) {
		printf("%s", env->file_name);
	} else {
		printf("[No Name]");
	}

	printf(" ");

	if (env->syntax) {
		printf("[%s]", env->syntax->name);
	}

	/* Print file status indicators */
	if (env->modified) {
		printf("[+]");
	}

	if (env->readonly) {
		printf("[ro]");
	}

	printf(" ");

	if (env->tabs) {
		printf("[tabs]");
	} else {
		printf("[spaces=%d]", env->tabstop);
	}

	if (global_config.yanks) {
		printf("[y:%ld]", global_config.yank_count);
	}

	if (env->indent) {
		printf("[indent]");
	}

	/* Clear the rest of the status bar */
	clear_to_end();

	/* Pre-render the right hand side of the status bar */
	char right_hand[1024];
	snprintf(right_hand, 1024, "Line %d/%d Col: %d ", env->line_no, env->line_count, env->col_no);

	/* Move the cursor appropriately to draw it */
	place_cursor(global_config.term_width - strlen(right_hand), global_config.term_height - 1);
	/* TODO: What if we're localized and this has wide chars? */
	printf("%s",right_hand);
	fflush(stdout);
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
	set_colors(COLOR_FG, COLOR_BG);

	/* If we are in an edit mode, note that. */
	if (env->mode == MODE_INSERT) {
		set_bold();
		printf("-- INSERT --");
		clear_to_end();
		reset();
	} else if (env->mode == MODE_LINE_SELECTION) {
		set_bold();
		printf("-- LINE SELECTION --");
		clear_to_end();
		reset();
	} else if (env->mode == MODE_REPLACE) {
		set_bold();
		printf("-- REPLACE --");
		clear_to_end();
		reset();
	} else {
		clear_to_end();
	}
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
	set_colors(COLOR_FG, COLOR_BG);

	printf("%s", buf);

	/* Clear the rest of the status bar */
	clear_to_end();
}

/**
 * Draw all screen elements
 */
void redraw_all(void) {
	redraw_tabbar();
	redraw_text();
	redraw_statusbar();
	redraw_commandline();
}

/**
 * Update the terminal title bar
 */
void update_title(void) {
	if (!global_config.can_title) return;

	char cwd[1024] = {'/',0};
	getcwd(cwd, 1024);

	for (int i = 1; i < 3; ++i) {
		printf("\033]%d;%s%s (%s) - BIM\007", i, env->file_name ? env->file_name : "[No Name]", env->modified ? " +" : "", cwd);
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
	fflush(stdout);
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

	while (y < 2) {
		y++;
		env->offset--;
		needs_redraw = 1;
	}

	while (y > global_config.term_height - global_config.bottom_size) {
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
	if (x > global_config.term_width - 1) {
		/* Adjust the offset appropriately to scroll horizontally */
		int diff = x - (global_config.term_width - 1);
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

	/* Move the actual terminal cursor */
	place_cursor(x,y);

	/* Show the cursor */
	show_cursor();
}

/**
 * Update screen size
 */
void update_screen_size(void) {
	struct winsize w;
	ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
	global_config.term_width = w.ws_col;
	global_config.term_height = w.ws_row;
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
	redraw_all();
	signal(SIGCONT, SIGCONT_handler);
	signal(SIGTSTP, SIGTSTP_handler);
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
	env->offset = line - 1;
	env->line_no = line;
	env->col_no  = 1;
	redraw_all();
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
				env->lines = add_line(env->lines, env->line_no);
				env->col_no = 1;
				env->line_no += 1;
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
 * Create a new buffer from a file.
 */
void open_file(char * file) {
	env = buffer_new();
	env->loading = 1;

	setup_buffer(env);

	FILE * f;

	if (!strcmp(file,"-")) {
		/**
		 * Read file from stdin. stderr provides terminal input.
		 */
		f = stdin;
		global_config.tty_in = STDERR_FILENO;
		env->modified = 1;
	} else {
		f = fopen(file, "r");
		env->file_name = strdup(file);
	}

	if (!f) {
		if (global_config.hilight_on_open) {
			env->syntax = match_syntax(file);
		}
		env->loading = 0;
		return;
	}

	uint8_t buf[BLOCK_SIZE];

	state = 0;

	while (!feof(f)) {
		size_t r = fread(buf, 1, BLOCK_SIZE, f);
		add_buffer(buf, r);
	}

	if (env->line_no && env->lines[env->line_no-1] && env->lines[env->line_no-1]->actual == 0) {
		/* Remove blank line from end */
		remove_line(env->lines, env->line_no-1);
	}

	if (global_config.hilight_on_open) {
		env->syntax = match_syntax(file);
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

	for (int i = 0; i < env->line_count; ++i) {
		recalculate_tabs(env->lines[i]);
	}

	fclose(f);
}

/**
 * Clean up the terminal and exit the editor.
 */
void quit(void) {
	mouse_disable();
	set_buffered();
	reset();
	clear_screen();
	show_cursor();
	unset_alternate_screen();
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
	quit();
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
				redraw_all();
				return;
			} else {
				env = buffers[buffers_len-1];
				redraw_all();
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
				redraw_all();
				return;
			} else {
				/* Wrap around */
				env = buffers[0];
				redraw_all();
				return;
			}
		}
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

	FILE * f = fopen(file, "w+");

	if (!f) {
		render_error("Failed to open file for writing.");
		return;
	}

	/* Go through each line and convert it back to UTF-8 */
	int i, j;
	for (i = 0; i < env->line_count; ++i) {
		line_t * line = env->lines[i];
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
	/* No more buffers, exit */
	if (!new_env) {
		quit();
	}
	/* Clean up the old buffer */
	free(previous_env);

	/* Set the new active buffer */
	env = new_env;

	/* Redraw the screen */
	redraw_all();
}

/**
 * Move the cursor down one line in the text region
 */
void cursor_down(void) {
	/* If this isn't already the last line... */
	if (env->line_no < env->line_count) {

		/* Move the cursor down */
		env->line_no += 1;

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
		if (env->line_no > env->offset + global_config.term_height - global_config.bottom_size - 1) {
			env->offset += 1;

			/* Tell terminal to scroll */
			if (global_config.can_scroll) {
				shift_up();

				/* A new line appears on screen at the bottom, draw it */
				int l = global_config.term_height - global_config.bottom_size - 1;
				if (env->offset + l < env->line_count + 1) {
					redraw_line(l-1, env->offset + l-1);
				} else {
					draw_excess_line(l - 1);
				}

				/* Redraw elements that were moved by scrolling */
				redraw_tabbar();
				redraw_statusbar();
				redraw_commandline();
				place_cursor_actual();
			} else {
				redraw_all();
			}
			return;
		} else if (redraw) {
			/* Otherwise, if we need to redraw because of coffset change, do that */
			redraw_text();
		}

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

		if (env->line_no <= env->offset) {
			env->offset -= 1;

			/* Tell terminal to scroll */
			if (global_config.can_scroll) {
				shift_down();

				/*
				 * The line at the top of the screen should always be real
				 * so we can just call redraw_line here
				 */
				redraw_line(0,env->offset);

				/* Redraw elements that were moved by scrolling */
				redraw_tabbar();
				redraw_statusbar();
				redraw_commandline();
				place_cursor_actual();
			} else {
				redraw_all();
			}
			return;
		} else if (redraw) {
			/* Otherwise, if we need to redraw because of coffset change, do that */
			redraw_text();
		}

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
}

/**
 * Move the cursor to the fron the of the line
 */
void cursor_home(void) {
	env->col_no = 1;

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
	}
	set_history_break();
	env->mode = MODE_NORMAL;
	redraw_commandline();
}

/**
 * Process a user command.
 */
void process_command(char * cmd) {
	/* Special case ! to run shell commands without parsing tokens */
	if (*cmd == '!') {
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
		fflush(stdout);
		while (bim_getch() != ENTER_KEY);

		/* Redraw the screen */
		redraw_all();

		/* Done processing command */
		return;
	}

	/* Tokenize argument string on spaces */
	char *p, *argv[512], *last;
	int argc = 0;
	for ((p = strtok_r(cmd, " ", &last)); p;
			(p = strtok_r(NULL, " ", &last)), argc++) {
		if (argc < 511) argv[argc] = p;
	}
	argv[argc] = NULL;

	if (argc < 1) {
		/* no op */
		return;
	}

	if (!strcmp(argv[0], "e")) {
		/* e: edit file */
		if (argc > 1) {
			/* This actually opens a new tab */
			open_file(argv[1]);
			update_title();
			goto_line(0);
		} else {
			/* TODO: Reopen file? */
			render_error("Expected a file to open...");
		}
	} else if (!strcmp(argv[0], "tabnew")) {
		env = buffer_new();
		setup_buffer(env);
		redraw_all();
	} else if (!strcmp(argv[0], "w")) {
		/* w: write file */
		if (argc > 1) {
			write_file(argv[1]);
		} else {
			write_file(env->file_name);
		}
	} else if (!strcmp(argv[0], "wq")) {
		/* wq: write file and close buffer; if there's no file to write to, may do weird things */
		write_file(env->file_name);
		close_buffer();
	} else if (!strcmp(argv[0], "q")) {
		/* close buffer if unmodified */
		if (env->modified) {
			render_error("No write since last change. Use :q! to force exit.");
		} else {
			close_buffer();
		}
	} else if (!strcmp(argv[0], "q!")) {
		/* close buffer without warning if unmodified */
		close_buffer();
	} else if (!strcmp(argv[0], "qa") || !strcmp(argv[0], "qall")) {
		/* Close all */
		try_quit();
	} else if (!strcmp(argv[0], "qa!")) {
		/* Forcefully exit editor */
		quit();
	} else if (!strcmp(argv[0], "tabp")) {
		/* Next tab */
		previous_tab();
	} else if (!strcmp(argv[0], "tabn")) {
		/* Previous tab */
		next_tab();
	} else if (!strcmp(argv[0], "indent")) {
		env->indent = 1;
		redraw_statusbar();
	} else if (!strcmp(argv[0], "noindent")) {
		env->indent = 0;
		redraw_statusbar();
	} else if (!strcmp(argv[0], "noh")) {
		if (env->search) {
			free(env->search);
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
		render_commandline_message(" \033[1mbim - The standard ToaruOS Text Editor\033[22m\n");
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
		render_commandline_message(" Copyright 2013-2018 K. Lange <\033[3mklange@toaruos.org\033[23m>\n");
		render_commandline_message("\n");
		/* Redrawing the tabbar makes it look like we just shifted the whole view up */
		redraw_tabbar();
		redraw_commandline();
		fflush(stdout);
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
	} else if (!strcmp(argv[0], "theme")) {
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
	} else if (!strcmp(argv[0], "syntax")) {
		if (argc < 2) {
			render_status_message("syntax=%s", env->syntax ? env->syntax->name : "none");
			return;
		}
		if (!strcmp(argv[1],"none")) {
			for (int i = 0; i < env->line_count; ++i) {
				env->lines[i]->istate = 0;
				for (int j = 0; j < env->lines[i]->actual; ++j) {
					env->lines[i]->text[j].flags = 0;
				}
			}
			redraw_all();
			return;
		}
		for (struct syntax_definition * s = syntaxes; s->name; ++s) {
			if (!strcmp(argv[1],s->name)) {
				env->syntax = s;
				for (int i = 0; i < env->line_count; ++i) {
					env->lines[i]->istate = 0;
				}
				for (int i = 0; i < env->line_count; ++i) {
					recalculate_syntax(env->lines[i],i);
				}
				redraw_all();
				return;
			}
		}
		render_error("unrecognized syntax type");
	} else if (!strcmp(argv[0], "recalc")) {
		for (int i = 0; i < env->line_count; ++i) {
			env->lines[i]->istate = 0;
		}
		for (int i = 0; i < env->line_count; ++i) {
			recalculate_syntax(env->lines[i],i);
		}
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
	} else if (isdigit(*argv[0])) {
		/* Go to line number */
		goto_line(atoi(argv[0]));
	} else {
		/* Unrecognized command */
		render_error("Not an editor command: %s", argv[0]);
	}
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
		add_candidate("help");
		add_candidate("recalc");
		add_candidate("syntax");
		add_candidate("tabn");
		add_candidate("tabp");
		add_candidate("tabnew");
		add_candidate("theme");
		add_candidate("tabs");
		add_candidate("tabstop");
		add_candidate("spaces");
		add_candidate("noh");
		add_candidate("clearyank");
		add_candidate("indent");
		add_candidate("noindent");
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

	if (arg == 1 && !strcmp(args[0], "theme")) {
		/* Complete color theme names */
		for (struct theme_def * s = themes; s->name; ++s) {
			add_candidate(s->name);
		}
		goto _accept_candidate;
	}

	if (arg == 1 && !strcmp(args[0], "e")) {
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
			if (ent->d_name[0] != '.') {
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
		/* Print candidates in status bar */
		char tmp[global_config.term_width+1];
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
	}

	/* Free candidates */
	for (int i = 0; i < candidate_count; ++i) {
		free(candidates[i]);
	}

	/* Redraw command line */
done:
	redraw_commandline();
	printf(":%s", buffer);

	free(candidates);
	free(buf);
}

/**
 * Command mode
 *
 * Accept a user command and then process it and
 * return to normal mode.
 *
 * TODO: We only have basic line editing here; it might be
 *       nice to add more advanced line editing, like cursor
 *       movement, tab completion, etc. This is easier than
 *       with the shell since we have a lot more control over
 *       where the command input bar is rendered.
 */
void command_mode(void) {
	int c;
	char buffer[1024] = {0};
	int  buffer_len = 0;

	redraw_commandline();
	printf(":");
	show_cursor();

	while ((c = bim_getch())) {
		if (c == -1) {
			/* Time out */
			continue;
		}
		if (c == '\033') {
			/* Escape, cancel command */
			break;
		} else if (c == ENTER_KEY) {
			/* Enter, run command */
			process_command(buffer);
			break;
		} else if (c == '\t') {
			/* Handle tab completion */
			command_tab_complete(buffer);
			buffer_len = strlen(buffer);
		} else if (c == BACKSPACE_KEY || c == DELETE_KEY) {
			/* Backspace, delete last character in command buffer */
			if (buffer_len > 0) {
				buffer_len -= 1;
				buffer[buffer_len] = '\0';
				redraw_commandline();
				printf(":%s", buffer);
			} else {
				/* If backspaced through entire command, cancel command mode */
				redraw_commandline();
				break;
			}
		} else {
			/* Regular character */
			buffer[buffer_len] = c;
			buffer_len++;
			printf("%c", c);
		}
		show_cursor();
	}
}

/**
 * Search forward from the given cursor position
 * to find a basic search match.
 *
 * This could be more complicated...
 */
void find_match(int from_line, int from_col, int * out_line, int * out_col, uint32_t * str) {
	int col = from_col;
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
				/* TODO search for UTF-8 sequences? */
				if (*match != line->text[k].codepoint) break;
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
				/* TODO search for UTF-8 sequences? */
				if (*match != line->text[k].codepoint) break;
				match++;
				k++;
			}
			j--;
		}
		col = (i > 1) ? (env->lines[i-2]->actual) : -1;
	}

}

/**
 * Draw the matched search result.
 */
void draw_search_match(int line, uint32_t * buffer, int redraw_buffer) {
	place_cursor_actual();
	redraw_text();
	if (line != -1) {
		/*
		 * TODO this should probably mark the relevant
		 * regions so that redraw_text can hilight it
		 */
		set_colors(COLOR_SEARCH_FG, COLOR_SEARCH_BG);
		place_cursor_actual();

		uint32_t * c = buffer;
		while (*c) {
			char tmp[7] = {0}; /* Max six bytes, use 7 to ensure last is always nil */
			to_eight(*c, tmp);
			printf("%s", tmp);
			c++;
		}
	}
	redraw_statusbar();
	redraw_commandline();
	if (redraw_buffer) {
		printf("/");
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
void search_mode(void) {
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
	printf("/");
	show_cursor();

	uint32_t state = 0;
	int cin;

	while ((cin = bim_getch())) {
		if (cin == -1) {
			/* Time out */
			continue;
		}
		if (!decode(&state, &c, cin)) {
			if (c == '\033') {
				/* Cancel search */
				env->line_no = prev_line;
				env->col_no  = prev_col;
				redraw_all();
				break;
			} else if (c == ENTER_KEY) {
				/* Exit search */
				if (env->search) {
					free(env->search);
				}
				env->search = malloc((buffer_len + 1) * sizeof(uint32_t));
				memcpy(env->search, buffer, (buffer_len + 1) * sizeof(uint32_t));
				break;
			} else if (c == BACKSPACE_KEY || c == DELETE_KEY) {
				/* Backspace, delete last character in search buffer */
				if (buffer_len > 0) {
					buffer_len -= 1;
					buffer[buffer_len] = '\0';
					/* Search from beginning to find first match */
					int line = -1, col = -1;
					find_match(prev_line, prev_col, &line, &col, buffer);

					if (line != -1) {
						env->col_no = col;
						env->line_no = line;
					}

					draw_search_match(line, buffer, 1);

				} else {
					/* If backspaced through entire search term, cancel search */
					redraw_commandline();
					env->coffset = prev_coffset;
					env->offset = prev_offset;
					env->col_no = prev_col;
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
				find_match(prev_line, prev_col, &line, &col, buffer);

				if (line != -1) {
					env->col_no = col;
					env->line_no = line;
				} else {
					env->coffset = prev_coffset;
					env->offset = prev_offset;
					env->col_no = prev_col;
					env->line_no = prev_line;
				}
				draw_search_match(line, buffer, 1);
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
	if (!env->search) return;
	int line = -1, col = -1;
	find_match(env->line_no, env->col_no+1, &line, &col, env->search);

	if (line == -1) {
		find_match(1,1, &line, &col, env->search);
		if (line == -1) return;
	}

	env->col_no = col;
	env->line_no = line;
	draw_search_match(line, env->search, 0);
}

/**
 * Find the previous search result, or loop to the end of the file.
 */
void search_prev(void) {
	if (!env->search) return;
	int line = -1, col = -1;
	find_match_backwards(env->line_no, env->col_no-1, &line, &col, env->search);

	if (line == -1) {
		render_error("no match");
		find_match_backwards(env->line_count, env->lines[env->line_count-1]->actual, &line, &col, env->search);
		if (line == -1) return;
	}

	env->col_no = col;
	env->line_no = line;
	draw_search_match(line, env->search, 0);
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
void find_matching_paren(void) {
	if (env->col_no > env->lines[env->line_no-1]->actual) {
		return; /* Invalid cursor position */
	}

	/* TODO: vim can find the nearest paren to start searching from, we need to be on one right now */

	int paren_match = 0;
	int direction = 0;
	int start = env->lines[env->line_no-1]->text[env->col_no-1].codepoint;
	int flags = env->lines[env->line_no-1]->text[env->col_no-1].flags;
	int count = 0;

	/* TODO what about unicode parens? */
	char * p = "()[]{}<>";
	for (int i = 0; p[i]; ++i) {
		if (start == p[i]) {
			direction = (i % 2 == 0) ? 1 : -1;
			paren_match = p[(i % 2 == 0) ? (i+1) : (i-1)];
			break;
		}
	}

	if (!paren_match) return;

	/* Scan for match */
	int line = env->line_no;
	int col  = env->col_no;

	do {
		while (col > 0 && col < env->lines[line-1]->actual + 1) {
			/* Only match on same syntax */
			if (env->lines[line-1]->text[col-1].flags == flags) {
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
	env->line_no = line;
	env->col_no = col;
	place_cursor_actual();
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
		for (int i = 0; i < 5; ++i) {
			cursor_up();
		}
		return;
	} else if (buttons == 65) {
		/* Scroll down */
		for (int i = 0; i < 5; ++i) {
			cursor_down();
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
				if (_env->modified) {
					_x += 2;
				}
				if (_env->file_name) {
					_x += 2 + strlen(_env->file_name);
				} else {
					_x += strlen(" [No Name] ");
				}
				if (_x > x) {
					env = buffers[i];
					redraw_all();
					return;
				}
			}
			return;
		}

		/* Figure out y coordinate */
		int line_no = y + env->offset - 1;
		int col_no = -1;

		if (line_no > env->line_count) {
			line_no = env->line_count;
		}

		/* Account for the left hand gutter */
		int num_size = num_width() + 3;
		int _x = num_size - (line_no == env->line_no ? env->coffset : 0);

		/* Determine where the cursor is physically */
		for (int i = 0; i < env->lines[line_no-1]->actual; ++i) {
			char_t * c = &env->lines[line_no-1]->text[i];
			_x += c->display_width;
			if (_x > x) {
				col_no = i;
				break;
			}
		}

		if (col_no == -1 || col_no > env->lines[line_no-1]->actual) {
			col_no = env->lines[line_no-1]->actual;
		}

		env->line_no = line_no;
		env->col_no = col_no;
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
	redraw_line(env->line_no - env->offset - 1, env->line_no-1);
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

	redraw_line(env->line_no - env->offset - 1, env->line_no-1);
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
						env->lines[e->insert_delete_replace.lineno],
						e->insert_delete_replace.offset+1,
						e->insert_delete_replace.lineno
				);
				env->line_no = e->insert_delete_replace.lineno + 1;
				env->col_no  = e->insert_delete_replace.offset + 1;
				count_chars++;
				break;
			case HISTORY_DELETE:
				{
					char_t _c = {codepoint_width(e->insert_delete_replace.old_codepoint),0,e->insert_delete_replace.old_codepoint};
					env->lines[e->insert_delete_replace.lineno] = line_insert(
							env->lines[e->insert_delete_replace.lineno],
							_c,
							e->insert_delete_replace.offset-1,
							e->insert_delete_replace.lineno
					);
				}
				env->line_no = e->insert_delete_replace.lineno + 1;
				env->col_no  = e->insert_delete_replace.offset + 2;
				count_chars++;
				break;
			case HISTORY_REPLACE:
				{
					char_t _o = {codepoint_width(e->insert_delete_replace.old_codepoint),0,e->insert_delete_replace.old_codepoint};
					line_replace(
							env->lines[e->insert_delete_replace.lineno],
							_o,
							e->insert_delete_replace.offset,
							e->insert_delete_replace.lineno
					);
				}
				env->line_no = e->insert_delete_replace.lineno + 1;
				env->col_no  = e->insert_delete_replace.offset + 1;
				count_chars++;
				break;
			case HISTORY_REMOVE_LINE:
				env->lines = add_line(env->lines, e->remove_replace_line.lineno);
				replace_line(env->lines, e->remove_replace_line.lineno, e->remove_replace_line.old_contents);
				env->line_no = e->remove_replace_line.lineno + 2;
				env->col_no = 1;
				count_lines++;
				break;
			case HISTORY_ADD_LINE:
				env->lines = remove_line(env->lines, e->add_merge_split_lines.lineno);
				env->line_no = e->add_merge_split_lines.lineno + 1;
				env->col_no = 1;
				count_lines++;
				break;
			case HISTORY_REPLACE_LINE:
				replace_line(env->lines, e->remove_replace_line.lineno, e->remove_replace_line.old_contents);
				env->line_no = e->remove_replace_line.lineno + 1;
				env->col_no = 1;
				count_lines++;
				break;
			case HISTORY_SPLIT_LINE:
				env->lines = merge_lines(env->lines, e->add_merge_split_lines.lineno+1);
				env->line_no = e->add_merge_split_lines.lineno + 2;
				env->col_no = 1;
				count_lines++;
				break;
			case HISTORY_MERGE_LINES:
				env->lines = split_line(env->lines, e->add_merge_split_lines.lineno-1, e->add_merge_split_lines.split);
				env->line_no = e->add_merge_split_lines.lineno;
				env->col_no = 1;
				count_lines++;
				break;
			case HISTORY_BREAK:
				/* Ignore break */
				break;
			default:
				render_error("Unknown type %d!\n", e->type);
				break;
		}

		env->history = e->previous;
		e = env->history;
	} while (e->type != HISTORY_BREAK);

	if (env->line_no > env->line_count) env->line_no = env->line_count;
	if (env->col_no > env->lines[env->line_no-1]->actual) env->col_no = env->lines[env->line_no-1]->actual;

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
					char_t _c = {codepoint_width(e->insert_delete_replace.codepoint),0,e->insert_delete_replace.codepoint};
					env->lines[e->insert_delete_replace.lineno] = line_insert(
							env->lines[e->insert_delete_replace.lineno],
							_c,
							e->insert_delete_replace.offset,
							e->insert_delete_replace.lineno
					);
				}
				env->line_no = e->insert_delete_replace.lineno + 1;
				env->col_no  = e->insert_delete_replace.offset + 2;
				count_chars++;
				break;
			case HISTORY_DELETE:
				/* Delete */
				line_delete(
						env->lines[e->insert_delete_replace.lineno],
						e->insert_delete_replace.offset,
						e->insert_delete_replace.lineno
				);
				env->line_no = e->insert_delete_replace.lineno + 1;
				env->col_no  = e->insert_delete_replace.offset + 1;
				count_chars++;
				break;
			case HISTORY_REPLACE:
				{
					char_t _o = {codepoint_width(e->insert_delete_replace.codepoint),0,e->insert_delete_replace.codepoint};
					line_replace(
							env->lines[e->insert_delete_replace.lineno],
							_o,
							e->insert_delete_replace.offset,
							e->insert_delete_replace.lineno
					);
				}
				env->line_no = e->insert_delete_replace.lineno + 1;
				env->col_no  = e->insert_delete_replace.offset + 2;
				count_chars++;
				break;
			case HISTORY_ADD_LINE:
				env->lines = add_line(env->lines, e->remove_replace_line.lineno);
				env->line_no = e->remove_replace_line.lineno + 2;
				env->col_no = 1;
				count_lines++;
				break;
			case HISTORY_REMOVE_LINE:
				env->lines = remove_line(env->lines, e->remove_replace_line.lineno);
				env->line_no = e->add_merge_split_lines.lineno + 1;
				env->col_no = 1;
				count_lines++;
				break;
			case HISTORY_REPLACE_LINE:
				replace_line(env->lines, e->remove_replace_line.lineno, e->remove_replace_line.contents);
				env->line_no = e->remove_replace_line.lineno + 2;
				env->col_no = 1;
				count_lines++;
				break;
			case HISTORY_MERGE_LINES:
				env->lines = merge_lines(env->lines, e->add_merge_split_lines.lineno);
				env->line_no = e->remove_replace_line.lineno + 1;
				env->col_no = 1;
				count_lines++;
				break;
			case HISTORY_SPLIT_LINE:
				env->lines = split_line(env->lines, e->add_merge_split_lines.lineno, e->add_merge_split_lines.split);
				env->line_no = e->remove_replace_line.lineno + 2;
				env->col_no = 1;
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

	if (env->line_no > env->line_count) env->line_no = env->line_count;
	if (env->col_no > env->lines[env->line_no-1]->actual) env->col_no = env->lines[env->line_no-1]->actual;

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
	redraw_all();
	render_commandline_message("%d character%s, %d line%s changed",
			count_chars, (count_chars == 1) ? "" : "s",
			count_lines, (count_lines == 1) ? "" : "s");
}

/**
 * Move the cursor the start of the previous word.
 */
void word_left(void) {
	int line_no = env->line_no;
	int col_no = env->col_no;

	do {
		col_no--;
		if (col_no == 0) {
			line_no--;
			if (line_no == 0) {
				goto_line(1);
				return;
			}
			col_no = env->lines[line_no-1]->actual + 1;
		}
	} while (isspace(env->lines[line_no-1]->text[col_no-1].codepoint));

	do {
		col_no--;
		if (col_no == 0) {
			line_no--;
			if (line_no == 0) {
				goto_line(1);
				return;
			}
			col_no = env->lines[line_no-1]->actual + 1;
		}
		if (col_no == 1) {
			env->col_no = 1;
			env->line_no = line_no;
			redraw_statusbar();
			place_cursor_actual();
			return;
		}
	} while (!isspace(env->lines[line_no-1]->text[col_no-1].codepoint));

	env->col_no = col_no;
	env->line_no = line_no;
	cursor_right();
}

/**
 * Word right
 */
void word_right(void) {
	int line_no = env->line_no;
	int col_no = env->col_no;

	do {
		col_no++;
		if (col_no >= env->lines[line_no-1]->actual + 1) {
			line_no++;
			if (line_no >= env->line_count) {
				env->col_no = env->lines[env->line_count-1]->actual;
				env->line_no = env->line_count;
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
		if (col_no >= env->lines[line_no-1]->actual + 1) {
			line_no++;
			if (line_no >= env->line_count) {
				env->col_no = env->lines[env->line_count-1]->actual;
				env->line_no = env->line_count;
				redraw_statusbar();
				place_cursor_actual();
				return;
			}
			col_no = 1;
			break;
		}
	} while (isspace(env->lines[line_no-1]->text[col_no-1].codepoint));

	env->col_no = col_no;
	env->line_no = line_no;
	redraw_statusbar();
	place_cursor_actual();
	return;
}

int handle_escape(int * this_buf, int * timeout, int c) {
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
				if (this_buf[*timeout-1] == '5') {
					word_right();
				} else {
					cursor_right();
				}
				break;
			case 'D': // left
				if (this_buf[*timeout-1] == '5') {
					word_left();
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
								redraw_line(env->line_no - env->offset - 1, env->line_no-1);
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
 * Macro for redrawing selected lines with appropriate highlighting.
 */
#define _redraw_line(line, force_start_line) \
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
			recalculate_syntax(env->lines[(line)-1],(line)-1); \
		} else { \
			for (int j = 0; j < env->lines[(line)-1]->actual; ++j) { \
				env->lines[(line)-1]->text[j].flags = FLAG_SELECT; \
			} \
		} \
		if ((line) - env->offset + 1 > 1 && \
			(line) - env->offset - 1< global_config.term_height - global_config.bottom_size - 1) { \
			redraw_line((line) - env->offset - 1, (line)-1); \
		} \
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
		if ((direction == -1) && env->lines[start_point + i]->actual < 1) continue;
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
	set_modified();
}


/**
 * LINE SELECTION mode
 *
 * Equivalent to visual line in vim; selects lines of texts.
 */
void line_selection_mode(void) {
	int start_line = env->line_no;
	int prev_line  = start_line;

	env->mode = MODE_LINE_SELECTION;
	redraw_commandline();

	int c;
	int timeout = 0;
	int this_buf[20];

	for (int j = 0; j < env->lines[env->line_no-1]->actual; ++j) {
		env->lines[env->line_no-1]->text[j].flags = FLAG_SELECT;
	}
	redraw_line(env->line_no - env->offset - 1, env->line_no-1);

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
					case ':':
						/* Switch to command mode */
						command_mode();
						break;
					case '/':
						/* Switch to search mode */
						search_mode();
						break;
					case '\t':
						if (env->readonly) goto _readonly;
						adjust_indent(start_line, 1);
						break;
					case 'V':
						goto _leave_select_line;
					case 'n':
						search_next();
						break;
					case 'N':
						search_prev();
						break;
					case 'j':
						cursor_down();
						break;
					case 'k':
						cursor_up();
						break;
					case 'h':
						cursor_left();
						break;
					case 'l':
						cursor_right();
						break;
					case 'y':
						yank_lines(start_line, env->line_no);
						goto _leave_select_line;
					case 'D':
					case 'd':
						if (env->readonly) goto _readonly;
						yank_lines(start_line, env->line_no);
						if (start_line <= env->line_no) {
							int lines_to_delete = env->line_no - start_line + 1;
							for (int i = 0; i < lines_to_delete; ++i) {
								remove_line(env->lines, start_line-1);
							}
							env->line_no = start_line;
						} else {
							int lines_to_delete = start_line - env->line_no + 1;
							for (int i = 0; i < lines_to_delete; ++i) {
								remove_line(env->lines, env->line_no-1);
							}
						}
						if (env->line_no > env->line_count) {
							env->line_no = env->line_count;
						}
						set_modified();
						goto _leave_select_line;
					case ' ':
						goto_line(env->line_no + global_config.term_height - 6);
						break;
					case '%':
						for (int i = 0; i < env->line_count; ++i) {
							recalculate_syntax(env->lines[i],i);
						}
						find_matching_paren();
						redraw_statusbar();
						break;
					case '{':
						env->col_no = 1;
						if (env->line_no == 1) break;
						do {
							env->line_no--;
							if (env->lines[env->line_no-1]->actual == 0) break;
						} while (env->line_no > 1);
						redraw_statusbar();
						break;
					case '}':
						env->col_no = 1;
						if (env->line_no == env->line_count) break;
						do {
							env->line_no++;
							if (env->lines[env->line_no-1]->actual == 0) break;
						} while (env->line_no < env->line_count);
						redraw_statusbar();
						break;
					case '$':
						cursor_end();
						break;
					case '^':
					case '0':
						cursor_home();
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
						adjust_indent(start_line, -1);
						break;
				}
			}

			/* Mark current line */
			_redraw_line(env->line_no,0);

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

/**
 * Backspace from the current cursor position.
 */
void delete_at_cursor(void) {
	if (env->col_no > 1) {
		line_delete(env->lines[env->line_no - 1], env->col_no - 1, env->line_no - 1);
		env->col_no -= 1;
		redraw_line(env->line_no - env->offset - 1, env->line_no-1);
		set_modified();
		redraw_statusbar();
		place_cursor_actual();
	} else if (env->line_no > 1) {
		int tmp = env->lines[env->line_no - 2]->actual;
		merge_lines(env->lines, env->line_no - 1);
		env->line_no -= 1;
		env->col_no = tmp+1;
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
	env->col_no = 1;
	env->line_no += 1;
	add_indent(env->line_no-1,env->line_no-2);
	if (env->line_no > env->offset + global_config.term_height - global_config.bottom_size - 1) {
		env->offset += 1;
	}
	redraw_text();
	set_modified();
	redraw_statusbar();
	place_cursor_actual();
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
						delete_at_cursor();
						break;
					case ENTER_KEY:
						insert_line_feed();
						break;
					case '\t':
						if (env->tabs) {
							insert_char('\t');
						} else {
							for (int i = 0; i < env->tabstop; ++i) {
								insert_char(' ');
							}
						}
						redraw_statusbar();
						place_cursor_actual();
						break;
					default:
						insert_char(c);
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

/*
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
							place_cursor_actual();
						} else {
							cursor_left();
						}
						break;
					case ENTER_KEY:
						insert_line_feed();
						break;
					default:
						if (env->col_no <= env->lines[env->line_no - 1]->actual) {
							replace_char(c);
							env->col_no += 1;
						} else {
							insert_char(c);
						}
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

static void show_usage(char * argv[]) {
#define _S "\033[3m"
#define _E "\033[0m\n"
	printf(
			"bim - Text editor\n"
			"\n"
			"usage: %s [options] [file]\n"
			"       %s [options] -\n"
			"\n"
			" -R     " _S "open initial buffer read-only" _E
			" -O     " _S "set various options:" _E
			"        noscroll    " _S "disable terminal scrolling" _E
			"        noaltscreen " _S "disable alternate screen buffer" _E
			"        nomouse     " _S "disable mouse support" _E
			"        nounicode   " _S "disable unicode display" _E
			"        nobright    " _S "disable bright next" _E
			"        nohideshow  " _S "disable togglging cursor visibility" _E
			"        nosyntax    " _S "disable syntax highlighting on load" _E
			"        notitle     " _S "disable title-setting escapes" _E
			"        history     " _S "enable experimental undo/redo" _E
			" -c,-C  " _S "print file to stdout with syntax hilighting" _E
			"        " _S "-C includes line numbers, -c does not" _E
			" -u     " _S "override bimrc file" _E
			" -?     " _S "show this help text" _E
			"\n", argv[0], argv[0]);
#undef _E
#undef _S
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

	if (!bimrc) {
		/* No bimrc, or bad permissions */
		free(tmp);
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

		/* Extrace value from keypair, if available
		 * (I forsee options without values in the future) */
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
			global_config.history_enabled = 1;
		}
	}

	fclose(bimrc);
}

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
	while ((opt = getopt(argc, argv, "?c:C:u:RO:")) != -1) {
		switch (opt) {
			case 'R':
				global_config.initial_file_is_read_only = 1;
				break;
			case 'c':
			case 'C':
				/* Print file to stdout using our syntax highlighting and color theme */
				initialize();
				open_file(optarg);
				for (int i = 0; i < env->line_count; ++i) {
					if (opt == 'C') {
						draw_line_number(i);
					}
					render_line(env->lines[i], 6 * (env->lines[i]->actual + 1), 0);
					clear_to_end();
					reset();
					fprintf(stdout, "\n");
				}
				return 0;
			case 'u':
				global_config.bimrc_path = optarg;
				break;
			case 'O':
				/* Set various display options */
				if (!strcmp(optarg,"noaltscreen"))     global_config.can_altscreen = 0;
				else if (!strcmp(optarg,"noscroll"))   global_config.can_scroll = 0;
				else if (!strcmp(optarg,"nomouse"))    global_config.can_mouse = 0;
				else if (!strcmp(optarg,"nounicode"))  global_config.can_unicode = 0;
				else if (!strcmp(optarg,"nobright"))   global_config.can_bright = 0;
				else if (!strcmp(optarg,"nohideshow")) global_config.can_hideshow = 0;
				else if (!strcmp(optarg,"nosyntax"))   global_config.hilight_on_open = 0;
				else if (!strcmp(optarg,"nohistory"))  global_config.history_enabled = 0;
				else if (!strcmp(optarg,"history"))    global_config.history_enabled = 1;
				else if (!strcmp(optarg,"notitle"))    global_config.can_title = 0;
				else {
					fprintf(stderr, "%s: unrecognized -O option: %s\n", argv[0], optarg);
					return 1;
				}
				break;
			case '?':
				show_usage(argv);
				return 0;
		}
	}

	initialize();
	init_terminal();

	if (argc > optind) {
		open_file(argv[optind]);
		update_title();
		goto_line(0);
		if (global_config.initial_file_is_read_only) {
			env->readonly = 1;
		}
	} else {
		env = buffer_new();
		update_title();
		setup_buffer(env);
	}

	redraw_all();

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
							place_cursor_actual();
						} else {
							cursor_left();
						}
						break;
					case ':':
						/* Switch to command mode */
						command_mode();
						break;
					case '/':
						/* Switch to search mode */
						search_mode();
						break;
					case 'V':
						line_selection_mode();
						break;
					case 'n':
						search_next();
						break;
					case 'N':
						search_prev();
						break;
					case 'j':
						cursor_down();
						break;
					case 'k':
						cursor_up();
						break;
					case 'h':
						cursor_left();
						break;
					case 'l':
						cursor_right();
						break;
					case ' ':
						goto_line(env->line_no + global_config.term_height - 6);
						break;
					case 'O':
						{
							if (env->readonly) goto _readonly;
							env->lines = add_line(env->lines, env->line_no-1);
							env->col_no = 1;
							add_indent(env->line_no-1,env->line_no);
							redraw_text();
							set_modified();
							place_cursor_actual();
							goto _insert;
						}
					case 'o':
						{
							if (env->readonly) goto _readonly;
							env->lines = add_line(env->lines, env->line_no);
							env->col_no = 1;
							env->line_no += 1;
							add_indent(env->line_no-1,env->line_no-2);
							if (env->line_no > env->offset + global_config.term_height - global_config.bottom_size - 1) {
								env->offset += 1;
							}
							redraw_text();
							set_modified();
							place_cursor_actual();
							goto _insert;
						}
					case 'a':
						if (env->col_no < env->lines[env->line_no-1]->actual + 1) {
							env->col_no += 1;
						}
						goto _insert;
					case 'P':
					case 'p':
						if (env->readonly) goto _readonly;
						if (global_config.yanks) {
							for (unsigned int i = 0; i < global_config.yank_count; ++i) {
								env->lines = add_line(env->lines, env->line_no - (c == 'P' ? 1 : 0));
							}
							for (unsigned int i = 0; i < global_config.yank_count; ++i) {
								replace_line(env->lines, env->line_no - (c == 'P' ? 1 : 0) + i, global_config.yanks[i]);
							}
							for (int i = 0; i < env->line_count; ++i) {
								env->lines[i]->istate = 0;
							}
							for (int i = 0; i < env->line_count; ++i) {
								recalculate_syntax(env->lines[i],i);
							}
							set_modified();
							redraw_all();
						}
						break;
					case '%':
						find_matching_paren();
						redraw_statusbar();
						break;
					case '{':
						env->col_no = 1;
						if (env->line_no == 1) break;
						do {
							env->line_no--;
							if (env->lines[env->line_no-1]->actual == 0) break;
						} while (env->line_no > 1);
						redraw_statusbar();
						break;
					case '}':
						env->col_no = 1;
						if (env->line_no == env->line_count) break;
						do {
							env->line_no++;
							if (env->lines[env->line_no-1]->actual == 0) break;
						} while (env->line_no < env->line_count);
						redraw_statusbar();
						break;
					case '$':
						cursor_end();
						break;
					case '^':
					case '0':
						cursor_home();
						break;
					case 'u':
						undo_history();
						break;
					case 18: /* ^R */
						redo_history();
						break;
					case 'i':
_insert:
						if (env->readonly) goto _readonly;
						insert_mode();
						redraw_statusbar();
						redraw_commandline();
						timeout = 0;
						break;
					case 'R':
						if (env->readonly) goto _readonly;
						replace_mode();
						redraw_statusbar();
						redraw_commandline();
						timeout = 0;
						break;
_readonly:
						render_error("Buffer is read-only");
						break;
					case 12:
						redraw_all();
						break;
				}
			} else {
				handle_escape(this_buf,&timeout,c);
			}
			place_cursor_actual();
		}
	}

	return 0;
}
