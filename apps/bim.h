#ifndef _BIM_CORE_H
#define _BIM_CORE_H

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
#include <libgen.h>
#include <locale.h>
#include <wchar.h>
#include <ctype.h>
#include <dirent.h>
#include <poll.h>
#include <limits.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <kuroko/vm.h>

#ifdef __DATE__
# define BIM_BUILD_DATE " built " __DATE__ " at " __TIME__
#else
# define BIM_BUILD_DATE DATE ""
#endif

#ifdef GIT_TAG
# define TAG "-" GIT_TAG
#else
# define TAG "-alpha"
#endif

#define BLOCK_SIZE 4096
#define ENTER_KEY     '\r'
#define LINE_FEED     '\n'
#define BACKSPACE_KEY 0x08
#define DELETE_KEY    0x7F
#define DEFAULT_KEY_WAIT (global_config.background_task ? 0 : -1)

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
#define FLAG_LINK      (12 + (1<<4))
#define FLAG_ESCAPE    13
#define FLAG_EXTRA     14
#define FLAG_SPECIAL   15

#define FLAG_LINK_COLOR 12

#define FLAG_UNDERLINE (1 << 4)
#define FLAG_SELECT    (1 << 5)
#define FLAG_SEARCH    (1 << 6)

#define FLAG_MASK_COLORS 0x0F
#define FLAG_MASK_ATTRIB 0x70

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

typedef struct background_task {
	struct _env * env;
	void (*func)(struct background_task*);
	struct background_task * next;
	int _private_i;
	void * _private_p;
} background_task_t;

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
	int search_point;
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
	unsigned int can_sgrmouse:1;

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
	unsigned int search_wraps:1;
	unsigned int had_error:1;
	unsigned int use_biminfo:1;

	int cursor_padding;
	int split_percent;
	int scroll_amount;
	int tab_offset;

	char * tab_indicator;
	char * space_indicator;

	background_task_t * background_task;
	background_task_t * tail_task;

} global_config_t;

#define OVERLAY_MODE_NONE     0
#define OVERLAY_MODE_READ_ONE 1
#define OVERLAY_MODE_COMMAND  2
#define OVERLAY_MODE_SEARCH   3
#define OVERLAY_MODE_COMPLETE 4
#define OVERLAY_MODE_FILESEARCH 5

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
	unsigned int slowop:1;

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
	void * callable;
};

extern struct theme_def * themes;

extern void add_colorscheme(struct theme_def theme);

struct syntax_state {
	buffer_t * env;
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
	int (*completion_matcher)(uint32_t * comp, struct completion_match ** matches, int * matches_count, int complete_match, int * matches_len, buffer_t * env);
	void * krkFunc;
	void * krkClass;
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
extern int update_biminfo(buffer_t * buf, int is_open);
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
extern void render_commandline_message(char * message, ...);
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

#define paint(length, flag) do { for (int i = 0; i < (length) && state->i < state->line->actual; i++, state->i++) { \
	state->line->text[state->i].flags = (state->line->text[state->i].flags & (3 << 5)) | (flag); \
} } while (0)
#define charat() (state->i < state->line->actual ? state->line->text[(state->i)].codepoint : -1)
#define nextchar() (state->i + 1 < state->line->actual ? state->line->text[(state->i+1)].codepoint : -1)
#define lastchar() (state->i - 1 >= 0 ? state->line->text[(state->i-1)].codepoint : -1)
#define skip() (state->i++)
#define charrel(x) ((state->i + (x) >= 0 && state->i + (x) < state->line->actual) ? state->line->text[(state->i+(x))].codepoint : -1)

static int match_and_paint(struct syntax_state * state, const char * keyword, int flag, int (*keyword_qualifier)(int c));
static int common_comment_buzzwords(struct syntax_state * state);
static int paint_comment(struct syntax_state * state);
static struct syntax_definition * find_syntax_calculator(const char * name);

#define nest(lang, low) \
	do { \
		state->state = (state->state < 1 ? 0 : state->state - low); \
		do { state->state = lang(state); } while (state->state == 0); \
		if (state->state == -1) return low; \
		return state->state + low; \
	} while (0)

/* Hacky workaround for isdigit not really accepting Unicode stuff */
static __attribute__((used)) int _isdigit(int c) { if (c > 128) return 0; return isdigit(c); }
static __attribute__((used)) int _isxdigit(int c) { if (c > 128) return 0; return isxdigit(c); }

#undef isdigit
#undef isxdigit
#define isdigit(c) _isdigit(c)
#define isxdigit(c) _isxdigit(c)

#endif /* _BIM_CORE_H */
