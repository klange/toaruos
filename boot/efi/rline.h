#pragma once

struct rline_callback;

typedef struct {
	char *  buffer;
	struct rline_callback * callbacks;
	int     collected;
	int     requested;
	int     newline;
	int     cancel;
	int     offset;
	int     tabbed;
	int     quiet;
} rline_context_t;

typedef void (*rline_callback_t)(rline_context_t * context);

typedef struct rline_callback {
	rline_callback_t tab_complete;
	rline_callback_t redraw_prompt;
	rline_callback_t special_key;
	rline_callback_t key_up;
	rline_callback_t key_down;
	rline_callback_t key_left;
	rline_callback_t key_right;
	rline_callback_t rev_search;
} rline_callbacks_t;

typedef enum {
	/* Base colors */
	RLINE_STYLE_MAIN,
	RLINE_STYLE_ALT,
	/* Syntax flags */
	RLINE_STYLE_KEYWORD,
	RLINE_STYLE_STRING,
	RLINE_STYLE_COMMENT,
	RLINE_STYLE_TYPE,
	RLINE_STYLE_PRAGMA,
	RLINE_STYLE_NUMERAL,
} rline_style_t;

extern int rline(char * buffer, int buf_size);
extern int rline_exp_set_prompts(char * left, char * right, int left_width, int right_width);
extern int rline_exp_set_shell_commands(char ** cmds, int len);
extern int rline_exp_set_tab_complete_func(rline_callback_t func);
extern int rline_exp_set_syntax(char * name);
extern void rline_history_insert(char * str);
extern void rline_history_append_line(char * str);
extern char * rline_history_get(int item);
extern char * rline_history_prev(int item);
extern void rline_place_cursor(void);
extern void rline_set_colors(rline_style_t style);
extern void rline_insert(rline_context_t * context, const char * what);
extern int rline_terminal_width;

#define RLINE_HISTORY_ENTRIES 128
extern char * rline_history[RLINE_HISTORY_ENTRIES];
extern int rline_history_count;
extern int rline_history_offset;
extern int rline_scroll;
extern char * rline_exit_string;
extern char * rline_preload;
