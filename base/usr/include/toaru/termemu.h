#pragma once

#include <_cheader.h>

#ifdef _KERNEL_
#	include <kernel/types.h>
#else
#	include <stdint.h>
#endif

_Begin_C_Header

#define TERM_BUF_LEN 128

/* A terminal cell represents a single character on screen */
typedef struct {
	uint32_t c;     /* codepoint */
	uint32_t fg;    /* background indexed color */
	uint32_t bg;    /* foreground indexed color */
	uint32_t flags; /* other flags */
} term_cell_t;

typedef struct {
	void (*writer)(char);
	void (*set_color)(uint32_t, uint32_t);
	void (*set_csr)(int,int);
	int  (*get_csr_x)(void);
	int  (*get_csr_y)(void);
	void (*set_cell)(int,int,uint32_t);
	void (*cls)(int);
	void (*scroll)(int);
	void (*redraw_cursor)(void);
	void (*input_buffer_stuff)(char *);
	void (*set_title)(char *);
	void (*set_cell_contents)(int,int,char *);
	int  (*get_cell_width)(void);
	int  (*get_cell_height)(void);
	void (*set_csr_on)(int);
	void (*switch_buffer)(int);
	void (*insert_delete_lines)(int);
} term_callbacks_t;

typedef struct {
	uint16_t x;       /* Current cursor location */
	uint16_t y;       /*    "      "       "     */
	uint16_t save_x;  /* Last cursor save */
	uint16_t save_y;
	uint32_t width;   /* Terminal width */
	uint32_t height;  /*     "    height */
	uint32_t fg;      /* Current foreground color */
	uint32_t bg;      /* Current background color */
	uint8_t  flags;   /* Bright, etc. */
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

#define ANSI_EXT_IOCTL 'z'  /* These are special escapes only we support */

/* Default color settings */
#define TERM_DEFAULT_FG     0x07 /* Index of default foreground */
#define TERM_DEFAULT_BG     0x10 /* Index of default background */
#define TERM_DEFAULT_FLAGS  0x00 /* Default flags for a cell */
#define TERM_DEFAULT_OPAC   0xF2 /* For background, default transparency */

#define TERMEMU_MOUSE_ENABLE  0x01
#define TERMEMU_MOUSE_DRAG    0x02
#define TERMEMU_MOUSE_SGR     0x04
/* TODO: _MOUSE_UTF8          0x08 */
/* TODO: _MOUSE_URXVT         0x10 */

extern term_state_t * ansi_init(term_state_t * s, int w, int y, term_callbacks_t * callbacks_in);
extern void ansi_put(term_state_t * s, char c);

_End_C_Header

