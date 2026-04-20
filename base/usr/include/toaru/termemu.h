#pragma once

#include <_cheader.h>
#include <stdint.h>
#include <toaru/list.h>

_Begin_C_Header

#define TERM_BUF_LEN 128

/* A terminal cell represents a single character on screen */
typedef struct {
	uint32_t c;     /* codepoint */
	uint32_t fg;    /* background indexed color */
	uint32_t bg;    /* foreground indexed color */
	uint32_t flags; /* other flags */
} term_cell_t;

struct TermemuState;

typedef struct {
	void (*cls)                (struct TermemuState *, int);
	void (*scroll)             (struct TermemuState *, int);
	void (*input_buffer_stuff) (struct TermemuState *, char *);
	void (*set_title)          (struct TermemuState *, char *);
	void (*set_cell_contents)  (struct TermemuState *, int,int,char *);
	int  (*get_cell_width)     (struct TermemuState *);
	int  (*get_cell_height)    (struct TermemuState *);
	void (*full_reset)         (struct TermemuState *);
	void (*state_change)       (struct TermemuState *);
} term_callbacks_t;

struct TermemuScrollbackRow {
	unsigned short width;
	term_cell_t cells[];
};

struct TermemuScrollbackState {
	size_t max_scrollback;
	list_t * scrollback_list;
	ssize_t scrollback_offset;
};


typedef struct TermemuState {
	void * priv;
	uint16_t x;       /* Current cursor location */
	uint16_t y;       /*    "      "       "     */
	uint16_t h;       /* Cursor hold */
	uint16_t save_x;  /* Last cursor save */
	uint16_t save_y;
	uint16_t orig_x;
	uint16_t orig_y;
	int32_t width;   /* Terminal width */
	int32_t height;  /*     "    height */
	uint32_t fg;      /* Current foreground color */
	uint32_t bg;      /* Current background color */
	uint32_t flags;   /* Bright, etc. */
	uint8_t  escape;  /* Escape status */
	uint8_t  box;
	uint8_t  buflen;  /* Buffer Length */
	char     buffer[TERM_BUF_LEN];  /* Previous buffer */
	term_callbacks_t * callbacks;
	int volatile lock;
	uint8_t  mouse_on;
	uint32_t img_collected;
	uint32_t img_size;
	char *   img_data;
	uint8_t  paste_mode;
	int      active_buffer;
	uint32_t current_fg; /* reflective of bold */
	uint32_t current_bg; /* should always be same as bg? */
	uint32_t orig_fg;
	uint32_t orig_bg;
	int cursor_on;
	int cursor_flipped;
	int focused;
	unsigned long long mouse_ticks;
	uint64_t last_click;

	term_cell_t * term_buffer; /* The active terminal cell buffer */
	term_cell_t * term_buffer_a; /* The main buffer */
	term_cell_t * term_buffer_b; /* The secondary buffer */
	term_cell_t * term_mirror;  /* What we want to draw */
	term_cell_t * term_display; /* What we think we've drawn already */

	int selection;
	int selection_start_x;
	int selection_start_y;
	int selection_start_xx;
	int selection_end_x;
	int selection_end_y;

	struct TermemuScrollbackState * scrollback;
} term_state_t;

