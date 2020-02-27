/**
 * This is a baked, single-file version of bim.
 * It was built Thu Feb 27 21:56:23 2020
 * It is based on git commit 83e6cc609584bd31e961c3873a9f3a5c7c2973ec
 */
#define GIT_TAG "83e6cc6-baked"
/* Bim - A Text Editor
 *
 * Copyright (C) 2012-2020 K. Lange
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

/* Included from bim-core.h */
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

#ifdef __TIMESTAMP__
# define BIM_BUILD_DATE " built " __TIMESTAMP__
#else
# define BIM_BUILD_DATE DATE ""
#endif

#ifdef GIT_TAG
# define TAG "-" GIT_TAG
#else
# define TAG ""
#endif

#define BIM_VERSION   "2.6.1" TAG
#define BIM_COPYRIGHT "Copyright 2012-2020 K. Lange <\033[3mklange@toaruos.org\033[23m>"

#define BLOCK_SIZE 4096
#define ENTER_KEY     '\r'
#define LINE_FEED     '\n'
#define BACKSPACE_KEY 0x08
#define DELETE_KEY    0x7F

enum Key {
	KEY_TIMEOUT = -1,
	KEY_CTRL_AT = 0, /* Base */
	KEY_CTRL_A, KEY_CTRL_B, KEY_CTRL_C, KEY_CTRL_D, KEY_CTRL_E, KEY_CTRL_F, KEY_CTRL_G, KEY_CTRL_H,
	KEY_CTRL_I, KEY_CTRL_J, KEY_CTRL_K, KEY_CTRL_L, KEY_CTRL_M, KEY_CTRL_N, KEY_CTRL_O, KEY_CTRL_P,
	KEY_CTRL_Q, KEY_CTRL_R, KEY_CTRL_S, KEY_CTRL_T, KEY_CTRL_U, KEY_CTRL_V, KEY_CTRL_W, KEY_CTRL_X,
	KEY_CTRL_Y, KEY_CTRL_Z, /* Note we keep ctrl-z mapped in termios as suspend */
	KEY_CTRL_OPEN, KEY_CTRL_BACKSLASH, KEY_CTRL_CLOSE, KEY_CTRL_CARAT, KEY_CTRL_UNDERSCORE,
	/* Space... */
	/* Some of these are equivalent to things above */
	KEY_BACKSPACE = 0x08,
	KEY_LINEFEED = '\n',
	KEY_ENTER = '\r',
	KEY_TAB = '\t',
	/* Basic printable characters go here. */
	/* Delete is special */
	KEY_DELETE = 0x7F,
	/* Unicode codepoints go here */
	KEY_ESCAPE = 0x400000, /* Escape would normally be 27, but is special because reasons */
	KEY_F1, KEY_F2, KEY_F3, KEY_F4,
	KEY_F5, KEY_F6, KEY_F7, KEY_F8,
	KEY_F9, KEY_F10, KEY_F11, KEY_F12,
	/* TODO ALT, SHIFT, etc., for F keys */
	KEY_MOUSE, /* Must be followed with a 3-byte mouse read */
	KEY_MOUSE_SGR, /* Followed by an SGR-style sequence of mouse data */
	KEY_HOME, KEY_END, KEY_PAGE_UP, KEY_PAGE_DOWN,
	KEY_UP, KEY_DOWN, KEY_RIGHT, KEY_LEFT,
	KEY_SHIFT_UP, KEY_SHIFT_DOWN, KEY_SHIFT_RIGHT, KEY_SHIFT_LEFT,
	KEY_CTRL_UP, KEY_CTRL_DOWN, KEY_CTRL_RIGHT, KEY_CTRL_LEFT,
	KEY_ALT_UP, KEY_ALT_DOWN, KEY_ALT_RIGHT, KEY_ALT_LEFT,
	KEY_ALT_SHIFT_UP, KEY_ALT_SHIFT_DOWN, KEY_ALT_SHIFT_RIGHT, KEY_ALT_SHIFT_LEFT,
	KEY_SHIFT_TAB,
	/* Special signals for paste start, paste end */
	KEY_PASTE_BEGIN, KEY_PASTE_END,
};

struct key_name_map {
	enum Key keycode;
	char * name;
};

extern struct key_name_map KeyNames[];

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
 *
 * At the moment, this is all in a global, but in the future
 * this should be passed around to various functions.
 */
typedef struct {
	/* Terminal size */
	int term_width, term_height;
	int bottom_size;

	line_t ** yanks;
	size_t    yank_count;
	int       yank_is_full_lines;

	int tty_in;

	const char * bimrc_path;
	const char * syntax_fallback;
	uint32_t * search;

	int overlay_mode;
	line_t * command_buffer;

	int command_offset, command_col_no;
	struct syntax_definition * command_syn, * command_syn_back;
	int history_point;
	int search_direction;
	int prev_line, prev_col, prev_coffset, prev_offset;

	unsigned int highlight_on_open:1;
	unsigned int initial_file_is_read_only:1;
	unsigned int go_to_line:1;
	unsigned int break_from_selection:1;
	unsigned int can_scroll:1;
	unsigned int can_hideshow:1;
	unsigned int can_altscreen:1;
	unsigned int can_mouse:1;
	unsigned int can_unicode:1;
	unsigned int can_bright:1;
	unsigned int can_title:1;
	unsigned int can_bce:1;
	unsigned int can_24bit:1;
	unsigned int can_256color:1;
	unsigned int can_italic:1;
	unsigned int can_insert:1;
	unsigned int can_bracketedpaste:1;
	unsigned int history_enabled:1;
	unsigned int highlight_parens:1;
	unsigned int smart_case:1;
	unsigned int highlight_current_line:1;
	unsigned int shift_scrolling:1;
	unsigned int check_git:1;
	unsigned int color_gutter:1;
	unsigned int relative_lines:1;
	unsigned int numbers:1;
	unsigned int horizontal_shift_scrolling:1;
	unsigned int hide_statusbar:1;
	unsigned int tabs_visible:1;
	unsigned int autohide_tabs:1;
	unsigned int smart_complete:1;
	unsigned int has_terminal:1;
	unsigned int use_sgr_mouse:1;
	unsigned int search_wraps:1;

	int cursor_padding;
	int split_percent;
	int scroll_amount;
	int tab_offset;

	char * tab_indicator;
	char * space_indicator;

} global_config_t;

#define OVERLAY_MODE_NONE     0
#define OVERLAY_MODE_READ_ONE 1
#define OVERLAY_MODE_COMMAND  2
#define OVERLAY_MODE_SEARCH   3
#define OVERLAY_MODE_COMPLETE 4

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
	unsigned int numbers:1;
	unsigned int gutter:1;

	int highlighting_paren;
	int maxcolumn;

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
	int start_col;
	int prev_line;
} buffer_t;

struct theme_def {
	const char * name;
	void (*load)(const char * name);
};

extern struct theme_def * themes;

extern void add_colorscheme(struct theme_def theme);

struct syntax_state {
	line_t * line;
	int line_no;
	int state;
	int i;
};

struct completion_match {
	char * string;
	char * file;
	char * search;
};

struct syntax_definition {
	char * name;
	char ** ext;
	int (*calculate)(struct syntax_state *);
	int prefers_spaces;
	int (*completion_qualifier)(int c);
	int (*completion_matcher)(uint32_t * comp, struct completion_match ** matches, int * matches_count, int complete_match, int * matches_len);
};

extern struct syntax_definition * syntaxes;

/**
 * Editor mode states
 */
#define MODE_NORMAL 0
#define MODE_INSERT 1
#define MODE_LINE_SELECTION 2
#define MODE_REPLACE 3
#define MODE_CHAR_SELECTION 4
#define MODE_COL_SELECTION 5
#define MODE_COL_INSERT 6
#define MODE_DIRECTORY_BROWSE 7

extern global_config_t global_config;

extern const char * COLOR_FG;
extern const char * COLOR_BG;
extern const char * COLOR_ALT_FG;
extern const char * COLOR_ALT_BG;
extern const char * COLOR_NUMBER_FG;
extern const char * COLOR_NUMBER_BG;
extern const char * COLOR_STATUS_FG;
extern const char * COLOR_STATUS_BG;
extern const char * COLOR_STATUS_ALT;
extern const char * COLOR_TABBAR_BG;
extern const char * COLOR_TAB_BG;
extern const char * COLOR_ERROR_FG;
extern const char * COLOR_ERROR_BG;
extern const char * COLOR_SEARCH_FG;
extern const char * COLOR_SEARCH_BG;
extern const char * COLOR_KEYWORD;
extern const char * COLOR_STRING;
extern const char * COLOR_COMMENT;
extern const char * COLOR_TYPE;
extern const char * COLOR_PRAGMA;
extern const char * COLOR_NUMERAL;
extern const char * COLOR_SELECTFG;
extern const char * COLOR_SELECTBG;
extern const char * COLOR_RED;
extern const char * COLOR_GREEN;
extern const char * COLOR_BOLD;
extern const char * COLOR_LINK;
extern const char * COLOR_ESCAPE;
extern const char * current_theme;

struct action_def {
	char * name;
	void (*action)();
	int options;
	const char * description;
};

extern struct action_def * mappable_actions;

#define ARG_IS_INPUT   0x01 /* Takes the key that triggered it as the first argument */
#define ARG_IS_CUSTOM  0x02 /* Takes a custom argument which is specific to the method */
#define ARG_IS_PROMPT  0x04 /* Prompts for an argument. */
#define ACTION_IS_RW   0x08 /* Needs to be able to write. */

