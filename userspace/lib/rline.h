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

void rline_redraw(rline_context_t * context);
void rline_redraw_clean(rline_context_t * context);
void rline_insert(rline_context_t * context, const char * what);
int rline(char * buffer, int buf_size, rline_callbacks_t * callbacks);

void rline_history_insert(char * str);
void rline_history_append_line(char * str);
char * rline_history_get(int item);
char * rline_history_prev(int item);

#define RLINE_HISTORY_ENTRIES 128
extern char * rline_history[RLINE_HISTORY_ENTRIES];
extern int rline_history_count;
extern int rline_history_offset;
extern int rline_scroll;
extern char * rline_exit_string;