/* Triggers escape mode. */
#define ANSI_ESCAPE  27
/* Escape verify */
#define ANSI_BRACKET '['
#define ANSI_BRACKET_RIGHT ']'
#define ANSI_OPEN_PAREN '('
/* Anything in this range (should) exit escape mode. */
#define ANSI_LOW    'A'
#define ANSI_HIGH   'z'
/* Escape commands */
#define ANSI_CUU    'A' /* CUrsor Up                  */
#define ANSI_CUD    'B' /* CUrsor Down                */
#define ANSI_CUF    'C' /* CUrsor Forward             */
#define ANSI_CUB    'D' /* CUrsor Back                */
#define ANSI_CNL    'E' /* Cursor Next Line           */
#define ANSI_CPL    'F' /* Cursor Previous Line       */
#define ANSI_CHA    'G' /* Cursor Horizontal Absolute */
#define ANSI_CUP    'H' /* CUrsor Position            */
#define ANSI_ED     'J' /* Erase Data                 */
#define ANSI_EL     'K' /* Erase in Line              */
#define ANSI_SU     'S' /* Scroll Up                  */
#define ANSI_SD     'T' /* Scroll Down                */
#define ANSI_HVP    'f' /* Horizontal & Vertical Pos. */
#define ANSI_SGR    'm' /* Select Graphic Rendition   */
#define ANSI_DSR    'n' /* Device Status Report       */
#define ANSI_SCP    's' /* Save Cursor Position       */
#define ANSI_RCP    'u' /* Restore Cursor Position    */
#define ANSI_HIDE   'l' /* DECTCEM - Hide Cursor      */
#define ANSI_SHOW   'h' /* DECTCEM - Show Cursor      */
#define ANSI_IL     'L' /* Insert Line(s)             */
#define ANSI_DL     'M' /* Delete Line(s)             */
/* Display flags */
#define ANSI_BOLD      0x01
#define ANSI_UNDERLINE 0x02
#define ANSI_ITALIC    0x04
#define ANSI_ALTFONT   0x08 /* Character should use alternate font */
#define ANSI_SPECBG    0x10
#define ANSI_BORDER    0x20
#define ANSI_WIDE      0x40 /* Character is double width */
#define ANSI_CROSS     0x80 /* And that's all I'm going to support (for now) */
#define ANSI_EXT_IMG   0x100 /* Cell is actually an image, use fg color as pointer */
#define ANSI_MARKED    0x200 /* Marked for selection update */
#define ANSI_RED       0x400 /* Marked as red after selection */
#define ANSI_INVERTED  0x800 /* Cell was inverted (by us, for special rendering) */
#define ANSI_INVERT    0x1000 /* The inverted flag, like the bold or italic or underline flag. */

#define ANSI_EXT_IOCTL 'z'  /* These are special escapes only we support */

/* Default color settings */
#define TERM_DEFAULT_FG     0x07 /* Index of default foreground */
#define TERM_DEFAULT_BG     0x10 /* Index of default background */
#define TERM_DEFAULT_FLAGS  0x00 /* Default flags for a cell */
#define TERM_DEFAULT_OPAC   0xF2 /* For background, default transparency */

#define TERMEMU_MOUSE_ENABLE  0x01
#define TERMEMU_MOUSE_DRAG    0x02
#define TERMEMU_MOUSE_SGR     0x04
#define TERMEMU_MOUSE_ALTSCRL 0x80
/* TODO: _MOUSE_UTF8          0x10 */
/* TODO: _MOUSE_URXVT         0x80 */

extern term_state_t * termemu_init(int w, int h, term_callbacks_t * callbacks_in);
extern int termemu_reinit(term_state_t * state, int w, int h);
extern void termemu_put(term_state_t * s, char c);

int termemu_to_eight(uint32_t codepoint, char * out);

void termemu_iterate_selection(term_state_t * state, void (*func)(term_state_t * s, uint16_t x, uint16_t y));
void termemu_redraw_selection(term_state_t * state);
term_cell_t * termemu_cell_at(term_state_t * state, uint16_t x, uint16_t _y);
void termemu_mark_cell(term_state_t * state, uint16_t x, uint16_t y);
void termemu_mark_selection(term_state_t * state);
void termemu_red_cell(term_state_t * state, uint16_t x, uint16_t y);
void termemu_flip_selection(term_state_t * state);
void termemu_scroll_up(term_state_t * state, int amount);
void termemu_scroll_down(term_state_t * state, int amount);
void termemu_switch_buffer(term_state_t * state, int buffer);
void termemu_redraw_all(term_state_t * state);
void termemu_redraw_scrollback(term_state_t * state);
void termemu_unscroll(term_state_t * state);
void termemu_draw_cursor(term_state_t * state);
void termemu_scroll_top(term_state_t * state);
void termemu_init_scrollback(term_state_t * s, int max_scrollback);
void termemu_maybe_flip_cursor(term_state_t * state);
void termemu_selection_click(term_state_t * state, int new_x, int new_y);
void termemu_selection_drag(term_state_t * state, int new_x, int new_y);
void termemu_clear(term_state_t * state, int i);
void termemu_full_reset(term_state_t * s);

_End_C_Header

