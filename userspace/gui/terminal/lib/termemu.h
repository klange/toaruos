#ifndef _TERMEMU_H__
#define _TERMEMU_H__

#include <stdint.h>

#define TERM_BUF_LEN 128

/* A terminal cell represents a single character on screen */
typedef struct {
	uint16_t c;     /* codepoint */
	uint16_t flags; /* other flags */
	uint32_t fg;    /* background indexed color */
	uint32_t bg;    /* foreground indexed color */
} term_cell_t;

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
} term_state_t;

#if 0
typedef struct {
	term_state_t state;
	void (*writer)(char);
	void (*set_color)(uint32_t, uint32_t);
	void (*set_csr)(int,int);
	int  (*get_csr_x)(void);
	int  (*get_csr_y)(void);
	void (*set_cell)(int,int,uint16_t);
	void (*cls)(int);
	void (*scroll)(int);
	void (*redraw_cursor)(void);
	void (*input_stuff)(char * str);
} term_emu_t;
#endif


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
/* Display flags */
#define ANSI_BOLD      0x01
#define ANSI_UNDERLINE 0x02
#define ANSI_ITALIC    0x04
#define ANSI_ALTFONT   0x08 /* Character should use alternate font */
#define ANSI_SPECBG    0x10
#define ANSI_BORDER    0x20
#define ANSI_WIDE      0x40 /* Character is double width */
#define ANSI_CROSS     0x80 /* And that's all I'm going to support (for now) */

#define ANSI_EXT_IOCTL 'z'  /* These are special escapes only we support */

/* Default color settings */
#define TERM_DEFAULT_FG     0x07 /* Index of default foreground */
#define TERM_DEFAULT_BG     0x10 /* Index of default background */
#define TERM_DEFAULT_FLAGS  0x00 /* Default flags for a cell */
#define TERM_DEFAULT_OPAC   0xF2 /* For background, default transparency */

void ansi_init(void (*writer)(char), int w, int y, void (*setcolor)(uint32_t, uint32_t),
		void (*setcsr)(int,int), int (*getcsrx)(void), int (*getcsry)(void), void (*setcell)(int,int,uint16_t),
		void (*cls)(int), void (*redraw_csr)(void), void (*scroll_term)(int), void (*stuff)(char *),
		void (*fontsize)(float), void (*settitle)(char *));

void ansi_put(char c);
term_state_t * ansi_state();


#endif /* _TERMEMU_H__ */