#define BIM_ACTION(name, options, description) \
	void name (); /* Define the action with unknown arguments */ \
	void __attribute__((constructor)) _install_ ## name (void) { \
		add_action((struct action_def){#name, name, options, description}); \
	} \
	void name

struct command_def {
	char * name;
	int (*command)(char *, int, char * arg[]);
	const char * description;
};

#define BIM_COMMAND(cmd_name, cmd_str, description) \
	int bim_command_ ## cmd_name (char * cmd, int argc, char * argv[]); \
	void __attribute__((constructor)) _install_cmd_ ## cmd_name (void) { \
		add_command((struct command_def){cmd_str, bim_command_ ## cmd_name, description}); \
	} \
	int bim_command_ ## cmd_name (char * cmd __attribute__((unused)), int argc __attribute__((unused)), char * argv[] __attribute__((unused)))

#define BIM_ALIAS(alias, alias_name, cmd_name) \
	void __attribute__((constructor)) _install_alias_ ## alias_name (void) { \
		add_command((struct command_def){alias, bim_command_ ## cmd_name, "Alias for " #cmd_name}); \
	}

#define BIM_PREFIX_COMMAND(cmd_name, cmd_prefix, description) \
	int bim_command_ ## cmd_name (char * cmd, int argc, char * argv[]); \
	void __attribute__((constructor)) _install_cmd_ ## cmd_name (void) { \
		add_prefix_command((struct command_def){cmd_prefix, bim_command_ ## cmd_name, description}); \
	} \
	int bim_command_ ## cmd_name (char * cmd __attribute__((unused)), int argc __attribute__((unused)), char * argv[] __attribute__((unused)))

extern buffer_t * env;
extern buffer_t * left_buffer;
extern buffer_t * right_buffer;
#define NAV_BUFFER_MAX 10
extern char nav_buf[NAV_BUFFER_MAX+1];
extern int nav_buffer;
extern int    buffers_len;
extern int    buffers_avail;
extern buffer_t ** buffers;

extern const char * flag_to_color(int _flag);
extern void redraw_line(int x);
extern int git_examine(char * filename);
extern void search_next(void);
extern void set_preferred_column(void);
extern void quit(const char * message);
extern void close_buffer(void);
extern void set_syntax_by_name(const char * name);
extern void rehighlight_search(line_t * line);
extern void try_to_center();
extern int read_one_character(char * message);
extern void bim_unget(int c);
#define bim_getch() bim_getch_timeout(200)
extern int bim_getch_timeout(int timeout);
extern buffer_t * buffer_new(void);
extern FILE * open_biminfo(void);
extern int fetch_from_biminfo(buffer_t * buf);
extern int update_biminfo(buffer_t * buf);
extern buffer_t * buffer_close(buffer_t * buf);
extern int to_eight(uint32_t codepoint, char * out);
extern char * name_from_key(enum Key keycode);
extern void add_action(struct action_def action);
extern void open_file(char * file);
extern void recalculate_selected_lines(void);
extern void add_command(struct command_def command);
extern void add_prefix_command(struct command_def command);
extern void render_command_input_buffer(void);
extern void unhighlight_matching_paren(void);

extern void add_syntax(struct syntax_definition def);

struct ColorName {
	const char * name;
	const char ** value;
};

extern struct ColorName color_names[];

struct bim_function {
	char * command;
	struct bim_function * next;
};

extern struct bim_function ** user_functions;
extern int run_function(char * name);
extern int has_function(char * name);
extern void find_matching_paren(int * out_line, int * out_col, int in_col);
extern void render_error(char * message, ...);
extern void pause_for_key(void);

#define add_match(match_string, match_file, match_search) do { \
	if (*matches_count == *matches_len) { \
		(*matches_len) *= 2; \
		*matches = realloc(*matches, sizeof(struct completion_match) * (*matches_len)); \
	} \
	(*matches)[*matches_count].string = strdup(match_string); \
	(*matches)[*matches_count].file = strdup(match_file); \
	(*matches)[*matches_count].search = strdup(match_search); \
	(*matches_count)++; \
} while (0)

#define add_if_match(name,desc) do { \
	int i = 0; \
	while (comp[i] && comp[i] == (unsigned char)name[i]) i++; \
	if (comp[i] == '\0') { \
		add_match(name,desc,""); \
	} \
} while (0)

struct action_map {
	int key;
	void (*method)();
	int options;
	int arg;
};

#define opt_rep  0x1 /* This action will be repeated */
#define opt_arg  0x2 /* This action will take a specified argument */
#define opt_char 0x4 /* This action will read a character to pass as an argument */
#define opt_nav  0x8 /* This action will consume the nav buffer as its argument */
#define opt_rw   0x10 /* Must not be read-only */
#define opt_norm 0x20 /* Returns to normal mode */
#define opt_byte 0x40 /* Same as opt_char but forces a byte */

struct mode_names {
	const char * description;
	const char * name;
	struct action_map ** mode;
};

extern struct mode_names mode_names[];

/* End of bim-core.h */

/* Included from bim-syntax.h */
#define BIM_SYNTAX(name, spaces) \
	__attribute__((constructor)) static void _load_ ## name (void) { \
		add_syntax((struct syntax_definition){#name, syn_ ## name ## _ext, syn_ ## name ## _calculate, spaces, NULL, NULL}); \
	} \

#define BIM_SYNTAX_EXT(name, spaces, matcher) \
	__attribute__((constructor)) static void _load_ ## name (void) { \
		add_syntax((struct syntax_definition){#name, syn_ ## name ## _ext, syn_ ## name ## _calculate, spaces, matcher, _match_completions_ ## name}); \
	} \

#define BIM_SYNTAX_COMPLETER(name) \
	static int _match_completions_ ## name ( \
		uint32_t * comp __attribute__((unused)), \
		struct completion_match **matches __attribute__((unused)), \
		int * matches_count __attribute__((unused)), \
		int complete_match __attribute__((unused)), \
		int *matches_len __attribute__((unused)))

#define paint(length, flag) do { for (int i = 0; i < (length) && state->i < state->line->actual; i++, state->i++) { state->line->text[state->i].flags = (flag); } } while (0)
#define charat() (state->i < state->line->actual ? state->line->text[(state->i)].codepoint : -1)
#define nextchar() (state->i + 1 < state->line->actual ? state->line->text[(state->i+1)].codepoint : -1)
#define lastchar() (state->i - 1 >= 0 ? state->line->text[(state->i-1)].codepoint : -1)
#define skip() (state->i++)
#define charrel(x) (state->i + (x) < state->line->actual ? state->line->text[(state->i+(x))].codepoint : -1)

extern int find_keywords(struct syntax_state * state, char ** keywords, int flag, int (*keyword_qualifier)(int c));
extern int match_and_paint(struct syntax_state * state, const char * keyword, int flag, int (*keyword_qualifier)(int c));
extern void paint_single_string(struct syntax_state * state);
extern void paint_simple_string(struct syntax_state * state);
extern int common_comment_buzzwords(struct syntax_state * state);
extern int paint_comment(struct syntax_state * state);
extern int match_forward(struct syntax_state * state, char * c);
extern struct syntax_definition * find_syntax_calculator(const char * name);

#define nest(lang, low) \
	do { \
		state->state = (state->state < 1 ? 0 : state->state - low); \
		do { state->state = lang(state); } while (state->state == 0); \
		if (state->state == -1) return low; \
		return state->state + low; \
	} while (0)

/* Some of the C stuff is widely used */
extern int c_keyword_qualifier(int c);
extern int paint_c_numeral(struct syntax_state * state);
extern int paint_c_comment(struct syntax_state * state);
extern void paint_c_char(struct syntax_state * state);

/* Hacky workaround for isdigit not really accepting Unicode stuff */
static __attribute__((used)) int _isdigit(int c) { if (c > 128) return 0; return isdigit(c); }
static __attribute__((used)) int _isxdigit(int c) { if (c > 128) return 0; return isxdigit(c); }

#undef isdigit
#undef isxdigit
#define isdigit(c) _isdigit(c)
#define isxdigit(c) _isxdigit(c)


/* End of bim-syntax.h */

global_config_t global_config = {
	/* State */
	.term_width = 0,
	.term_height = 0,
	.bottom_size = 2,
	.yanks = NULL,
	.yank_count = 0,
	.yank_is_full_lines = 0,
	.tty_in = STDIN_FILENO,
	.bimrc_path = "~/.bimrc",
	.syntax_fallback = NULL, /* syntax to fall back to if none other match applies */
	.search = NULL,
	.overlay_mode = OVERLAY_MODE_NONE,
	.command_buffer = NULL,
	.command_offset = 0,
	.command_col_no = 0,
	.history_point = -1,
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
	.use_sgr_mouse = 0,
	.search_wraps = 1,
	/* Integer config values */
	.cursor_padding = 4,
	.split_percent = 50,
	.scroll_amount = 5,
	.tab_offset = 0,
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

FLEXIBLE_ARRAY(mappable_actions, add_action, struct action_def, ((struct action_def){NULL,NULL,0,NULL}))
FLEXIBLE_ARRAY(regular_commands, add_command, struct command_def, ((struct command_def){NULL,NULL,NULL}))
FLEXIBLE_ARRAY(prefix_commands, add_prefix_command, struct command_def, ((struct command_def){NULL,NULL,NULL}))
FLEXIBLE_ARRAY(themes, add_colorscheme, struct theme_def, ((struct theme_def){NULL,NULL}))
FLEXIBLE_ARRAY(user_functions, add_user_function, struct bim_function *, NULL)

/**
 * Special implementation of getch with a timeout
 */
int _bim_unget = -1;

void bim_unget(int c) {
	_bim_unget = c;
}

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

	while ((cin = bim_getch_timeout(read_timeout))) {
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
						case 'P': timeout = 0; return KEY_F1;
						case 'Q': timeout = 0; return KEY_F2;
						case 'R': timeout = 0; return KEY_F3;
						case 'S': timeout = 0; return KEY_F4;
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
						case 'M': timeout = 0; return KEY_MOUSE;
						case '<': timeout = 0; return KEY_MOUSE_SGR;
						case 'A': return shift_key(KEY_UP);
						case 'B': return shift_key(KEY_DOWN);
						case 'C': return shift_key(KEY_RIGHT);
						case 'D': return shift_key(KEY_LEFT);
						case 'H': timeout = 0; return KEY_HOME;
						case 'F': timeout = 0; return KEY_END;
						case 'I': timeout = 0; return KEY_PAGE_UP;
						case 'G': timeout = 0; return KEY_PAGE_DOWN;
						case 'Z': timeout = 0; return KEY_SHIFT_TAB;
						case '~':
							if (timeout == 3) {
								switch (this_buf[2]) {
									case '1': timeout = 0; return KEY_HOME;
									case '3': timeout = 0; return KEY_DELETE;
									case '4': timeout = 0; return KEY_END;
									case '5': timeout = 0; return KEY_PAGE_UP;
									case '6': timeout = 0; return KEY_PAGE_DOWN;
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

enum Key key_from_name(char * name) {
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
			else return -1; /* Multiple characters */
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
	if (buffers_len && global_config.tab_offset >= buffers_len) global_config.tab_offset--;
	global_config.tabs_visible = (!global_config.autohide_tabs) || (buffers_len > 1);
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
 * Theming data
 *
 * This default set is pretty simple "default foreground on default background"
 * except for search and selections which are black-on-white specifically.
 *
 * The theme colors get set by separate configurable themes.
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
 * Find keywords from a list and paint them, assuming they aren't in the middle of other words.
 * Returns 1 if a keyword from the last was found, otherwise 0.
 */
int find_keywords(struct syntax_state * state, char ** keywords, int flag, int (*keyword_qualifier)(int c)) {
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
int match_and_paint(struct syntax_state * state, const char * keyword, int flag, int (*keyword_qualifier)(int c)) {
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

void paint_single_string(struct syntax_state * state) {
	paint(1, FLAG_STRING);
	while (charat() != -1) {
		if (charat() == '\\' && nextchar() == '\'') {
			paint(2, FLAG_ESCAPE);
		} else if (charat() == '\'') {
			paint(1, FLAG_STRING);
			return;
		} else if (charat() == '\\') {
			paint(2, FLAG_ESCAPE);
		} else {
			paint(1, FLAG_STRING);
		}
	}
}


void paint_simple_string(struct syntax_state * state) {
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
 * This is a basic character matcher for "keyword" characters.
 */
int simple_keyword_qualifier(int c) {
	return isalnum(c) || (c == '_');
}

/**
 * These words can appear in comments and should be highlighted.
 * Since there are a lot of comment highlighters, let's break them out.
 */
int common_comment_buzzwords(struct syntax_state * state) {
	if (match_and_paint(state, "TODO", FLAG_NOTICE, simple_keyword_qualifier)) { return 1; }
	else if (match_and_paint(state, "XXX", FLAG_NOTICE, simple_keyword_qualifier)) { return 1; }
	else if (match_and_paint(state, "FIXME", FLAG_ERROR, simple_keyword_qualifier)) { return 1; }
	return 0;
}

/**
 * Paint a comment until end of line, assumes this comment can not continue.
 * (Some languages have comments that can continue with a \ - don't use this!)
 * Assumes you've already painted your comment start characters.
 */
int paint_comment(struct syntax_state * state) {
	while (charat() != -1) {
		if (common_comment_buzzwords(state)) continue;
		else { paint(1, FLAG_COMMENT); }
	}
	return -1;
}

/**
 * Match a word forward and return whether it was matched.
 */
int match_forward(struct syntax_state * state, char * c) {
	int i = 0;
	while (1) {
		if (charrel(i) == -1 && !*c) return 1;
		if (charrel(i) != *c) return 0;
		c++;
		i++;
	}
	return 0;
}

/**
 * Find and return a highlighter by name, or NULL
 */
struct syntax_definition * find_syntax_calculator(const char * name) {
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
			if (line_no != -1) rehighlight_search(line);
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

	if (!env->loading) {
		unhighlight_matching_paren();
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
	return (line->text[i].codepoint == '{' || line->text[i].codepoint == ':') ? i+1 : 0;
}

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
	new.c_iflag &= (~ICRNL) & (~IXON);
	new.c_lflag &= (~ICANON) & (~ECHO) & (~ISIG);
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

char * color_string(const char * fg, const char * bg) {
	static char output[100];
	char * t = output;
	t += sprintf(t,"\033[22;23;24;");
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
		if (global_config.use_sgr_mouse) {
			printf("\033[?1006h");
		}
	}
}

/**
 * Stop mouse events
 */
void mouse_disable(void) {
	if (global_config.can_mouse) {
		if (global_config.use_sgr_mouse) {
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
		if (!decode(&state, &c, (unsigned char)*t)) {

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
 * Redaw the tabbar, with a tab for each buffer.
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
				printf("▏");
			} else {
				printf("|");
			}
		}

		/* Fill the rest with the alternate background color */
		set_colors(COLOR_ALT_FG, COLOR_ALT_BG);
	}

	if (env->left + env->width == global_config.term_width && global_config.can_bce) {
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

	/*
	 * Draw the line text 
	 * If this is the active line, the current character cell offset should be used.
	 * (Non-active lines are not shifted and always render from the start of the line)
	 */
	render_line(env->lines[x], env->width - gutter_width() - num_width(), (x + 1 == env->line_no || global_config.horizontal_shift_scrolling) ? env->coffset : 0, x+1);

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
int display_width_of_string(char * str) {
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

int statusbar_append_status(int *remaining_width, char * output, char * base, ...) {
	va_list args;
	va_start(args, base);
	char tmp[100]; /* should be big enough */
	vsnprintf(tmp, 100, base, args);
	va_end(args);

	int width = display_width_of_string(tmp) + 2;

	if (width < *remaining_width) {
		strcat(output,color_string(COLOR_STATUS_ALT, COLOR_STATUS_BG));
		strcat(output,"[");
		strcat(output,color_string(COLOR_STATUS_FG, COLOR_STATUS_BG));
		strcat(output, tmp);
		strcat(output,color_string(COLOR_STATUS_ALT, COLOR_STATUS_BG));
		strcat(output,"]");
		(*remaining_width) -= width;
		return width;
	} else {
		return 0;
	}
}

int statusbar_build_right(char * right_hand) {
	char tmp[1024];
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
 * The right side of the tatus bar shows the line number and column.
 */
void redraw_statusbar(void) {
	if (global_config.hide_statusbar) return;
	if (!env) return;
	/* Hide cursor while rendering */
	hide_cursor();

	/* Move cursor to the status bar line (second from bottom */
	place_cursor(1, global_config.term_height - 1);

	/* Set background colors for status line */
	paint_line(COLOR_STATUS_BG);
	set_colors(COLOR_STATUS_FG, COLOR_STATUS_BG);


	/* Pre-render the right hand side of the status bar */
	char right_hand[1024];
	int right_width = statusbar_build_right(right_hand);

	char status_bits[1024] = {0}; /* Sane maximum */
	int status_bits_width = 0;

	int remaining_width = global_config.term_width - right_width;

#define ADD(...) do { status_bits_width += statusbar_append_status(&remaining_width, status_bits, __VA_ARGS__); } while (0)
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

BIM_ACTION(redraw_all, 0,
	"Repaint the screen."
)(void) {
	if (!env) return;
	redraw_tabbar();
	redraw_text();
	if (left_buffer) {
		redraw_alt_buffer(left_buffer == env ? right_buffer : left_buffer);
	}
	redraw_statusbar();
	redraw_commandline();
	if (global_config.overlay_mode == OVERLAY_MODE_COMMAND || global_config.overlay_mode == OVERLAY_MODE_SEARCH) {
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

	if (env) {
		/* Hide cursor while rendering */
		hide_cursor();

		/* Move cursor to the command line */
		place_cursor(1, global_config.term_height);

		/* Set appropriate error message colors */
		set_colors(COLOR_ERROR_FG, COLOR_ERROR_BG);

		/* Draw the message */
		printf("%s", buf);
	} else {
		printf("bim: error during startup: %s\n", buf);
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
		for (int i = env->highlighting_paren - 1; i <= env->highlighting_paren + 1; ++i) {
			if (i >= 1 && i <= env->line_count) {
				recalculate_syntax(env->lines[i-1], i-1);
				redraw_line(i-1);
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

void try_to_center() {
	int half_a_screen = (global_config.term_height - 3) / 2;
	if (half_a_screen < env->line_no) {
		env->offset = env->line_no - half_a_screen;
	} else {
		env->offset = 0;
	}
}

BIM_ACTION(suspend, 0,
	"Suspend bim and the rest of the job it was run in."
)(void) {
	kill(0, SIGTSTP);
}

/**
 * Move the cursor to a specific line.
 */
BIM_ACTION(goto_line, ARG_IS_CUSTOM,
	"Jump to the requested line."
)(int line) {

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
				env->lines[i]->text[j].flags = 0;
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
	for (int i = 0; i < env->line_count; ++i) {
		recalculate_syntax(env->lines[i],i);
	}
	env->readonly = 1;
	env->loading = 0;
	env->mode = MODE_DIRECTORY_BROWSE;
	env->line_no = 1;
	redraw_all();
}

BIM_ACTION(open_file_from_line, 0,
	"When browsing a directory, open the file under the cursor."
)(void) {
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
	uint32_t c, state = 0;
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
	if (has_function("onload:*")) {
		run_function("onload:*");
	}
	if (env->syntax) {
		char tmp[512];
		sprintf(tmp, "onload:%s", env->syntax->name);
		if (has_function(tmp)) {
			run_function(tmp);
		}
	}
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

		struct stat statbuf;
		if (!stat(_file, &statbuf) && S_ISDIR(statbuf.st_mode)) {
			read_directory_into_buffer(_file);
			if (file != _file) free(_file);
			return;
		}
		f = fopen(_file, "r");
		if (file != _file) free(_file);
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
	} else if (spaces == tabs && env->syntax) {
		env->tabs = env->syntax->prefers_spaces;
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
	"Switch the previoius tab"
)(void) {
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
)(void) {
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

	FILE * f = fopen(file, "w+");

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
)(void) {
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
)(void) {
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
)(void) {
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
)(void) {

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
)(void) {
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
)(void) {
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
)(void) {
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

int subsearch_matches(line_t * line, int j, uint32_t * needle, int ignorecase, int *len) {
	int k = j;
	uint32_t * match = needle;
	if (*match == '^') {
		if (j != 0) return 0;
		match++;
	}
	while (k < line->actual + 1) {
		if (*match == '\0') {
			if (len) *len = k - j;
			return 1;
		}
		if (*match == '$') {
			if (k != line->actual) return 0;
			match++;
			continue;
		}
		if (*match == '.') {
			if (match[1] == '*') {
				int greedy = !(match[2] == '?');
				/* Short-circuit chained .*'s */
				if (match[greedy ? 2 : 3] == '.' && match[greedy ? 3 : 4] == '*') {
					int _len;
					if (subsearch_matches(line, k, &match[greedy ? 2 : 3], ignorecase, &_len)) {
						if (len) *len = _len + k - j;
						return 1;
					}
					return 0;
				}
				int _j = greedy ? line->actual : k;
				int _break = -1;
				int _len = -1;
				if (!match[greedy ? 2 : 3]) {
					_len = greedy ? (line->actual - _j) : 0;
					_break = _j;
				} else {
					while (_j < line->actual + 1 && _j >= k) {
						int len;
						if (subsearch_matches(line, _j, &match[greedy ? 2 : 3], ignorecase, &len)) {
							_break = _j;
							_len = len;
							break;
						}
						_j += (greedy ? -1 : 1);
					}
				}
				if (_break != -1) {
					if (len) *len = (_break - j) + _len;
					return 1;
				}
				return 0;
			} else {
				if (k >= line->actual) return 0;
				match++;
				k++;
				continue;
			}
		}
		if (*match == '\\' && (match[1] == '$' || match[1] == '^' || match[1] == '/' || match[1] == '\\' || match[1] == '.')) {
			match++;
		} else if (*match == '\\' && match[1] == 't') {
			if (line->text[k].codepoint != '\t') break;
			match += 2;
			k++;
			continue;
		}
		if (k == line->actual) break;
		if (!search_matches(*match, line->text[k].codepoint, ignorecase)) break;
		match++;
		k++;
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
		int match_len;
		if (subsearch_matches(line,j,needle,ignorecase,&match_len)) {
			/* Perform replacement */
			for (int i = 0; i < match_len; ++i) {
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
			if (*cmd == '!') {
				system(&cmd[1]); /* Yes we can just do this */
			} else {
				char * const args[] = {"python3","-c",&cmd[1],NULL};
				execvp("python3",args);
			}
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
		int c;
		while ((c = bim_getch(), c != ENTER_KEY && c != LINE_FEED));

		/* Redraw the screen */
		redraw_all();
	}

	/* Done processing command */
	return 0;
}

BIM_PREFIX_COMMAND(bang,"!","Executes shell commands.") {
	(void)argc, (void)argv;
	return _prefix_command_run_script(cmd);
}

BIM_PREFIX_COMMAND(tick,"`","Executes Python commands.") {
	(void)argc, (void)argv;
	return _prefix_command_run_script(cmd);
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

BIM_COMMAND(tabindicator,"tabindicator","Set the tab indicator") {
	if (argc < 2) {
		render_status_message("tabindicator=%s", global_config.tab_indicator);
		return 0;
	}
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
	if (display_width_of_string(argv[1]) != 1) {
		render_error("Can't set '%s' as indicator, must be one cell wide.", argv[1]);
		return 1;
	}
	if (global_config.space_indicator) free(global_config.space_indicator);
	global_config.space_indicator = strdup(argv[1]);
	return 0;
}

BIM_COMMAND(global_sgr,"global.sgr_mouse","Enable SGR mouse escapes") {
	if (argc < 2) {
		render_status_message("global.sgr_mouse=%d", global_config.use_sgr_mouse);
	} else {
		if (global_config.has_terminal) mouse_disable();
		global_config.use_sgr_mouse = !!atoi(argv[1]);
		if (global_config.has_terminal) mouse_enable();
	}
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

BIM_COMMAND(noindent,"noindent","Disable smrat indentation") {
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
			recalculate_syntax(env->lines[i],i);
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

void load_colorscheme_script(const char * name) {
	static char name_copy[512];
	char tmp[1024];
	snprintf(tmp, 1023, "theme:%s", name);
	if (!run_function(tmp)) {
		sprintf(name_copy, "%s", name);
		current_theme = name_copy;
	}
}

BIM_COMMAND(theme,"theme","Set color theme") {
	if (argc < 2) {
		render_status_message("theme=%s", current_theme);
	} else {
		for (struct theme_def * d = themes; themes && d->name; ++d) {
			if (!strcmp(argv[1], d->name)) {
				d->load(d->name);
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
	for (int i = 0; i < env->line_count; ++i) {
		env->lines[i]->istate = -1;
	}
	env->loading = 1;
	for (int i = 0; i < env->line_count; ++i) {
		recalculate_syntax(env->lines[i],i);
	}
	env->loading = 0;
	redraw_all();
	return 0;
}

BIM_COMMAND(tabs,"tabs","Use tabs for indentation") {
	env->tabs = 1;
	redraw_statusbar();
	return 0;
}

BIM_COMMAND(spaces,"spaces","Use spaces for indentation") {
	env->tabs = 0;
	redraw_statusbar();
	return 0;
}

BIM_COMMAND(tabstop,"tabstop","Show or set the tabstop (width of an indentation unit)") {
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
				recalculate_syntax(env->lines[i],i);
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
	while ((c = bim_getkey(200)) == KEY_TIMEOUT);
	render_commandline_message("%d = %s", c, name_from_key(c));
	return 0;
}

/**
 * Process a user command.
 */
int process_command(char * cmd) {

	if (*cmd == '#') return 0;

	/* First, check prefix commands */
	for (struct command_def * c = prefix_commands; prefix_commands && c->name; ++c) {
		if (strstr(cmd, c->name) == cmd &&
		    (!isalpha(cmd[strlen(c->name)]) || !isalpha(cmd[0]))) {
			return c->command(cmd, 0, NULL);
		}
	}

	char * argv[3] = {NULL, NULL, NULL};
	int argc = !!(*cmd);
	char cmd_name[512] = {0};
	for (char * c = cmd; *c; ++c) {
		if (c-cmd == 511) break;
		if (*c == ' ') {
			cmd_name[c-cmd] = '\0';
			argv[1] = c+1;
			if (*argv[1]) argc++;
			break;
		}
		cmd_name[c-cmd] = *c;
	}

	argv[0] = cmd_name;
	argv[argc] = NULL;

	if (argc < 1) {
		/* no op */
		return 0;
	}

	/* Now check regular commands */
	for (struct command_def * c = regular_commands; regular_commands && c->name; ++c) {
		if (!strcmp(argv[0], c->name)) {
			return c->command(cmd, argc, argv);
		}
	}

	global_config.break_from_selection = 1;

	if (argv[0][0] == '-' && isdigit(argv[0][1])) {
		goto_line(env->line_no-atoi(&argv[0][1]));
		return 0;
	} else if (argv[0][0] == '+' && isdigit(argv[0][1])) {
		goto_line(env->line_no+atoi(&argv[0][1]));
		return 0;
	} else if (isdigit(*argv[0])) {
		goto_line(atoi(argv[0]));
		return 0;
	} else {
		render_error("Not an editor command: %s", argv[0]);
		return 1;
	}
}

/**
 * Wrap strcmp for use with qsort.
 */
int compare_str(const void * a, const void * b) {
	return strcmp(*(const char **)a, *(const char **)b);
}

/**
 * List of file extensions to ignore when tab completing.
 */
const char * tab_complete_ignore[] = {".o",NULL};

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
		args[arg] = start;
		break;
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

	int _candidates_are_files = 0;

	if (arg == 0 || (arg == 1 && !strcmp(args[0], "help"))) {
		/* Complete command names */
		for (struct command_def * c = regular_commands; regular_commands && c->name; ++c) {
			add_candidate(c->name);
		}
		for (struct command_def * c = prefix_commands; prefix_commands && c->name; ++c) {
			add_candidate(c->name);
		}

		goto _accept_candidate;
	}

	if (arg == 1 && !strcmp(args[0], "syntax")) {
		/* Complete syntax options */
		add_candidate("none");
		for (struct syntax_definition * s = syntaxes; syntaxes && s->name; ++s) {
			add_candidate(s->name);
		}
		goto _accept_candidate;
	}

	if (arg == 1 && (!strcmp(args[0], "theme") || !strcmp(args[0], "colorscheme"))) {
		/* Complete color theme names */
		for (struct theme_def * s = themes; themes && s->name; ++s) {
			add_candidate(s->name);
		}
		goto _accept_candidate;
	}

	if (arg == 1 && (!strcmp(args[0], "setcolor"))) {
		for (struct ColorName * c = color_names; c->name; ++c) {
			add_candidate(c->name);
		}
		goto _accept_candidate;
	}

	if (arg == 1 && (!strcmp(args[0], "action"))) {
		for (struct action_def * a = mappable_actions; a->name; ++a) {
			add_candidate(a->name);
		}
		goto _accept_candidate;
	}

	if (arg == 1 && (!strcmp(args[0], "call") || !strcmp(args[0], "trycall") || !strcmp(args[0], "showfunction"))) {
		for (int i = 0; i < flex_user_functions_count; ++i) {
			add_candidate(user_functions[i]->command);
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
				args[arg] = start;
				i = 0;
				if (arg == 32) {
					arg = 31;
					break;
				}
			}
		}

		if (arg == 1) {
			for (struct mode_names * m = mode_names; m->name; ++m) {
				add_candidate(m->name);
			}
		} else if (arg == 2) {
			for (unsigned int i = 0;  i < sizeof(KeyNames)/sizeof(KeyNames[0]); ++i) {
				add_candidate(KeyNames[i].name);
			}
		} else if (arg == 3) {
			for (struct action_def * a = mappable_actions; a->name; ++a) {
				add_candidate(a->name);
			}
			add_candidate("none");
		} else if (arg == 4) {
			for (char * c = "racnwmb"; *c; ++c) {
				char tmp[] = {*c,'\0'};
				add_candidate(tmp);
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

				int skip = 0;
				for (const char ** c = tab_complete_ignore; *c; ++c) {
					if (str_ends_with(s, *c)) {
						skip = 1;
						break;
					}
				}
				if (!skip) {
					add_candidate(s);
				}
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
			char * printed_candidate = candidates[i];
			if (_candidates_are_files) {
				for (char * c = printed_candidate; *c; ++c) {
					if (c[0] == '/' && c[1] != '\0') {
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
			strcat(tmp, printed_candidate);
			offset += strlen(printed_candidate);
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
 * Macros for use in command mode.
 */
#define _syn_command() do { env->syntax = global_config.command_syn; } while (0)
#define _syn_restore() do { env->syntax = global_config.command_syn_back; } while (0)

#define _restore_history(point) do { \
	unsigned char * t = command_history[point]; \
	global_config.command_col_no = 1; \
	global_config.command_buffer->actual = 0; \
	_syn_command(); \
	uint32_t state = 0; \
	uint32_t c = 0; \
	while (*t) { \
		if (!decode(&state, &c, *t)) { \
			char_t _c = {codepoint_width(c), 0, c}; \
			global_config.command_buffer = line_insert(global_config.command_buffer, _c, global_config.command_col_no - 1, -1); \
			global_config.command_col_no++; \
		} else if (state == UTF8_REJECT) state = 0; \
		t++; \
	} \
	_syn_restore(); \
} while (0)

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
)(void) {
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
)(void) {
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

BIM_ACTION(command_accept, 0,
	"Accept the command input and run the requested command."
)(void) {
	/* Convert command buffer to UTF-8 char-array string */
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

	/* Free the original editing buffer */
	free(global_config.command_buffer);
	global_config.command_buffer = NULL;

	/* Run the converted command */
	global_config.break_from_selection = 0;
	insert_command_history(tmp);
	process_command(tmp);
	free(tmp);

	if (!global_config.break_from_selection && env->mode != MODE_DIRECTORY_BROWSE) {
		if (env->mode == MODE_LINE_SELECTION || env->mode == MODE_CHAR_SELECTION || env->mode == MODE_COL_SELECTION) {
			recalculate_selected_lines();
		}
		env->mode = MODE_NORMAL;
	}

	/* Leave command mode */
	global_config.overlay_mode = OVERLAY_MODE_NONE;
}

BIM_ACTION(command_word_delete, 0,
	"Delete the previous word from the input buffer."
)(void) {
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
)(void) {
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
)(void) {
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

BIM_ACTION(command_scroll_history, ARG_IS_CUSTOM,
	"Scroll through command input history."
)(int direction) {
	if (direction == -1) {
		if (command_history[global_config.history_point+1]) {
			_restore_history(global_config.history_point+1);
			global_config.history_point++;
		}
	} else {
		if (global_config.history_point > 0) {
			global_config.history_point--;
			_restore_history(global_config.history_point);
		} else {
			global_config.history_point = -1;
			global_config.command_col_no = 1;
			global_config.command_buffer->actual = 0;
		}
	}
}

BIM_ACTION(command_word_left, 0,
	"Move to the start of the previous word in the input buffer."
)(void) {
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
)(void) {
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
)(void) {
	if (global_config.command_col_no > 1) global_config.command_col_no--;
}

BIM_ACTION(command_cursor_right, 0,
	"Move the cursor one character right in the input buffer."
)(void) {
	if (global_config.command_col_no < global_config.command_buffer->actual+1) global_config.command_col_no++;
}

BIM_ACTION(command_cursor_home, 0,
	"Move the cursor to the start of the input buffer."
)(void) {
	global_config.command_col_no = 1;
}

BIM_ACTION(command_cursor_end, 0,
	"Move the cursor to the end of the input buffer."
)(void) {
	global_config.command_col_no = global_config.command_buffer->actual + 1;
}

BIM_ACTION(eat_mouse, 0,
	"(temporary) Read, but ignore mouse input."
)(void) {
	bim_getch();
	bim_getch();
	bim_getch();
}

BIM_ACTION(command_insert_char, ARG_IS_INPUT,
	"Insert one character into the input buffer."
)(int c) {
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
	int ignorecase = smart_case(global_config.search);
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
	int line = -1, col = -1, _line = 1, _col = 1;
	do {
		int matchlen;
		find_match(_line, _col, &line, &col, buffer, &matchlen);
		if (line != -1) {
			line_t * l = env->lines[line-1];
			for (int i = col; matchlen > 0; ++i, --matchlen) {
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

BIM_ACTION(enter_search, ARG_IS_CUSTOM,
	"Enter search mode."
)(int direction) {
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
)(void) {
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

_finish:
	/* Free the original editing buffer */
	free(global_config.command_buffer);
	global_config.command_buffer = NULL;

	/* Leave command mode */
	global_config.overlay_mode = OVERLAY_MODE_NONE;
}

/**
 * Find the next search result, or loop back around if at the end.
 */
BIM_ACTION(search_next, 0,
	"Jump to the next search match."
)(void) {
	if (!global_config.search) return;
	if (env->coffset) env->coffset = 0;
	int line = -1, col = -1;
	find_match(env->line_no, env->col_no+1, &line, &col, global_config.search, NULL);

	if (line == -1) {
		if (!global_config.search_wraps) return;
		find_match(1,1, &line, &col, global_config.search, NULL);
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
BIM_ACTION(search_prev, 0,
	"Jump to the preceding search match."
)(void) {
	if (!global_config.search) return;
	if (env->coffset) env->coffset = 0;
	int line = -1, col = -1;
	find_match_backwards(env->line_no, env->col_no-1, &line, &col, global_config.search);

	if (line == -1) {
		if (!global_config.search_wraps) return;
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
BIM_ACTION(use_left_buffer, 0,
	"Switch to the left split view."
)(void) {
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
)(void) {
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
)(void) {
	int buttons = bim_getch() - 32;
	int x = bim_getch() - 32;
	int y = bim_getch() - 32;

	handle_common_mouse(buttons, x, y);
}

BIM_ACTION(handle_mouse_sgr, 0,
	"Process SGR-style mouse actions."
)(void) {
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
)(unsigned int c) {
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
)(unsigned int c) {
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
)(void) {
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

BIM_ACTION(redo_history, ACTION_IS_RW,
	"Redo history until the next breakpoint."
)(void) {
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

BIM_ACTION(word_left, 0,
	"Move the cursor left to the previous word."
)(void) {
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
)(void) {
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
)(void) {
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
)(void) {
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
)(void) {
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
)(void) {
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
)(void) {
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
)(void) {
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
)(int direction) {
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
)(void) {
	if (global_config.search) free(global_config.search);
	global_config.search = get_word_under_cursor();

	/* Find it */
	search_next();
}

BIM_ACTION(find_character_forward, ARG_IS_PROMPT | ARG_IS_INPUT,
	"Find a character forward on the current line and place the cursor on (`f`) or before (`t`) it."
)(int type, int c) {
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
)(int type, int c) {
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
			recalculate_syntax(env->lines[(line)-1],(line)-1); \
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
			recalculate_syntax(env->lines[(line)-1],(line)-1); \
		} else { \
			if ((line) == env->start_line || (line) == env->line_no) { \
				recalculate_syntax(env->lines[(line)-1],(line)-1); \
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
)(int direction) {
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

void recalculate_selected_lines(void) {
	int start = env->line_no < env->start_line ? env->line_no : env->start_line;
	int end = env->line_no > env->start_line ? env->line_no : env->start_line;
	if (start < 1) start = 1;
	if (start > env->line_count) start = env->line_count;
	if (end < 1) end = 1;
	if (end > env->line_count) end = env->line_count;
	for (int i = (start > 1) ? (start-1) : (start); i <= end; ++i) {
		recalculate_syntax(env->lines[i-1],i-1);
	}
	redraw_all();
}

BIM_ACTION(enter_line_selection, 0,
	"Enter line selection mode."
)(void) {
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
)(int mode) {
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
)(void) {
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
)(void) {
	env->mode = MODE_INSERT;
	set_history_break();
}

BIM_ACTION(delete_lines_and_enter_insert, ACTION_IS_RW,
	"Delete and yank the selected lines and then enter insert mode."
)(void) {
	delete_and_yank_lines();
	env->lines = add_line(env->lines, env->line_no-1);
	redraw_text();
	env->mode = MODE_INSERT;
}

BIM_ACTION(replace_chars_in_line, ARG_IS_PROMPT | ACTION_IS_RW,
	"Replace characters in the selected lines."
)(int c) {
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
)(void) {
	set_history_break();
	env->mode = MODE_NORMAL;
	recalculate_selected_lines();
}

BIM_ACTION(insert_char_at_column, ARG_IS_INPUT | ACTION_IS_RW,
	"Insert a character on all lines at the current column."
)(int c) {
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
}

BIM_ACTION(enter_col_insert, ACTION_IS_RW,
	"Enter column insert mode."
)(void) {
	if (env->start_line < env->line_no) {
		/* swap */
		int tmp = env->line_no;
		env->line_no = env->start_line;
		env->start_line = tmp;
	}
	env->mode = MODE_COL_INSERT;
}

BIM_ACTION(enter_col_insert_after, ACTION_IS_RW,
	"Enter column insert mode after the selected column."
)(void) {
	env->sel_col += 1;
	enter_col_insert();
}

BIM_ACTION(delete_column, ACTION_IS_RW,
	"(temporary) Delete the selected column."
)(void) {
	/* TODO maybe a flag to do this so we can just call delete_at_column with arg = 1? */
	if (env->start_line < env->line_no) {
		int tmp = env->line_no;
		env->line_no = env->start_line;
		env->start_line = tmp;
	}
	delete_at_column(1);
}

BIM_ACTION(enter_col_selection, 0,
	"Enter column selection mode."
)(void) {
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
)(void) {
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
)(void) {
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
)(void) {
	delete_and_yank_chars();
	redraw_text();
	enter_insert();
}

BIM_ACTION(replace_chars, ARG_IS_PROMPT | ACTION_IS_RW,
	"Replace the selected characters."
)(int c) {
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
)(void) {
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
)(void) {
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

	/* TODO: Get these from syntax files with a dynamic callback */
	if (env->syntax && env->syntax->completion_matcher) {
		env->syntax->completion_matcher(comp,matches,matches_count,complete_match,matches_len);
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
	if (read_tags(tmp, &matches, &matches_count, 0)) goto _completion_done;

	/* Draw box with matches at cursor-width(tmp) */
	if (quit_quietly_on_none && matches_count == 0) return 0;
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
 * Set the search string from a UTF-8 sequence.
 * Since the search string is normally a series of codepoints, this saves
 * some effort when trying to search for things we pulled from the outside world.
 * (eg., ctags search terms)
 */
void set_search_from_bytes(char * bytes) {
	if (global_config.search) free(global_config.search);
	global_config.search = malloc(sizeof(uint32_t) * (strlen(bytes) + 1));
	uint32_t * s = global_config.search;
	char * tmp = bytes;
	uint32_t c, istate = 0;
	while (*tmp) {
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

BIM_ACTION(goto_definition, 0,
	"Jump to the definition of the word under under cursor."
)(void) {
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

#define _perform_correct_search(i) do { \
		if (matches[i].search[0] == '/') { \
			set_search_from_bytes(&matches[i].search[1]); \
			search_next(); \
		} else { \
			goto_line(atoi(matches[i].search)); \
		} \
	} while (0)

	if (env->file_name && !strcmp(matches[0].file, env->file_name)) {
		_perform_correct_search(0);
	} else {
		/* Check if there were other matches that are in this file */
		for (int i =1; env->file_name && i < matches_count; ++i) {
			if (!strcmp(matches[i].file, env->file_name)) {
				_perform_correct_search(i);
				goto _done;
			}
		}
		/* Check buffers */
		for (int i = 0; i < buffers_len; ++i) {
			if (buffers[i]->file_name && !strcmp(matches[0].file,buffers[i]->file_name)) {
				if (left_buffer && buffers[i] != left_buffer && buffers[i] != right_buffer) unsplit();
				env = buffers[i];
				redraw_tabbar();
				_perform_correct_search(i);
				goto _done;
			}
		}
		/* Okay, let's try opening */
		buffer_t * old_buf = env;
		open_file(matches[0].file);
		if (env != old_buf) {
			_perform_correct_search(0);
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
	while ((c = bim_getkey(200))) {
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
)(void) {
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
)(void) {
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
)(void) {
	set_history_break();
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
)(void) {
	if (env->col_no < env->lines[env->line_no-1]->actual + 1) {
		env->col_no += 1;
	}
	enter_insert();
}

BIM_ACTION(delete_forward, ACTION_IS_RW,
	"Delete the character under the cursor."
)(void) {
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
)(void) {
	set_history_break();
	delete_forward();
	env->mode = MODE_INSERT;
}

BIM_ACTION(paste, ARG_IS_CUSTOM | ACTION_IS_RW,
	"Paste yanked text before (`P`) or after (`p`) the cursor."
)(int direction) {
	if (global_config.yanks) {
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
		/* Recalculate whole document syntax */
		for (int i = 0; i < env->line_count; ++i) {
			env->lines[i]->istate = 0;
		}
		for (int i = 0; i < env->line_count; ++i) {
			recalculate_syntax(env->lines[i],i);
		}
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
)(void) {
	env->col_no = env->lines[env->line_no-1]->actual+1;
	env->mode = MODE_INSERT;
	set_history_break();
}

BIM_ACTION(enter_replace, ACTION_IS_RW,
	"Enter replace mode."
)(void) {
	env->mode = MODE_REPLACE;
	set_history_break();
}

BIM_ACTION(toggle_numbers, 0,
	"Toggle the display of line numbers."
)(void) {
	env->numbers = !env->numbers;
	redraw_all();
	place_cursor_actual();
}

BIM_ACTION(toggle_gutter, 0,
	"Toggle the display of the revision status gutter."
)(void) {
	env->gutter = !env->gutter;
	redraw_all();
	place_cursor_actual();
}

BIM_ACTION(toggle_indent, 0,
	"Toggle smart indentation."
)(void) {
	env->indent = !env->indent;
	redraw_statusbar();
	place_cursor_actual();
}

BIM_ACTION(toggle_smartcomplete, 0,
	"Toggle smart completion."
)(void) {
	global_config.smart_complete = !global_config.smart_complete;
	redraw_statusbar();
	place_cursor_actual();
}

BIM_ACTION(expand_split_right, 0,
	"Move the view split divider to the right."
)(void) {
	global_config.split_percent += 1;
	update_split_size();
	redraw_all();
}

BIM_ACTION(expand_split_left, 0,
	"Move the view split divider to the left."
)(void) {
	global_config.split_percent -= 1;
	update_split_size();
	redraw_all();
}

BIM_ACTION(go_page_up, 0,
	"Jump up a screenful."
)(void) {
	goto_line(env->line_no - (global_config.term_height - 6));
}

BIM_ACTION(go_page_down, 0,
	"Jump down a screenful."
)(void) {
	goto_line(env->line_no + (global_config.term_height - 6));
}

BIM_ACTION(jump_to_matching_bracket, 0,
	"Find and jump to the matching bracket for the character under the cursor."
)(void) {
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
)(void) {
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
)(void) {
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
)(void) {
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
)(void) {
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
)(void) {
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
	"(temporary) Perform smart symbol competion from ctags."
)(void) {
	/* This should probably be a submode */
	while (omni_complete(0) == 1);
}

BIM_ACTION(smart_tab, ACTION_IS_RW,
	"Insert a tab or spaces depending on indent mode. (Use ^V <tab> to guarantee a literal tab)"
)(void) {
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
)(int c) {
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
)(int c) {
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
)(void) {
	enter_line_selection();
	cursor_up();
}

BIM_ACTION(enter_line_selection_and_cursor_down, 0,
	"Enter line selection and move the cursor down one line."
)(void) {
	enter_line_selection();
	cursor_down();
}

BIM_ACTION(shift_horizontally, ARG_IS_CUSTOM,
	"Shift the current line or screen view horiztonally, depending on settings."
)(int amount) {
	env->coffset += amount;
	if (env->coffset < 0) env->coffset = 0;
	redraw_text();
}

static int state_before_paste = 0;
BIM_ACTION(paste_begin, 0, "Begin bracketed paste; disable indentation, completion, etc.")(void) {
	if (global_config.smart_complete) state_before_paste |= 0x01;
	if (env->indent) state_before_paste |= 0x02;

	global_config.smart_complete = 0;
	env->indent = 0;
	/* TODO: We need env->loading == 1, but with history (manual breaks, though) */
}

BIM_ACTION(paste_end, 0, "End bracketed paste; restore indentation, completion, etc.")(void) {
	if (state_before_paste & 0x01) global_config.smart_complete = 1;
	if (state_before_paste & 0x02) env->indent = 1;
	redraw_statusbar();
}

struct action_map _NORMAL_MAP[] = {
	{KEY_BACKSPACE, cursor_left_with_wrap, opt_rep, 0},
	{'V',           enter_line_selection, 0, 0},
	{'v',           enter_char_selection, 0, 0},
	{KEY_CTRL_V,    enter_col_selection, 0, 0},
	{'O',           prepend_and_insert, opt_rw, 0},
	{'o',           append_and_insert, opt_rw, 0},
	{'a',           insert_after_cursor, opt_rw, 0},
	{'s',           delete_forward_and_insert, opt_rw, 0},
	{'x',           delete_forward, opt_rep | opt_rw, 0},
	{'P',           paste, opt_arg | opt_rw, -1},
	{'p',           paste, opt_arg | opt_rw, 1},
	{'r',           replace_char, opt_char | opt_rw, 0},
	{'A',           insert_at_end, opt_rw, 0},
	{'u',           undo_history, opt_rw, 0},
	{KEY_CTRL_R,    redo_history, opt_rw, 0},
	{KEY_CTRL_L,    redraw_all, 0, 0},
	{KEY_CTRL_G,    goto_definition, 0, 0},
	{'i',           enter_insert, opt_rw, 0},
	{'R',           enter_replace, opt_rw, 0},
	{KEY_SHIFT_UP,   enter_line_selection_and_cursor_up, 0, 0},
	{KEY_SHIFT_DOWN, enter_line_selection_and_cursor_down, 0, 0},
	{KEY_ALT_UP,    previous_tab, 0, 0},
	{KEY_ALT_DOWN,  next_tab, 0, 0},
	{-1, NULL, 0, 0},
};

struct action_map _INSERT_MAP[] = {
	{KEY_ESCAPE,    leave_insert, 0, 0},
	{KEY_DELETE,    delete_forward, 0, 0},
	{KEY_CTRL_C,    leave_insert, 0, 0},
	{KEY_BACKSPACE, smart_backspace, 0, 0},
	{KEY_ENTER,     insert_line_feed, 0, 0},
	{KEY_CTRL_O,    perform_omni_completion, 0, 0},
	{KEY_CTRL_V,    insert_char, opt_byte, 0},
	{KEY_CTRL_W,    delete_word, 0, 0},
	{'\t',          smart_tab, 0, 0},
	{'/',           smart_comment_end, opt_arg, '/'},
	{'}',           smart_brace_end, opt_arg, '}'},
	{KEY_PASTE_BEGIN, paste_begin, 0, 0},
	{KEY_PASTE_END, paste_end, 0, 0},
	{-1, NULL, 0, 0},
};

struct action_map _REPLACE_MAP[] = {
	{KEY_ESCAPE,    leave_insert, 0, 0},
	{KEY_DELETE,    delete_forward, 0, 0},
	{KEY_BACKSPACE, cursor_left_with_wrap, 0, 0},
	{KEY_ENTER,     insert_line_feed, 0, 0},
	{-1, NULL, 0, 0},
};

struct action_map _LINE_SELECTION_MAP[] = {
	{KEY_ESCAPE,    leave_selection, 0, 0},
	{KEY_CTRL_C,    leave_selection, 0, 0},
	{'V',           leave_selection, 0, 0},
	{'v',           switch_selection_mode, opt_arg, MODE_CHAR_SELECTION},
	{'y',           yank_lines, opt_norm, 0},
	{KEY_BACKSPACE, cursor_left_with_wrap, 0, 0},
	{'\t',          adjust_indent, opt_arg | opt_rw, 1},
	{KEY_SHIFT_TAB, adjust_indent, opt_arg | opt_rw, -1},
	{'D',           delete_and_yank_lines, opt_rw | opt_norm, 0},
	{'d',           delete_and_yank_lines, opt_rw | opt_norm, 0},
	{'x',           delete_and_yank_lines, opt_rw | opt_norm, 0},
	{'s',           delete_lines_and_enter_insert, opt_rw, 0},
	{'r',           replace_chars_in_line, opt_char | opt_rw, 0},

	{KEY_SHIFT_UP,   cursor_up, 0, 0},
	{KEY_SHIFT_DOWN, cursor_down, 0, 0},
	{-1, NULL, 0, 0},
};

struct action_map _CHAR_SELECTION_MAP[] = {
	{KEY_ESCAPE,    leave_selection, 0, 0},
	{KEY_CTRL_C,    leave_selection, 0, 0},
	{'v',           leave_selection, 0, 0},
	{'V',           switch_selection_mode, opt_arg, MODE_LINE_SELECTION},
	{'y',           yank_characters, opt_norm, 0},
	{KEY_BACKSPACE, cursor_left_with_wrap, 0, 0},
	{'D',           delete_and_yank_chars, opt_rw | opt_norm, 0},
	{'d',           delete_and_yank_chars, opt_rw | opt_norm, 0},
	{'x',           delete_and_yank_chars, opt_rw | opt_norm, 0},
	{'s',           delete_chars_and_enter_insert, opt_rw, 0},
	{'r',           replace_chars, opt_char | opt_rw, 0},
	{'A',           insert_at_end_of_selection, opt_rw, 0},
	{-1, NULL, 0, 0},
};

struct action_map _COL_SELECTION_MAP[] = {
	{KEY_ESCAPE,    leave_selection, 0, 0},
	{KEY_CTRL_C,    leave_selection, 0, 0},
	{KEY_CTRL_V,    leave_selection, 0, 0},
	{'I',           enter_col_insert, opt_rw, 0},
	{'a',           enter_col_insert_after, opt_rw, 0},
	{'d',           delete_column, opt_norm | opt_rw, 0},
	{-1, NULL, 0, 0},
};

struct action_map _COL_INSERT_MAP[] = {
	{KEY_ESCAPE,    leave_selection, 0, 0},
	{KEY_CTRL_C,    leave_selection, 0, 0},
	{KEY_BACKSPACE, delete_at_column, opt_arg, -1},
	{KEY_DELETE,    delete_at_column, opt_arg, 1},
	{KEY_ENTER,     NULL, 0, 0},
	{KEY_CTRL_W,    NULL, 0, 0},
	{KEY_CTRL_V,    insert_char_at_column, opt_char, 0},
	{-1, NULL, 0, 0},
};

struct action_map _NAVIGATION_MAP[] = {
	/* Common navigation */
	{KEY_CTRL_B,    go_page_up, opt_rep, 0},
	{KEY_CTRL_F,    go_page_down, opt_rep, 0},
	{':',           enter_command, 0, 0},
	{'/',           enter_search, opt_arg, 1},
	{'?',           enter_search, opt_arg, 0},
	{'n',           search_next, opt_rep, 0},
	{'N',           search_prev, opt_rep, 0},
	{'j',           cursor_down, opt_rep, 0},
	{'k',           cursor_up, opt_rep, 0},
	{'h',           cursor_left, opt_rep, 0},
	{'l',           cursor_right, opt_rep, 0},
	{'b',           word_left, opt_rep, 0},
	{'w',           word_right, opt_rep, 0},
	{'B',           big_word_left, opt_rep, 0},
	{'W',           big_word_right, opt_rep, 0},

	{'<',           shift_horizontally, opt_arg, -1},
	{'>',           shift_horizontally, opt_arg, 1},

	{'f',           find_character_forward, opt_rep | opt_arg | opt_char, 'f'},
	{'F',           find_character_backward, opt_rep | opt_arg | opt_char, 'F'},
	{'t',           find_character_forward, opt_rep | opt_arg | opt_char, 't'},
	{'T',           find_character_backward, opt_rep | opt_arg | opt_char, 'T'},

	{'G',           goto_line, opt_nav, 0},
	{'*',           search_under_cursor, 0, 0},
	{' ',           go_page_down, opt_rep, 0},
	{'%',           jump_to_matching_bracket, 0, 0},
	{'{',           jump_to_previous_blank, opt_rep, 0},
	{'}',           jump_to_next_blank, opt_rep, 0},
	{'$',           cursor_end, 0, 0},
	{'|',           cursor_home, 0, 0},
	{KEY_ENTER,     next_line_non_whitespace, opt_rep, 0},
	{'^',           first_non_whitespace, 0, 0},
	{'0',           cursor_home, 0, 0},

	{-1, NULL, 0, 0},
};

struct action_map _ESCAPE_MAP[] = {
	{KEY_F1,        toggle_numbers, 0, 0},
	{KEY_F2,        toggle_indent, 0, 0},
	{KEY_F3,        toggle_gutter, 0, 0},
	{KEY_F4,        toggle_smartcomplete, 0, 0},
	{KEY_MOUSE,     handle_mouse, 0, 0},
	{KEY_MOUSE_SGR, handle_mouse_sgr, 0, 0},

	{KEY_UP,        cursor_up, opt_rep, 0},
	{KEY_DOWN,      cursor_down, opt_rep, 0},

	{KEY_RIGHT,     cursor_right, opt_rep, 0},
	{KEY_CTRL_RIGHT, big_word_right, opt_rep, 0},
	{KEY_SHIFT_RIGHT, word_right, opt_rep, 0},
	{KEY_ALT_RIGHT, expand_split_right, opt_rep, 0},
	{KEY_ALT_SHIFT_RIGHT, use_right_buffer, opt_rep, 0},

	{KEY_LEFT,      cursor_left, opt_rep, 0},
	{KEY_CTRL_LEFT, big_word_left, opt_rep, 0},
	{KEY_SHIFT_LEFT, word_left, opt_rep, 0},
	{KEY_ALT_LEFT, expand_split_left, opt_rep, 0},
	{KEY_ALT_SHIFT_LEFT, use_left_buffer, opt_rep, 0},

	{KEY_HOME, cursor_home, 0, 0},
	{KEY_END, cursor_end, 0, 0},
	{KEY_PAGE_UP, go_page_up, opt_rep, 0},
	{KEY_PAGE_DOWN, go_page_down, opt_rep, 0},

	{KEY_CTRL_Z,   suspend, 0, 0},

	{-1, NULL, 0, 0}
};

struct action_map _COMMAND_MAP[] = {
	{KEY_ENTER,     command_accept, 0, 0},
	{'\t',          command_tab_complete_buffer, 0, 0},
	{KEY_UP,        command_scroll_history, opt_arg, -1}, /* back */
	{KEY_DOWN,      command_scroll_history, opt_arg, 1}, /* forward */

	{-1, NULL, 0, 0}
};

struct action_map _SEARCH_MAP[] = {
	{KEY_ENTER,    search_accept, 0, 0},

	{KEY_UP,       NULL, 0, 0},
	{KEY_DOWN,     NULL, 0, 0},

	{-1, NULL, 0, 0}
};

struct action_map _INPUT_BUFFER_MAP[] = {
	/* These are generic and shared with search */
	{KEY_ESCAPE,    command_discard, 0, 0},
	{KEY_CTRL_C,    command_discard, 0, 0},
	{KEY_BACKSPACE, command_backspace, 0, 0},
	{KEY_CTRL_W,    command_word_delete, 0, 0},
	{KEY_MOUSE,     eat_mouse, 0, 0},
	{KEY_LEFT,      command_cursor_left, 0, 0},
	{KEY_CTRL_LEFT, command_word_left, 0, 0},
	{KEY_RIGHT,     command_cursor_right, 0, 0},
	{KEY_CTRL_RIGHT,command_word_right, 0, 0},
	{KEY_HOME,      command_cursor_home, 0, 0},
	{KEY_END,       command_cursor_end, 0, 0},

	{-1, NULL, 0, 0}
};

/* DIRECTORY_BROWSE_MAP is only to override KEY_ENTER and should not be remapped,
 * so unlike the others it is not going to be redefined as a pointer. */
struct action_map DIRECTORY_BROWSE_MAP[] = {
	{KEY_ENTER,     open_file_from_line, 0, 0},
	{-1, NULL, 0, 0}
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
					map->method(map->arg, c);
				} else if ((map->options & opt_char) || (map->options & opt_byte)) {
					map->method(c);
				} else if (map->options & opt_arg) {
					map->method(map->arg);
				} else if (map->options & opt_nav) {
					if (nav_buffer) {
						map->method(atoi(nav_buf));
						reset_nav_buffer(0);
					} else {
						map->method(-1);
					}
				} else {
					map->method();
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
				int key = bim_getkey(200);
				if (key != KEY_TIMEOUT) {
					refresh = 1;
					if (!handle_action(COMMAND_MAP, key))
						if (!handle_action(INPUT_BUFFER_MAP, key))
							if (key < KEY_ESCAPE) command_insert_char(key);
				}
				continue;
			} else if (global_config.overlay_mode == OVERLAY_MODE_SEARCH) {
				if (refresh) {
					render_command_input_buffer();
					refresh = 0;
				}
				int key = bim_getkey(200);
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
			int key = bim_getkey(200);
			if (handle_nav_buffer(key)) {
				if (!handle_action(NORMAL_MAP, key))
					if (!handle_action(NAVIGATION_MAP, key))
						handle_action(ESCAPE_MAP, key);
			}
			reset_nav_buffer(key);
		} else if (env->mode == MODE_INSERT) {
			place_cursor_actual();
			int key = bim_getkey(refresh ? 10 : 200);
			if (key == KEY_TIMEOUT) {
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
			int key = bim_getkey(200);
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
			int key = bim_getkey(200);
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
			int key = bim_getkey(200);
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
			int key = bim_getkey(200);
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
			int key = bim_getkey(refresh ? 10 : 200);
			if (key == KEY_TIMEOUT) {
				if (refresh) {
					redraw_commandline();
					redraw_text();
				}
				refresh = 0;
			} else if (handle_action(COL_INSERT_MAP, key)) {
				/* pass */
			} else if (key < KEY_ESCAPE) {
				insert_char_at_column(key);
				refresh = 1;
			}
		} if (env->mode == MODE_DIRECTORY_BROWSE) {
			place_cursor_actual();
			int key = bim_getkey(200);
			if (handle_nav_buffer(key)) {
				if (!handle_action(DIRECTORY_BROWSE_MAP, key))
					if (!handle_action(NAVIGATION_MAP, key))
						handle_action(ESCAPE_MAP, key);
			}
			reset_nav_buffer(key);
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

void free_function(struct bim_function * func) {
	do {
		struct bim_function * next = func->next;
		free(func->command);
		free(func);
		func = next;
	} while (func);
}

int run_function(char * name) {
	for (int i = 0; i < flex_user_functions_count; ++i) {
		if (user_functions[i] && !strcmp(user_functions[i]->command, name)) {
			/* Execute function */
			struct bim_function * this = user_functions[i]->next;
			while (this) {
				char * tmp = strdup(this->command);
				int result = process_command(tmp);
				free(tmp);
				if (result != 0) {
					return result;
				}
				this = this->next;
			}
			return 0;
		}
	}
	return -1;
}

int has_function(char * name) {
	for (int i = 0; i < flex_user_functions_count; ++i) {
		if (user_functions[i] && !strcmp(user_functions[i]->command, name)) {
			return 1;
		}
	}
	return 0;
}

BIM_COMMAND(call,"call","Call a function") {
	if (argc < 2) {
		render_error("Expected function name");
		return 1;
	}
	int result = run_function(argv[1]);
	if (result == -1) {
		render_error("Undefined function: %s", argv[1]);
		return 1;
	}
	return result;
}

BIM_COMMAND(try_call,"trycall","Call a function but return quietly if it fails") {
	if (argc < 2) return 0;
	run_function(argv[1]);
	return 0;
}

BIM_COMMAND(list_functions,"listfunctions","List functions") {
	render_commandline_message("");
	for (int i = 0; i < flex_user_functions_count; ++i) {
		render_commandline_message("%s\n", user_functions[i]->command);
	}
	pause_for_key();
	return 0;
}

BIM_COMMAND(show_function,"showfunction","Show the commands in a function") {
	if (argc < 2) return 1;
	struct bim_function * this = NULL;
	for (int i = 0; i < flex_user_functions_count; ++i) {
		if (user_functions[i] && !strcmp(user_functions[i]->command, argv[1])) {
			this = user_functions[i];
			break;
		}
	}
	if (!this) {
		render_error("Not a function: %s", argv[1]);
		return 1;
	}

	/* We really should rewrite this so syntax highlighting takes a highlighter */
	struct syntax_definition * old_syntax = env->syntax;
	env->syntax = find_syntax_calculator("bimcmd");

	int i = 0;

	while (this) {
		/* Turn command into line */
		line_t * tmp = calloc(sizeof(line_t) + sizeof(char_t) * strlen(this->command),1);
		tmp->available = strlen(this->command);

		unsigned char * t = (unsigned char *)this->command;
		uint32_t state = 0;
		uint32_t c = 0;
		int col = 1;
		while (*t) {
			if (!decode(&state, &c, *t)) {
				char_t _c = {codepoint_width(c), 0, c};
				tmp = line_insert(tmp, _c, col - 1, -1);
				col++;
			}
			t++;
		}

		render_commandline_message("");
		render_line(tmp, global_config.term_width - 1, 0, -1);
		printf("\n");
		this = this->next;
		i++;
		if (this && i == global_config.term_height - 3) {
			printf("(function continues)");
			while (bim_getkey(200) == KEY_TIMEOUT);
		}
	}

	/* Restore previous syntax */
	env->syntax = old_syntax;

	pause_for_key();
	return 0;
}

BIM_COMMAND(runscript,"runscript","Run a script file") {
	if (argc < 2) {
		render_error("Expected a script to run");
		return 1;
	}

	/* Run commands */
	FILE * f;
	char * home;
	if (argv[1][0] == '~' && (home = getenv("HOME"))) {
		char * tmp = malloc(strlen(argv[1]) + strlen(home) + 4);
		sprintf(tmp,"%s%s", home, argv[1]+1);
		f = fopen(tmp,"r");
		free(tmp);
	} else {
		f = fopen(argv[1],"r");
	}
	if (!f) {
		render_error("Failed to open script");
		return 1;
	}

	int retval = 0;
	char linebuf[4096];
	int line = 1;
	int was_collecting_function = 0;
	char * function_name = NULL;
	struct bim_function * new_function = NULL;
	struct bim_function * last_function = NULL;

	while (!feof(f)) {
		memset(linebuf, 0, 4096);
		fgets(linebuf, 4095, f);
		/* Remove linefeed */
		char * s = strstr(linebuf, "\n");
		if (s) *s = '\0';

		/* See if this is a special syntax element */
		if (!strncmp(linebuf, "function ", 9)) {
			/* Confirm we have a function name */
			if (was_collecting_function) {
				free_function(new_function);
				render_error("Syntax error on line %d: attempt nest function while already defining function '%s'", line, function_name);
				retval = 1;
				break;
			}
			if (!strlen(linebuf+9)) {
				render_error("Syntax error on line %d: function needs a name", line);
				retval = 1;
				break;
			}
			function_name = strdup(linebuf+9);
			was_collecting_function = 1;
			new_function = malloc(sizeof(struct bim_function));
			new_function->command = strdup(function_name);
			new_function->next = NULL;
			last_function = new_function;
			/* Set up function */
		} else if (!strcmp(linebuf,"end")) {
			if (!was_collecting_function) {
				render_error("Syntax error on line %d: unexpected 'end'", line);
				retval = 1;
				break;
			}
			was_collecting_function = 0;
			/* See if a function with this name is already defined */
			int this = -1;
			for (int i = 0; i < flex_user_functions_count; ++i) {
				if (user_functions[i] && !strcmp(user_functions[i]->command, function_name)) {
					this = i;
					break;
				}
			}
			if (this > -1) {
				free_function(user_functions[this]);
				user_functions[this] = new_function;
			} else {
				add_user_function(new_function);
				if (strstr(function_name,"theme:") == function_name) {
					add_colorscheme((struct theme_def){strdup(function_name+6), load_colorscheme_script});
				}
			}
			free(function_name);
			new_function = NULL;
			last_function = NULL;
			function_name = NULL;
		} else if (was_collecting_function) {
			/* Collect function */
			last_function->next = malloc(sizeof(struct bim_function));
			last_function = last_function->next;
			char * s = linebuf;
			while (*s == ' ') s++;
			last_function->command = strdup(s);
			last_function->next = NULL;
		} else {
			int result = process_command(linebuf);
			if (result != 0) {
				retval = result;
				break;
			}
		}

		line++;
	}

	if (was_collecting_function) {
		free_function(new_function);
		render_error("Syntax error on line %d: unexpected end of file while defining function '%s'", line, function_name);
		retval = 1;
	}

	if (function_name) free(function_name);
	fclose(f);
	return retval;
}

BIM_COMMAND(rundir,"rundir","Run scripts from a directory, in unspecified order") {
	if (argc < 2) return 1;
	char * file = argv[1];
	DIR * dirp = NULL;
	if (file[0] == '~') {
		char * home = getenv("HOME");
		if (home) {
			char * _file = malloc(strlen(file) + strlen(home) + 4); /* Paranoia */
			sprintf(_file, "%s%s", home, file+1);
			dirp = opendir(_file);
			free(_file);
		}
	} else {
		dirp = opendir(file);
	}
	if (!dirp) {
		render_error("Directory is not accessible: %s", argv[1]);
		return 1;
	}
	struct dirent * ent = readdir(dirp);
	while (ent) {
		if (str_ends_with(ent->d_name,".bimscript")) {
			char * tmp = malloc(strlen(file) + 1 + strlen(ent->d_name) + 1);
			snprintf(tmp, strlen(file) + 1 + strlen(ent->d_name) + 1, "%s/%s", file, ent->d_name);
			char * args[] = {"runscript",tmp,NULL};
			bim_command_runscript("runscript", 2, args);
			free(tmp);
		}
		ent = readdir(dirp);
	}
	return 0;
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
	if (stat(tmp, &statbuf)) return;

	char * args[] = {"runscript", tmp, NULL};
	if (bim_command_runscript("runscript", 2, args)) {
		/* Wait */
		render_error("Errors were encountered when loading bimrc. Press ENTER to continue.");
		int c;
		while ((c = bim_getch(), c != ENTER_KEY && c != LINE_FEED));
	}
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
	if (term && strstr(term,"xterm-256color") == term) {
		global_config.can_insert = 1;
		global_config.can_bracketedpaste = 1;
		char * term_emu = getenv("TERMINAL_EMULATOR");
		if (term_emu && strstr(term_emu,"JetBrains")) {
			global_config.can_bce = 0;
		}
	}
	if (term && strstr(term,"toaru") == term) {
		global_config.can_insert = 1;
		global_config.can_bracketedpaste = 1;
	}

	if (!global_config.can_unicode) {
		global_config.tab_indicator = strdup(">");
		global_config.space_indicator = strdup("-");
	} else {
		global_config.tab_indicator = strdup("»");
		global_config.space_indicator = strdup("·");
	}

}

/**
 * Run global initialization tasks
 */
void initialize(void) {
	/* Force empty locale */
	setlocale(LC_ALL, "");

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
	CLONE_MAP(INPUT_BUFFER_MAP);

#undef CLONE_MAP

	/* Detect terminal quirks */
	detect_weird_terminals();

	/* Load bimrc */
	load_bimrc();

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
}

struct action_def * find_action(void (*action)()) {
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

void dump_commands(void) {
	printf("## Regular Commands\n");
	printf("\n");
	printf("| **Command** | **Description** |\n");
	printf("|-------------|-----------------|\n");
	for (struct command_def * c = regular_commands; regular_commands && c->name; ++c) {
		printf("| `:%s` | %s |\n", c->name, c->description);
	}
	printf("\n");
	printf("## Prefix Commands\n");
	printf("\n");
	printf("| **Command** | **Description** |\n");
	printf("|-------------|-----------------|\n");
	for (struct command_def * c = prefix_commands; prefix_commands && c->name; ++c) {
		printf("| `:%s...` | %s |\n", !strcmp(c->name, "`") ? "`(backtick)`" : c->name, c->description);
	}
	printf("\n");
}

BIM_COMMAND(whatis,"whatis","Describe actions bound to a key in different modes.") {
	int key = 0;

	if (argc < 2) {
		render_commandline_message("(press a key)");
		while ((key = bim_getkey(200)) == KEY_TIMEOUT);
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
				struct action_def * action = find_action(m->method);
				render_commandline_message("%s: %s\n", map->name, action ? action->description : "(unmapped)");
				found_something = 1;
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
		char * space = strstr(colorname, " ");
		if (!space) {
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
		char * colorvalue = space + 1;
		*space = '\0';
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
	arg1 = strstr(argv[1]," ");
	if (arg1) {
		*arg1 = '\0';
		arg1++;
		arg2 = strstr(arg1," ");
		if (arg2) {
			*arg2 = '\0';
			arg2++;
			arg3 = strstr(arg1," ");
			if (arg3) {
				*arg3 = '\0';
				arg3++;
			}
		}
	}

	/* Find the action */
	for (int i = 0; i < flex_mappable_actions_count; ++i) {
		if (!strcmp(mappable_actions[i].name, action)) {
			/* Count arguments */
			int args = 0;
			if (mappable_actions[i].options & ARG_IS_CUSTOM) args++;
			if (mappable_actions[i].options & ARG_IS_INPUT) args++;
			if (mappable_actions[i].options & ARG_IS_PROMPT) args++;

			if (args == 0) {
				mappable_actions[i].action();
			} else if (args == 1) {
				if (!arg1) { render_error("Expected one argument"); return 1; }
				mappable_actions[i].action(atoi(arg1));
			} else if (args == 2) {
				if (!arg2) { render_error("Expected two arguments"); return 1; }
				mappable_actions[i].action(atoi(arg1), atoi(arg2));
			} else if (args == 3) {
				if (!arg3) { render_error("Expected three arguments"); return 1; }
				mappable_actions[i].action(atoi(arg1), atoi(arg2), atoi(arg3));
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
					initialize(); /* Need to load bimrc to get themes */
					fprintf(stderr, "bim %s%s - %s\n", BIM_VERSION, BIM_BUILD_DATE, BIM_COPYRIGHT);
					fprintf(stderr, " Available syntax highlighters:");
					for (struct syntax_definition * s = syntaxes; syntaxes && s->name; ++s) {
						fprintf(stderr, " %s", s->name);
					}
					fprintf(stderr, "\n");
					fprintf(stderr, " Available color themes:");
					for (struct theme_def * d = themes; themes && d->name; ++d) {
						fprintf(stderr, " %s", d->name);
					}
					fprintf(stderr, "\n");
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
char * syn_bash_keywords[] = {
	/* Actual bash keywords */
	"if","then","else","elif","fi","case","esac","for","coproc",
	"select","while","until","do","done","in","function","time",
	/* Other keywords */
	"exit","return","source","function","export","alias","complete","shopt","local","eval",
	/* Common Unix utilities */
	"echo","cd","pushd","popd","printf","sed","rm","mv",
	NULL
};

int bash_pop_state(int state) {
	int new_state = state / 100;
	return new_state * 10;
}

int bash_push_state(int state, int new) {
	return state * 10 + new;
}

int bash_paint_tick(struct syntax_state * state, int out_state) {
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

int bash_paint_braced_variable(struct syntax_state * state) {
	while (charat() != -1) {
		if (charat() == '}') {
			paint(1, FLAG_NUMERAL);
			return 0;
		}
		paint(1, FLAG_NUMERAL);
	}
	return 0;
}

int bash_special_variable(int c) {
	return (c == '@' || c == '?');
}

int bash_paint_string(struct syntax_state * state, char terminator, int out_state, int color) {
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

int syn_bash_calculate(struct syntax_state * state) {
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

char * syn_bash_ext[] = {
#ifndef __toaru__
	".sh",
#endif
	".bash",".bashrc",
	NULL
};

BIM_SYNTAX_COMPLETER(bash) {
	for (char ** keyword = syn_bash_keywords; *keyword; ++keyword) {
		add_if_match((*keyword),"(sh keyword)");
	}

	return 0;
}

BIM_SYNTAX_EXT(bash, 0, c_keyword_qualifier)
int cmd_qualifier(int c) { return c != -1 && c != ' '; }

extern int syn_bash_calculate(struct syntax_state * state);
extern int syn_py_calculate(struct syntax_state * state);

static int bimcmd_paint_replacement(struct syntax_state * state) {
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
	return -1;
}

extern struct command_def * regular_commands;
extern struct command_def * prefix_commands;

static int bimcmd_find_commands(struct syntax_state * state) {
	for (struct command_def * c = regular_commands; regular_commands && c->name; ++c) {
		if (match_and_paint(state, c->name, FLAG_KEYWORD, cmd_qualifier)) return 1;
	}
	for (struct command_def * c = prefix_commands; prefix_commands && c->name; ++c) {
		if (match_and_paint(state, c->name, FLAG_KEYWORD, cmd_qualifier)) return 1;
	}
	return 0;
}

static char * bimscript_comments[] = {
	"@author","@version","@url","@description",
	NULL
};

static int bcmd_at_keyword_qualifier(int c) {
	return isalnum(c) || (c == '_') || (c == '@');
}

int syn_bimcmd_calculate(struct syntax_state * state) {
	if (state->i == 0) {
		while (charat() == ' ') skip();
		if (charat() == '#') {
			while (charat() != -1) {
				if (charat() == '@') {
					if (!find_keywords(state, bimscript_comments, FLAG_ESCAPE, bcmd_at_keyword_qualifier)) {
						paint(1, FLAG_COMMENT);
					}
				} else {
					paint(1, FLAG_COMMENT);
				}
			}
			return -1;
		} else if (match_and_paint(state, "function", FLAG_PRAGMA, cmd_qualifier)) {
			while (charat() == ' ') skip();
			while (charat() != -1 && charat() != ' ') paint(1, FLAG_TYPE);
			while (charat() != -1) paint(1, FLAG_ERROR);
			return -1;
		} else if (match_and_paint(state, "end", FLAG_PRAGMA, cmd_qualifier)) {
			while (charat() != -1) paint(1, FLAG_ERROR);
			return -1;
		} else if (match_and_paint(state, "return", FLAG_PRAGMA, cmd_qualifier)) {
			while (charat() == ' ') skip();
			while (charat() != -1 && charat() != ' ') paint(1, FLAG_NUMERAL);
			return -1;
		} else if (match_and_paint(state, "call", FLAG_KEYWORD, cmd_qualifier) ||
			match_and_paint(state, "trycall", FLAG_KEYWORD, cmd_qualifier) ||
			match_and_paint(state, "showfunction", FLAG_KEYWORD, cmd_qualifier)) {
			while (charat() == ' ') skip();
			for (struct bim_function ** f = user_functions; user_functions && *f; ++f) {
				if (match_and_paint(state, (*f)->command, FLAG_TYPE, cmd_qualifier)) break;
			}
		} else if (match_and_paint(state, "theme", FLAG_KEYWORD, cmd_qualifier) ||
			match_and_paint(state, "colorscheme", FLAG_KEYWORD, cmd_qualifier)) {
			while (charat() == ' ') skip();
			for (struct theme_def * s = themes; themes && s->name; ++s) {
				if (match_and_paint(state, s->name, FLAG_TYPE, cmd_qualifier)) break;
			}
		} else if (match_and_paint(state, "syntax", FLAG_KEYWORD, cmd_qualifier)) {
			while (charat() == ' ') skip();
			for (struct syntax_definition * s = syntaxes; syntaxes && s->name; ++s) {
				if (match_and_paint(state, s->name, FLAG_TYPE, cmd_qualifier)) return -1;
			}
			if (match_and_paint(state, "none", FLAG_TYPE, cmd_qualifier)) return -1;
		} else if (match_and_paint(state, "setcolor", FLAG_KEYWORD, cmd_qualifier)) {
			while (charat() == ' ') skip();
			for (struct ColorName * c = color_names; c->name; ++c) {
				if (match_and_paint(state, c->name, FLAG_TYPE, cmd_qualifier)) {
					while (charat() != -1) paint(1, FLAG_STRING);
					return -1;
				}
			}
			return -1;
		} else if (match_and_paint(state, "mapkey", FLAG_KEYWORD, cmd_qualifier)) {
			if (charat() == ' ') skip(); else { paint(1, FLAG_ERROR); return -1; }
			for (struct mode_names * m = mode_names; m->name; ++m) {
				if (match_and_paint(state, m->name, FLAG_TYPE, cmd_qualifier)) break;
			}
			if (charat() == ' ') skip(); else { paint(1, FLAG_ERROR); return -1; }
			while (charat() != ' ' && charat() != -1) skip(); /* key name */
			if (charat() == ' ') skip(); else { paint(1, FLAG_ERROR); return -1; }
			for (struct action_def * a = mappable_actions; a->name; ++a) {
				if (match_and_paint(state, a->name, FLAG_TYPE, cmd_qualifier)) break;
			}
			match_and_paint(state, "none", FLAG_TYPE, cmd_qualifier);
			if (charat() == -1) return -1;
			if (charat() == ' ' && charat() != -1) skip(); else { paint(1, FLAG_ERROR); return -1; }
			while (charat() != -1 && charat() != ' ') {
				if (!strchr("racnwmb",charat())) {
					paint(1, FLAG_ERROR);
				} else {
					skip();
				}
			}
			return -1;
		} else if (match_and_paint(state, "action", FLAG_KEYWORD, cmd_qualifier)) {
			while (charat() == ' ') skip();
			for (struct action_def * a = mappable_actions; a->name; ++a) {
				if (match_and_paint(state, a->name, FLAG_TYPE, cmd_qualifier)) return -1;
			}
		} else if (charat() == '%' && nextchar() == 's') {
			paint(1, FLAG_KEYWORD);
			return bimcmd_paint_replacement(state);
		} else if (charat() == 's' && !isalpha(nextchar())) {
			return bimcmd_paint_replacement(state);
		} else if (bimcmd_find_commands(state)) {
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

char * syn_bimcmd_ext[] = {".bimscript",".bimrc",NULL}; /* no files */

BIM_SYNTAX_COMPLETER(bimcmd) {
	for (struct command_def * c = regular_commands; regular_commands && c->name; ++c) {
		add_if_match(c->name,c->description);
	}
	add_if_match("function","Define a function");
	add_if_match("end","End a function definition");
	return 0;
}

BIM_SYNTAX_EXT(bimcmd, 1, cmd_qualifier)
int syn_biminfo_calculate(struct syntax_state * state) {
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

char * syn_biminfo_ext[] = {".biminfo",NULL};

BIM_SYNTAX(biminfo, 0)
/**
 * Syntax definition for C
 */
char * syn_c_keywords[] = {
	"while","if","for","continue","return","break","switch","case","sizeof",
	"struct","union","typedef","do","default","else","goto",
	"alignas","alignof","offsetof","asm","__asm__",
	/* C++ stuff */
	"public","private","class","using","namespace","virtual","override","protected",
	"template","typename","static_cast","throw",
	NULL
};

char * syn_c_types[] = {
	"static","int","char","short","float","double","void","unsigned","volatile","const",
	"register","long","inline","restrict","enum","auto","extern","bool","complex",
	"uint8_t","uint16_t","uint32_t","uint64_t",
	"int8_t","int16_t","int32_t","int64_t","FILE",
	"ssize_t","size_t","uintptr_t","intptr_t","__volatile__",
	"constexpr",
	NULL
};

char * syn_c_special[] = {
	"NULL",
	"stdin","stdout","stderr",
	"STDIN_FILENO","STDOUT_FILENO","STDERR_FILENO",
	NULL
};

int c_keyword_qualifier(int c) {
	return isalnum(c) || (c == '_');
}

/**
 * Paints a basic C-style quoted string.
 */
void paint_c_string(struct syntax_state * state) {
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
				if (charat() == '\\' || charat() == '"') continue;
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

/**
 * Paint a C character numeral. Can be arbitrarily large, so
 * it supports multibyte chars for things like defining weird
 * ASCII multibyte integer constants.
 */
void paint_c_char(struct syntax_state * state) {
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
 * Paint a classic C comment which continues until terminated.
 * Assumes you've already painted the starting / and *.
 */
int paint_c_comment(struct syntax_state * state) {
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
int paint_c_pragma(struct syntax_state * state) {
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
int paint_c_numeral(struct syntax_state * state) {
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

int syn_c_calculate(struct syntax_state * state) {
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
					if (charat() == ' ' && nextchar() == '0' && charrel(2) == -1) {
						state->i -= 4;
						while (charat() != -1) paint(1, FLAG_COMMENT);
						return 4;
					}
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
		default:
			while (charat() == ' ' || charat() == '\t') paint(1, FLAG_COMMENT);
			if (charat() == '#') {
				paint(1, FLAG_COMMENT);
				while (charat() == ' ' || charat() == '\t') paint(1, FLAG_COMMENT);
				if (match_and_paint(state,"if",FLAG_COMMENT, c_keyword_qualifier)) {
					while (charat() != -1) paint(1, FLAG_COMMENT);
					return state->state + 1;
				} else if (match_and_paint(state, "else", FLAG_COMMENT, c_keyword_qualifier) || match_and_paint(state, "elif", FLAG_COMMENT, c_keyword_qualifier)) {
					while (charat() != -1) paint(1, FLAG_COMMENT);
					return (state->state == 4) ? 0 : (state->state);
				} else if (match_and_paint(state, "endif", FLAG_COMMENT, c_keyword_qualifier)) {
					while (charat() != -1) paint(1, FLAG_COMMENT);
					return (state->state == 4) ? 0 : (state->state - 1);
				} else {
					while (charat() != -1) paint(1, FLAG_COMMENT);
					return (state->state);
				}
			} else {
				while (charat() != -1) paint(1, FLAG_COMMENT);
				return state->state;
			}
			break;
	}
	return -1;
}

char * syn_c_ext[] = {".c",".h",".cpp",".hpp",".c++",".h++",".cc",".hh",NULL};

BIM_SYNTAX_COMPLETER(c) {
	for (char ** keyword = syn_c_keywords; *keyword; ++keyword) {
		add_if_match((*keyword),"(c keyword)");
	}
	for (char ** keyword = syn_c_types; *keyword; ++keyword) {
		add_if_match((*keyword),"(c type)");
	}
	return 0;
}

BIM_SYNTAX_EXT(c, 0, c_keyword_qualifier)
int syn_conf_calculate(struct syntax_state * state) {
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

char * syn_conf_ext[] = {".conf",".ini",".git/config",".cfg",".properties",NULL};

BIM_SYNTAX(conf, 1)
static char * html_elements[] = {
	"a","abbr","address","area","article","aside","audio",
	"b","base","bdi","bdo","blockquote","body","br","button",
	"canvas","cite","code","col","colgroup","data","datalist",
	"dd","del","details","dfn","dialog","div","dl","dt","em",
	"embed","fieldset","figcaption","figure","footer","form",
	"h1","h2","h3","h4","h5","h6","head","header","hr","html",
	"i","iframe","img","input","ins","kbd","label","legend",
	"li","link","main","map","mark","meta","meter","nav",
	"noscript","object","ol","optgroup","option","output",
	"p","param","picture","pre","progress","q","rp","rt",
	"ruby","s","samp","script","section","select","small",
	"source","span","strong","style","sub","summary","sup",
	"svg","table","tbody","td","template","textarea","tfoot",
	"th","thead","time","title","tr","track","u","ul","var",
	"video","wbr","hgroup","*",
	NULL
};

static char * css_properties[] = {
	"align-content","align-items","align-self","all","animation",
	"animation-delay","animation-direction","animation-duration",
	"animation-fill-mode","animation-iteration-count","animation-name",
	"animation-play-state","animation-timing-function","backface-visibility",
	"background","background-attachment","background-blend-mode","background-clip",
	"background-color","background-image","background-origin","background-position",
	"background-repeat","background-size","border","border-bottom","border-bottom-color",
	"border-bottom-left-radius","border-bottom-right-radius","border-bottom-style",
	"border-bottom-width","border-collapse","border-color","border-image","border-image-outset",
	"border-image-repeat","border-image-slice","border-image-source","border-image-width",
	"border-left","border-left-color","border-left-style","border-left-width",
	"border-radius","border-right","border-right-color","border-right-style","border-right-width",
	"border-spacing","border-style","border-top","border-top-color","border-top-left-radius",
	"border-top-right-radius","border-top-style","border-top-width","border-width",
	"bottom","box-decoration-break","box-shadow","box-sizing","break-after",
	"break-before","break-inside","caption-side","caret-color","@charset",
	"clear","clip","color","column-count","column-fill","column-gap","column-rule","column-rule-color",
	"column-rule-style","column-rule-width","column-span","column-width","columns","content",
	"counter-increment","counter-reset","cursor","direction","display","empty-cells",
	"filter","flex","flex-basis","flex-direction","flex-flow","flex-grow","flex-shrink",
	"flex-wrap","float","font","@font-face","font-family","font-feature-settings","@font-feature-values",
	"font-kerning","font-language-override","font-size","font-size-adjust","font-stretch","font-style",
	"font-synthesis","font-variant","font-variant-alternates","font-variant-caps","font-variant-east-asian",
	"font-variant-ligatures","font-variant-numeric","font-variant-position","font-weight",
	"grid","grid-area","grid-auto-columns","grid-auto-flow","grid-auto-rows","grid-column",
	"grid-column-end","grid-column-gap","grid-column-start","grid-gap","grid-row","grid-row-end",
	"grid-row-gap","grid-row-start","grid-template","grid-template-areas","grid-template-columns",
	"grid-template-rows","hanging-punctuation","height","hyphens","image-rendering","@import",
	"isolation","justify-content","@keyframes","left","letter-spacing","line-break","line-height",
	"list-style","list-style-image","list-style-position","list-style-type","margin","margin-bottom",
	"margin-left","margin-right","margin-top","max-height","max-width","@media","min-height",
	"min-width","mix-blend-mode","object-fit","object-position","opacity","order","orphans",
	"outline","outline-color","outline-offset","outline-style","outline-width","overflow",
	"overflow-wrap","overflow-x","overflow-y","padding","padding-bottom","padding-left","padding-right",
	"padding-top","page-break-after","page-break-before","page-break-inside","perspective",
	"perspective-origin","pointer-events","position","quotes","resize","right","scroll-behavior",
	"tab-size","table-layout","text-align","text-align-last","text-combine-upright","text-decoration",
	"text-decoration-color","text-decoration-line","text-decoration-style","text-indent","text-justify",
	"text-orientation","text-overflow","text-shadow","text-transform","text-underline-position",
	"top","transform","transform-origin","transform-style","transition","transition-delay",
	"transition-duration","transition-property","transition-timing-function","unicode-bidi",
	"user-select","vertical-align","visibility","white-space","widows","width","word-break",
	"word-spacing","word-wrap","writing-mode",
	NULL
};

static char * css_values[] = {
	"inline","block","inline-block","none",
	"transparent","thin","dotted","sans-serif",
	"rgb","rgba","bold","italic","underline","context-box",
	"monospace","serif","sans-serif","pre-wrap",
	"relative","baseline","hidden","solid","inherit","normal",
	"button","pointer","border-box","default","textfield",
	"collapse","top","bottom","avoid","table-header-group",
	"middle","absolute","rect","left","center","right",
	"ellipsis","nowrap","table","both","uppercase","lowercase","help",
	"static","table-cell","table-column","scroll","touch","auto",
	"not-allowed","inset","url","fixed","translate","alpha","fixed","device-width",
	"table-row",
	NULL
};

static char * css_states[] = {
	"focus","active","hover","link","visited","before","after",
	"left","right","root","empty","target","enabled","disabled","checked","invalid",
	"first-child","nth-child","not","last-child",
	NULL
};

int css_property_qualifier(int c) {
	return isalnum(c) || c == '-' || c == '@' || c == '*' || c == '!';
}

int match_prefix(struct syntax_state * state, char * prefix) {
	int i = 0;
	while (1) {
		if (prefix[i] == '\0') return 1;
		if (prefix[i] != charrel(i)) return 0;
		if (charrel(i) == -1) return 0;
		i++;
	}
}

int syn_css_calculate(struct syntax_state * state) {
	if (state->state < 1) {
		if (charat() == '/' && nextchar() == '*') {
			if (paint_c_comment(state) == 1) return 1;
		} else if (charat() == '"') {
			paint_simple_string(state);
			return 0;
		} else if (lastchar() != '.' && find_keywords(state,html_elements,FLAG_KEYWORD,css_property_qualifier)) {
			return 0;
		} else if (lastchar() != '.' && find_keywords(state,css_properties,FLAG_TYPE,css_property_qualifier)) {
			return 0;
		} else if (match_prefix(state,"-moz-")) {
			paint(5,FLAG_ESCAPE);
			while (charat() != -1 && css_property_qualifier(charat())) paint(1, FLAG_TYPE);
		} else if (match_prefix(state,"-webkit-")) {
			paint(8,FLAG_ESCAPE);
			while (charat() != -1 && css_property_qualifier(charat())) paint(1, FLAG_TYPE);
		} else if (match_prefix(state,"-ms-")) {
			paint(4,FLAG_ESCAPE);
			while (charat() != -1 && css_property_qualifier(charat())) paint(1, FLAG_TYPE);
		} else if (match_prefix(state,"-o-")) {
			paint(3,FLAG_ESCAPE);
			while (charat() != -1 && css_property_qualifier(charat())) paint(1, FLAG_TYPE);
		} else if (charat() == ':') {
			skip();
			if (find_keywords(state, css_states, FLAG_PRAGMA, css_property_qualifier)) return 0;
			while (charat() != -1 && charat() != ';') {
				if (find_keywords(state, css_values, FLAG_NUMERAL, css_property_qualifier)) {
					continue;
				} else if (charat() == '"') {
					paint_simple_string(state);
					continue;
				} else if (charat() == '{') {
					skip();
					return 0;
				} else if (charat() == '#') {
					paint(1, FLAG_NUMERAL);
					while (isxdigit(charat())) paint(1, FLAG_NUMERAL);
				} else if (isdigit(charat())) {
					while (isdigit(charat())) paint(1, FLAG_NUMERAL);
					if (charat() == '.') {
						paint(1, FLAG_NUMERAL);
						while (isdigit(charat())) paint(1, FLAG_NUMERAL);
					}
					if (charat() == '%') paint(1, FLAG_NUMERAL);
					else if (charat() == 'p' && (nextchar() == 't' || nextchar() == 'x' || nextchar() == 'c')) paint(2, FLAG_NUMERAL);
					else if ((charat() == 'e' || charat() == 'c' || charat() == 'm') && nextchar() == 'm') paint(2, FLAG_NUMERAL);
					else if (charat() == 'e' && nextchar() == 'x') paint(2, FLAG_NUMERAL);
					else if (charat() == 'i' && nextchar() == 'n') paint(2, FLAG_NUMERAL);
					else if (charat() == 'v' && (nextchar() == 'w' || nextchar() == 'h')) paint(2, FLAG_NUMERAL);
					else if (charat() == 'c' && nextchar() == 'h') paint(2, FLAG_NUMERAL);
					else if (charat() == 'r' && nextchar() == 'e' && charrel(2) == 'm') paint(3, FLAG_NUMERAL);
					else if (charat() == 'v' && nextchar() == 'm' && ((charrel(2) == 'i' && charrel(3) == 'n') || (charrel(2) == 'a' && charrel(3) == 'x'))) paint(4, FLAG_NUMERAL);
					else if (charat() == 's') paint(1, FLAG_NUMERAL);
				} else if (match_and_paint(state,"!important",FLAG_PRAGMA,css_property_qualifier)) {
					continue;
				} else if (charat() != -1) {
					skip();
				}
			}
			return 0;
		} else if (charat() == -1) {
			return -1;
		} else {
			skip();
		}
		return 0;
	} else if (state->state == 1) {
		if (paint_c_comment(state) == 1) return 1;
		return 0;
	}
	return -1;
}

char * syn_css_ext[] = {".css",NULL};

BIM_SYNTAX(css, 1)
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

char * syn_ctags_ext[] = { "tags", NULL };

BIM_SYNTAX(ctags, 0)
int syn_diff_calculate(struct syntax_state * state) {
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

char * syn_diff_ext[] = {".patch",".diff",NULL};

BIM_SYNTAX(diff, 1)
int syn_dirent_calculate(struct syntax_state * state) {
	if (state->i == 0) {
		if (charat() == '#') {
			while (charat() != -1) paint(1, FLAG_COMMENT);
		} else if (charat() == 'd') {
			paint(1, FLAG_COMMENT);
			while (charat() != -1) paint(1, FLAG_KEYWORD);
		} else if (charat() == 'f') {
			paint(1, FLAG_COMMENT);
			return -1;
		}
	}
	return -1;
}

char * syn_dirent_ext[] = {NULL};

BIM_SYNTAX(dirent, 1)
int esh_variable_qualifier(int c) {
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (c == '_');
}

int paint_esh_variable(struct syntax_state * state) {
	if (charat() == '{') {
		paint(1, FLAG_TYPE);
		while (charat() != '}' && charat() != -1) paint(1, FLAG_TYPE);
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

int paint_esh_string(struct syntax_state * state) {
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

int paint_esh_single_string(struct syntax_state * state) {
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

int esh_keyword_qualifier(int c) {
	return (isalnum(c) || c == '?' || c == '_' || c == '-'); /* technically anything that isn't a space should qualify... */
}

char * esh_keywords[] = {
	"cd","exit","export","help","history","if","empty?",
	"equals?","return","export-cmd","source","exec","not","while",
	"then","else","echo",
	NULL
};

int syn_esh_calculate(struct syntax_state * state) {
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
char * syn_esh_ext[] = {
#ifdef __toaru__
	".sh",
#endif
	".eshrc",".yutanirc",
	NULL
};

BIM_SYNTAX(esh, 0)
int syn_gitcommit_calculate(struct syntax_state * state) {
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

char * syn_gitcommit_ext[] = {"COMMIT_EDITMSG", NULL};

char * syn_gitrebase_commands[] = {
	"p","r","e","s","f","x","d",
	"pick","reword","edit","squash","fixup",
	"exec","drop",
	NULL
};

int syn_gitrebase_calculate(struct syntax_state * state) {
	if (state->i == 0 && charat() == '#') {
		while (charat() != -1) paint(1, FLAG_COMMENT);
	} else if (state->i == 0 && find_keywords(state, syn_gitrebase_commands, FLAG_KEYWORD, c_keyword_qualifier)) {
		while (charat() == ' ') skip();
		while (isxdigit(charat())) paint(1, FLAG_NUMERAL);
		return -1;
	}

	return -1;
}

char * syn_gitrebase_ext[] = {"git-rebase-todo",NULL};

BIM_SYNTAX(gitcommit, 1)
BIM_SYNTAX(gitrebase, 1)
static char * groovy_keywords[] = {
	"as","assert","break","case",
	"catch","class","const","continue",
	"def","default","do","else","enum",
	"extends","finally","for",
	"goto","if","implements","import",
	"in","instanceof","interface","new",
	"package","return","super",
	"switch","throw","throws",
	"trait","try","while",
	NULL
};

static char * groovy_stuff[] = {
	"true","false","null","this",
	NULL
};

static char * groovy_primitives[] = {
	"byte","char","short","int","long","BigInteger",
	NULL
};

static int paint_triple_single(struct syntax_state * state) {
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

int syn_groovy_calculate(struct syntax_state * state) {
	if (state->state <= 0) {
		if (state->line_no == 0 && state->i == 0 && charat() == '#') {
			paint_comment(state);
			return -1;
		} else if (charat() == '/' && nextchar() == '/') {
			/* C++-style comments */
			paint_comment(state);
			return -1;
		} else if (charat() == '/' && nextchar() == '*') {
			if (paint_c_comment(state) == 1) return 1;
		} else if (charat() == '"') {
			paint_simple_string(state);
			return 0;
		} else if (charat() == '\'') {
			paint_single_string(state);
			return 0;
		} else if (find_keywords(state, groovy_keywords, FLAG_KEYWORD, c_keyword_qualifier)) {
			return 0;
		} else if (find_keywords(state, groovy_primitives, FLAG_TYPE, c_keyword_qualifier)) {
			return 0;
		} else if (find_keywords(state, groovy_stuff, FLAG_NUMERAL, c_keyword_qualifier)) {
			return 0;
		} else if (!c_keyword_qualifier(lastchar()) && isdigit(charat())) {
			paint_c_numeral(state);
			return 0;
		} else if (charat() != -1) {
			skip();
			return 0;
		}
		return -1;
	} else if (state->state == 1) {
		if (paint_c_comment(state) == 1) return 1;
		return 0;
	} else if (state->state == 2) {
		return paint_triple_single(state);
	}
	return -1;
}

char * syn_groovy_ext[] = {".groovy",".jenkinsfile",".gradle",NULL};

BIM_SYNTAX(groovy, 1)
int syn_hosts_calculate(struct syntax_state * state) {
	if (state->i == 0) {
		if (charat() == '#') {
			while (charat() != -1) {
				if (common_comment_buzzwords(state)) continue;
				else paint(1, FLAG_COMMENT);
			}
		} else {
			while (charat() != '\t' && charat() != ' ' && charat() != -1) paint(1, FLAG_NUMERAL);
			while (charat() != -1) paint(1, FLAG_TYPE);
		}
	}

	return -1;
}

char * syn_hosts_ext[] = {"hosts",NULL};

BIM_SYNTAX(hosts, 1)

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

static int java_at_keyword_qualifier(int c) {
	return isalnum(c) || (c == '_') || (c == '@');
}

static char * syn_java_brace_comments[] = {
	"{@docRoot","{@inheritDoc","{@link","{@linkplain",
	"{@value","{@code","{@literal","{@serial",
	"{@serialData","{@serialField",
	NULL
};

static int java_brace_keyword_qualifier(int c) {
	return isalnum(c) || (c == '{') || (c == '@') || (c == '_');
}

static int paint_java_comment(struct syntax_state * state) {
	int last = -1;
	while (charat() != -1) {
		if (common_comment_buzzwords(state)) continue;
		else if (charat() == '@') {
			if (!find_keywords(state, syn_java_at_comments, FLAG_ESCAPE, java_at_keyword_qualifier)) {
				if (match_and_paint(state, "@param", FLAG_ESCAPE, java_at_keyword_qualifier)) {
					while (charat() == ' ') skip();
					while (c_keyword_qualifier(charat())) paint(1, FLAG_TYPE);
				} else {
					/* Paint the @ */
					paint(1, FLAG_COMMENT);
				}
			}
		} else if (charat() == '{') {
			/* see if this terminates */
			if (find_keywords(state, syn_java_brace_comments, FLAG_ESCAPE, java_brace_keyword_qualifier)) {
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

int syn_java_calculate(struct syntax_state * state) {
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

char * syn_java_ext[] = {".java",NULL};

BIM_SYNTAX_COMPLETER(java) {
	for (char ** keyword = syn_java_keywords; *keyword; ++keyword) {
		add_if_match((*keyword),"(java keyword)");
	}
	for (char ** keyword = syn_java_types; *keyword; ++keyword) {
		add_if_match((*keyword),"(java type)");
	}

	/* XXX Massive hack */
	if (env->col_no > 1 && env->lines[env->line_no-1]->text[env->col_no-2].flags == FLAG_COMMENT) {
		if (comp[0] == '@') {
			for (char ** keyword = syn_java_at_comments; *keyword; ++keyword) {
				add_if_match((*keyword),"(javadoc annotation)");
			}
		} else if (comp[0] == '{') {
			for (char ** keyword = syn_java_brace_comments; *keyword; ++keyword) {
				add_if_match((*keyword),"(javadoc annotation)");
			}
		}
	}

	return 0;
}

BIM_SYNTAX_EXT(java, 1, java_brace_keyword_qualifier)
char * syn_json_keywords[] = {
	"true","false","null",
	NULL
};

int syn_json_calculate(struct syntax_state * state) {
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

char * syn_json_ext[] = {".json",NULL}; // TODO other stuff that uses json

BIM_SYNTAX(json, 1)
static char * syn_kotlin_keywords[] = {
	"as","as?","break","class","continue","do","else","false","for",
	"fun","if","in","!in","interface","is","!is","null","object",
	"package","return","super","this","throw","true","try","typealias",
	"typeof","val","var","when","while",
	"by","catch","constructor","delegate","dynamic","field","file",
	"finally","get","import","init","param","property","receiver",
	"set","setparam","where",
	"actual","abstract","annotation","companion","const",
	"crossinline","data","enum","expect","external","final",
	"infix","inner","internal","lateinit","noinline","open",
	"operator","out","override","private","protected","public",
	"reified","sealed","suspend","tailrec","vararg",
	"field","it","inline",
	NULL
};

static char * syn_kotlin_types[] = {
	"Byte","Short","Int","Long",
	"Float","Double",
	NULL
};

static char * syn_kotlin_at_comments[] = {
	"@author","@see","@since","@return","@throws",
	"@version","@exception","@deprecated",
	/* @param is special */
	NULL
};

static int kt_at_keyword_qualifier(int c) {
	return isalnum(c) || (c == '_') || (c == '@');
}

static char * syn_kotlin_brace_comments[] = {
	"{@docRoot","{@inheritDoc","{@link","{@linkplain",
	"{@value","{@code","{@literal","{@serial",
	"{@serialData","{@serialField",
	NULL
};

static int kotlin_keyword_qualifier(int c) {
	return isalnum(c) || (c == '?') || (c == '!') || (c == '_');
}

static int kt_brace_keyword_qualifier(int c) {
	return isalnum(c) || (c == '{') || (c == '@') || (c == '_');
}

static int paint_kotlin_comment(struct syntax_state * state) {
	int last = -1;
	while (charat() != -1) {
		if (common_comment_buzzwords(state)) continue;
		else if (charat() == '@') {
			if (!find_keywords(state, syn_kotlin_at_comments, FLAG_ESCAPE, kt_at_keyword_qualifier)) {
				if (match_and_paint(state, "@param", FLAG_ESCAPE, kt_at_keyword_qualifier)) {
					while (charat() == ' ') skip();
					while (c_keyword_qualifier(charat())) paint(1, FLAG_TYPE);
				} else {
					/* Paint the @ */
					paint(1, FLAG_COMMENT);
				}
			}
		} else if (charat() == '{') {
			/* see if this terminates */
			if (find_keywords(state, syn_kotlin_brace_comments, FLAG_ESCAPE, kt_brace_keyword_qualifier)) {
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

int syn_kotlin_calculate(struct syntax_state * state) {
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
				if (paint_kotlin_comment(state) == 1) return 1;
			} else if (find_keywords(state, syn_kotlin_keywords, FLAG_KEYWORD, kotlin_keyword_qualifier)) {
				return 0;
			} else if (find_keywords(state, syn_kotlin_types, FLAG_TYPE, c_keyword_qualifier)) {
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
			if (paint_kotlin_comment(state) == 1) return 1;
			return 0;
	}
	return -1;
}

char * syn_kotlin_ext[] = {".kt",NULL};

BIM_SYNTAX_COMPLETER(kotlin) {
	for (char ** keyword = syn_kotlin_keywords; *keyword; ++keyword) {
		add_if_match((*keyword),"(kotlin keyword)");
	}
	for (char ** keyword = syn_kotlin_types; *keyword; ++keyword) {
		add_if_match((*keyword),"(kotlin type)");
	}

	/* XXX Massive hack */
	if (env->col_no > 1 && env->lines[env->line_no-1]->text[env->col_no-2].flags == FLAG_COMMENT) {
		if (comp[0] == '@') {
			for (char ** keyword = syn_kotlin_at_comments; *keyword; ++keyword) {
				add_if_match((*keyword),"(javadoc annotation)");
			}
		} else if (comp[0] == '{') {
			for (char ** keyword = syn_kotlin_brace_comments; *keyword; ++keyword) {
				add_if_match((*keyword),"(javadoc annotation)");
			}
		}
	}

	return 0;
}

BIM_SYNTAX_EXT(kotlin, 1, kt_brace_keyword_qualifier)

int lisp_paren_flags[] = {
	FLAG_DIFFPLUS, FLAG_TYPE, FLAG_PRAGMA, FLAG_KEYWORD,
};

int lisp_paren_flag_from_state(int i) {
	return lisp_paren_flags[i % (sizeof(lisp_paren_flags) / sizeof(*lisp_paren_flags))];
}

int syn_lisp_calculate(struct syntax_state * state) {

	if (state->state == -1) state->state = 0;

	while (charat() != -1) {
		if (charat() == ';') {
			while (charat() != -1) paint(1, FLAG_COMMENT);
		} else if (charat() == '(') {
			paint(1, lisp_paren_flag_from_state(state->state));
			state->state++;

			while (charat() != ' ' && charat() != -1 && charat() != '(' && charat() != ')') {
				paint(1, FLAG_KEYWORD);
			}
		} else if (charat() == ')') {
			if (state->state == 0) {
				paint(1, FLAG_ERROR);
			} else {
				state->state--;
				paint(1, lisp_paren_flag_from_state(state->state));
			}
		} else if (charat() == ':') {
			while (charat() != ' ' && charat() != -1 && charat() != '(' && charat() != ')') {
				paint(1, FLAG_PRAGMA);
			}
		} else if (charat() == '"') {
			paint_simple_string(state);
		} else if (charat() != -1) {
			skip();
		}
	}

	if (state->state == 0) return -1;
	if (charat() == -1) return state->state;
	return -1; /* Not sure what happened */
}

char * syn_lisp_ext[] = {
	".lisp",".lsp",".cl",
	NULL
};

BIM_SYNTAX(lisp, 0)
int make_command_qualifier(int c) {
	return isalnum(c) || c == '_' || c == '-' || c == '.';
}

char * syn_make_commands[] = {
	"define","endef","undefine","ifdef","ifndef","ifeq","ifneq","else","endif",
	"include","sinclude","override","export","unexport","private","vpath",
	"-include",
	NULL
};

char * syn_make_functions[] = {
	"subst","patsubst","findstring","filter","filter-out",
	"sort","word","words","wordlist","firstword","lastword",
	"dir","notdir","suffix","basename","addsuffix","addprefix",
	"join","wildcard","realpath","abspath","error","warning",
	"shell","origin","flavor","foreach","if","or","and",
	"call","eval","file","value",
	NULL
};

char * syn_make_special_targets[] = {
	"all", /* Not really special, but highlight it 'cause I feel like it. */
	".PHONY", ".SUFFIXES", ".DEFAULT", ".PRECIOUS", ".INTERMEDIATE",
	".SECONDARY", ".SECONDEXPANSION", ".DELETE_ON_ERROR", ".IGNORE",
	".LOW_RESOLUTION_TIME", ".SILENT", ".EXPORT_ALL_VARIABLES",
	".NOTPARALLEL", ".ONESHELL", ".POSIX",
	NULL
};

int make_close_paren(struct syntax_state * state) {
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

int make_close_brace(struct syntax_state * state) {
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

int make_variable_or_comment(struct syntax_state * state, int flag) {
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

int syn_make_calculate(struct syntax_state * state) {
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

char * syn_make_ext[] = {"Makefile","makefile","GNUmakefile",".mak",NULL};

BIM_SYNTAX(make, 0)
/**
 * NROFF highlighter
 */

int syn_man_calculate(struct syntax_state * state) {
	while (charat() != -1) {
		if (state->i == 0 && charat() == '.') {
			if (nextchar() == 'S' && charrel(2) == 'H' && charrel(3) == ' ') {
				paint(3, FLAG_KEYWORD);
				while (charat() != -1) paint(1, FLAG_STRING);
			} else if (nextchar() == 'B' && charrel(2) == ' ') {
				paint(2, FLAG_KEYWORD);
				while (charat() != -1) paint(1, FLAG_BOLD);
			} else if (isalpha(nextchar())) {
				paint(1, FLAG_KEYWORD);
				while (charat() != -1 && isalpha(charat())) paint(1, FLAG_KEYWORD);
			} else if (nextchar() == '\\' && charrel(2) == '"') {
				while (charat() != -1) paint(1, FLAG_COMMENT);
			} else {
				skip();
			}
		} else if (charat() == '\\') {
			if (nextchar() == 'f') {
				paint(2, FLAG_NUMERAL);
				paint(1, FLAG_PRAGMA);
			} else {
				paint(2, FLAG_ESCAPE);
			}
		} else {
			skip();
		}
	}
	return -1;
}

/* Yes this is dumb. No, I don't care. */
char * syn_man_ext[] = {".1",".2",".3",".4",".5",".6",".7",".8",NULL};

BIM_SYNTAX(man, 0)
static struct syntax_definition * syn_c = NULL;
static struct syntax_definition * syn_py = NULL;
static struct syntax_definition * syn_java = NULL;
static struct syntax_definition * syn_json = NULL;
static struct syntax_definition * syn_xml = NULL;
static struct syntax_definition * syn_make = NULL;
static struct syntax_definition * syn_diff = NULL;
static struct syntax_definition * syn_rust = NULL;
static struct syntax_definition * syn_bash = NULL;

static int _initialized = 0;

int syn_markdown_calculate(struct syntax_state * state) {
	if (!_initialized) {
		_initialized = 1;
		syn_c    = find_syntax_calculator("c");
		syn_py   = find_syntax_calculator("py");
		syn_java = find_syntax_calculator("java");
		syn_json = find_syntax_calculator("json");
		syn_xml  = find_syntax_calculator("xml");
		syn_make = find_syntax_calculator("make");
		syn_diff = find_syntax_calculator("diff");
		syn_rust = find_syntax_calculator("rust");
		syn_bash = find_syntax_calculator("bash");
	}
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
					if (syn_c &&match_forward(state, "c")) {
						nest(syn_c->calculate, 100);
					} else if (syn_c && match_forward(state,"c++")) {
						nest(syn_c->calculate, 100);
					} else if (syn_py && (match_forward(state,"py") || match_forward(state,"python"))) {
						nest(syn_py->calculate, 200);
					} else if (syn_java && match_forward(state, "java")) {
						nest(syn_java->calculate, 300);
					} else if (syn_json && match_forward(state,"json")) {
						nest(syn_json->calculate, 400);
					} else if (syn_xml && match_forward(state,"xml")) {
						nest(syn_xml->calculate, 500);
					} else if (syn_xml && match_forward(state,"html")) {
						nest(syn_xml->calculate, 500); // TODO this will be a different highlighter later
					} else if (syn_make && match_forward(state,"make")) {
						nest(syn_make->calculate, 600);
					} else if (syn_diff && match_forward(state, "diff")) {
						nest(syn_diff->calculate, 700);
					} else if (syn_bash && match_forward(state, "bash")) {
						nest(syn_bash->calculate, 800);
					} else if (syn_rust && match_forward(state, "rust")) {
						nest(syn_rust->calculate, 900); /* Keep this at the end for now */
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
			nest(syn_c->calculate, 100);
		} else if (state->state < 299) {
			nest(syn_py->calculate, 200);
		} else if (state->state < 399) {
			nest(syn_java->calculate, 300);
		} else if (state->state < 499) {
			nest(syn_json->calculate, 400);
		} else if (state->state < 599) {
			nest(syn_xml->calculate, 500);
		} else if (state->state < 699) {
			nest(syn_make->calculate, 600);
		} else if (state->state < 799) {
			nest(syn_diff->calculate, 700);
		} else if (state->state < 899) {
			nest(syn_bash->calculate, 800);
		} else {
			nest(syn_rust->calculate, 900);
		}
	}
	return -1;
}

char * syn_markdown_ext[] = {".md",".markdown",NULL};

BIM_SYNTAX(markdown, 1)
char * syn_proto_keywords[] = {
	"syntax","import","option","package","message","group","oneof",
	"optional","required","repeated","default","extend","extensions","to","max","reserved",
	"service","rpc","returns","stream",
	NULL
};

char * syn_proto_types[] = {
	"int32","int64","uint32","uint64","sint32","sint64",
	"fixed32","fixed64","sfixed32","sfixed64",
	"float","double","bool","string","bytes",
	"enum",
	NULL
};

char * syn_proto_special[] = {
	"true","false",
	NULL
};

int syn_proto_calculate(struct syntax_state * state) {
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

char * syn_proto_ext[] = {".proto",NULL};

BIM_SYNTAX(proto, 1)
char * syn_py_keywords[] = {
	"class","def","return","del","if","else","elif","for","while","continue",
	"break","assert","as","and","or","except","finally","from","global",
	"import","in","is","lambda","with","nonlocal","not","pass","raise","try","yield",
	NULL
};

char * syn_py_types[] = {
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

char * syn_py_special[] = {
	"True","False","None",
	NULL
};

int paint_py_triple_double(struct syntax_state * state) {
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

int paint_py_triple_single(struct syntax_state * state) {
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

int paint_py_single_string(struct syntax_state * state) {
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

int paint_py_numeral(struct syntax_state * state) {
	if (charat() == '0' && (nextchar() == 'x' || nextchar() == 'X')) {
		paint(2, FLAG_NUMERAL);
		while (isxdigit(charat()) || charat() == '_') paint(1, FLAG_NUMERAL);
	} else if (charat() == '0' && nextchar() == '.') {
		paint(2, FLAG_NUMERAL);
		while (isdigit(charat()) || charat() == '_') paint(1, FLAG_NUMERAL);
		if ((charat() == '+' || charat() == '-') && (nextchar() == 'e' || nextchar() == 'E')) {
			paint(2, FLAG_NUMERAL);
			while (isdigit(charat()) || charat() == '_') paint(1, FLAG_NUMERAL);
		} else if (charat() == 'e' || charat() == 'E') {
			paint(1, FLAG_NUMERAL);
			while (isdigit(charat()) || charat() == '_') paint(1, FLAG_NUMERAL);
		}
		if (charat() == 'j') paint(1, FLAG_NUMERAL);
		return 0;
	} else {
		while (isdigit(charat()) || charat() == '_') paint(1, FLAG_NUMERAL);
		if (charat() == '.') {
			paint(1, FLAG_NUMERAL);
			while (isdigit(charat()) || charat() == '_') paint(1, FLAG_NUMERAL);
			if ((charat() == '+' || charat() == '-') && (nextchar() == 'e' || nextchar() == 'E')) {
				paint(2, FLAG_NUMERAL);
				while (isdigit(charat()) || charat() == '_') paint(1, FLAG_NUMERAL);
			} else if (charat() == 'e' || charat() == 'E') {
				paint(1, FLAG_NUMERAL);
				while (isdigit(charat()) || charat() == '_') paint(1, FLAG_NUMERAL);
			}
			if (charat() == 'j') paint(1, FLAG_NUMERAL);
			return 0;
		}
		if (charat() == 'j') paint(1, FLAG_NUMERAL);
	}
	while (charat() == 'l' || charat() == 'L') paint(1, FLAG_NUMERAL);
	return 0;
}

void paint_py_format_string(struct syntax_state * state, char type) {
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

int syn_py_calculate(struct syntax_state * state) {
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

char * syn_py_ext[] = {".py",NULL};

BIM_SYNTAX(py, 1)
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

int paint_rust_numeral(struct syntax_state * state) {
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

char * syn_rust_ext[] = {".rs",NULL};

BIM_SYNTAX(rust, 1)
static struct syntax_definition * soy_syn_xml = NULL;
static int _soy_initialized = 0;

static char * soy_keywords[] = {
	"call","template","param","namespace","let","and","if","else","elseif",
	"switch","case","default","foreach","literal","sp","nil","lb","rb","in",
	NULL
};

static char * soy_functions[] = {
	"isNonnull","strContains","ceiling","floor","max","min","randomInt",
	"round","index","isFirst","isLast","length","augmentMap","keys",
	NULL
};

int soy_keyword_qualifier(int c) {
	return isalnum(c) || (c == '_') || (c == '.');
}

int syn_soy_calculate(struct syntax_state * state) {
	if (!_soy_initialized) {
		_soy_initialized = 1;
		soy_syn_xml  = find_syntax_calculator("xml");
	}

	if (state->state > 0 && state->state <= 4) {
		return soy_syn_xml ? soy_syn_xml->calculate(state) : 0;
	} else if (state->state == 5) {
		if (paint_c_comment(state) == 1) return 5;
		return 0;
	} else {
		if (charat() == '{') {
			paint(1, FLAG_TYPE);
			while (charat() != -1 && charat() != '}') {
				if (find_keywords(state, soy_keywords, FLAG_KEYWORD, soy_keyword_qualifier)) {
					continue;
				} else if (find_keywords(state, soy_functions, FLAG_TYPE, soy_keyword_qualifier)) {
					continue;
				} else if (charat() == '\'') {
					paint_single_string(state);
				} else if (charat() == '\"') {
					paint_simple_string(state);
				} else if (charat() == '$') {
					paint(1,FLAG_NUMERAL);
					while (charat() != -1 && soy_keyword_qualifier(charat())) paint(1, FLAG_NUMERAL);
				} else {
					skip();
				}
			}
			if (charat() == '}') paint(1, FLAG_TYPE);
			return 0;
		} else if (charat() == '/' && nextchar() == '*') {
			if (paint_c_comment(state) == 1) return 5;
			return 0;
		} else {
			return soy_syn_xml ? soy_syn_xml->calculate(state) : 0;
		}
	}

	return -1;
}

char * syn_soy_ext[] = {".soy",NULL};

BIM_SYNTAX(soy, 1)
int syn_xml_calculate(struct syntax_state * state) {
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

char * syn_xml_ext[] = {".xml",".htm",".html",".iml",NULL}; // TODO other stuff that uses xml (it's a lot!); FIXME htm/html are temporary; make dedicated SGML ones for this

BIM_SYNTAX(xml, 1)
