/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Terminal Emulator
 *
 * Graphical terminal emulator.
 *
 * Provides a number of features:
 *  - Windowed and full screen modes
 *  - Antialiased fonts
 *  - Built-in fallback bitmap font
 *  - ANSI escape support
 *  - 256 colors
 *  - [mouse support; incomplete]
 */

#include <stdio.h>
#include <stdint.h>
#include <syscall.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <getopt.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_CACHE_H

#include <wchar.h>

#include "lib/wcwidth.h"
#include "lib/utf8decode.h"
#include "../kernel/include/mouse.h"

#include "lib/graphics.h"
#include "lib/window.h"
#include "lib/decorations.h"
#include "lib/pthread.h"
#include "lib/kbd.h"

#include "terminal-palette.h"
#include "terminal-font.h"

/*
 * If you're working on updating the terminal's handling of escapes,
 * switch this option to 1 to have it print an escaped bit of output
 * to the serial line, so you can examine exactly which codes were
 * processed.
 */
#define DEBUG_TERMINAL_WITH_SERIAL 0

static int volatile lock = 0;

static void spin_lock(int volatile * lock) {
	while(__sync_lock_test_and_set(lock, 0x01)) {
		syscall_yield();
	}
}

static void spin_unlock(int volatile * lock) {
	__sync_lock_release(lock);
}


/* A terminal cell represents a single character on screen */
typedef struct _terminal_cell {
	uint16_t c;     /* codepoint */
	uint32_t fg;    /* background indexed color */
	uint32_t bg;    /* foreground indexed color */
	uint8_t  flags; /* other flags */
} __attribute__((packed)) t_cell;

/* master and slave pty descriptors */
static int fd_master, fd_slave;

int      scale_fonts    = 0;    /* Whether fonts should be scaled */
float    font_scaling   = 1.0;  /* How much they should be scaled by */
uint16_t term_width     = 0;    /* Width of the terminal (in cells) */
uint16_t term_height    = 0;    /* Height of the terminal (in cells) */
uint16_t font_size      = 13;   /* Font size according to Freetype */
uint16_t char_width     = 8;    /* Width of a cell in pixels */
uint16_t char_height    = 12;   /* Height of a cell in pixels */
uint16_t char_offset    = 0;    /* Offset of the font within the cell */
uint16_t csr_x          = 0;    /* Cursor X */
uint16_t csr_y          = 0;    /* Cursor Y */
t_cell * term_buffer    = NULL; /* The terminal cell buffer */
uint32_t current_fg     = 7;    /* Current foreground color */
uint32_t current_bg     = 0;    /* Current background color */
uint8_t  cursor_on      = 1;    /* Whether or not the cursor should be rendered */
window_t * window       = NULL; /* GUI window */
uint8_t  _windowed      = 0;    /* Whether or not we are running in the GUI enviornment */
uint8_t  _vga_mode      = 0;    /* Whether or not we are in VGA mode XXX should be combined ^ */
uint8_t  _login_shell   = 0;    /* Whether we're going to display a login shell or not */
uint8_t  _use_freetype  = 0;    /* Whether we should use freetype or not XXX seriously, how about some flags */
uint8_t  _unbuffered    = 0;
uint8_t  _force_kernel  = 0;
uint8_t  _hold_out      = 0;    /* state indicator on last cell ignore \n */

void reinit(); /* Defined way further down */

/* Cursor bink timer */
static unsigned int timer_tick = 0;
#define TIMER_TICK 40

/* Mouse control */
#define MOUSE_SCALE 6
int32_t mouse_x;
int32_t mouse_y;

/* Some GUI-only options */
uint16_t window_width  = 640;
uint16_t window_height = 408;
#define TERMINAL_TITLE_SIZE 512
char   terminal_title[TERMINAL_TITLE_SIZE];
size_t terminal_title_length = 0;
gfx_context_t * ctx;
volatile int needs_redraw = 1;
static void render_decors();
void term_clear();
void resize_callback(window_t * window);

/* Trigger to exit the terminal when the child process dies or
 * we otherwise receive an exit signal */
volatile int exit_application = 0;

/* Triggers escape mode. */
#define ANSI_ESCAPE  27
/* Escape verify */
#define ANSI_BRACKET '['
#define ANSI_BRACKET_RIGHT ']'
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
#define ANSI_HVP    'f' /* Horizontal & Vertical Pos. XXX: SAME AS CUP */
#define ANSI_SGR    'm' /* Select Graphic Rendition   */
#define ANSI_DSR    'n' /* Device Status Report XXX: Push to kgets() buffer? */
#define ANSI_SCP    's' /* Save Cursor Position       */
#define ANSI_RCP    'u' /* Restore Cursor Position    */
#define ANSI_HIDE   'l' /* DECTCEM - Hide Cursor      */
#define ANSI_SHOW   'h' /* DECTCEM - Show Cursor      */
/* Display flags */
#define ANSI_BOLD      0x01
#define ANSI_UNDERLINE 0x02
#define ANSI_ITALIC    0x04
#define ANSI_EXTRA     0x08 /* Character should use "extra" font (Japanese) */
#define ANSI_SPECBG    0x10
#define ANSI_OVERLINE  0x20
#define ANSI_WIDE      0x40 /* Character is double width */
#define ANSI_CROSS     0x80 /* And that's all I'm going to support */

#define DEFAULT_FG     0x07
#define DEFAULT_BG     0x10
#define DEFAULT_FLAGS  0x00

#define ANSI_EXT_IOCTL 'z'  /* These are special escapes only we support */

#define MAX_ARGS 1024

/* Returns the lower of two shorts */
uint16_t min(uint16_t a, uint16_t b) {
	return (a < b) ? a : b;
}

/* Returns the higher of two shorts */
uint16_t max(uint16_t a, uint16_t b) {
	return (a > b) ? a : b;
}

/* Performs low-level port I/O; used for VGA initialization */
void outb(unsigned char _data, unsigned short _port) {
	__asm__ __volatile__ ("outb %1, %0" : : "dN" (_port), "a" (_data));
}

/* State machine status */
static struct _ansi_state {
	uint16_t x     ;  /* Current cursor location */
	uint16_t y     ;  /*    "      "       "     */
	uint16_t save_x;
	uint16_t save_y;
	uint32_t width ;
	uint32_t height;
	uint32_t fg    ;  /* Current foreground color */
	uint32_t bg    ;  /* Current background color */
	uint8_t  flags ;  /* Bright, etc. */
	uint8_t  escape;  /* Escape status */
	uint8_t  local_echo;
	uint8_t  buflen;  /* Buffer Length */
	char     buffer[100];  /* Previous buffer */
} state;

void (*ansi_writer)(char) = NULL;
void (*ansi_set_color)(uint32_t, uint32_t) = NULL;
void (*ansi_set_csr)(int,int) = NULL;
int  (*ansi_get_csr_x)(void) = NULL;
int  (*ansi_get_csr_y)(void) = NULL;
void (*ansi_set_cell)(int,int,uint16_t) = NULL;
void (*ansi_cls)(int) = NULL;
void (*ansi_scroll)(int) = NULL;

/* XXX: Needs verification, but I'm pretty sure this never gets called */
void (*redraw_cursor)(void) = NULL;

/* Stuffs a string into the stdin of the terminal's child process
 * Useful for things like the ANSI DSR command. */
void input_buffer_stuff(char * str) {
	size_t s = strlen(str) + 1;
	write(fd_master, str, s);
}

/* Write the contents of the buffer, as they were all non-escaped data. */
void ansi_dump_buffer() {
	for (int i = 0; i < state.buflen; ++i) {
		ansi_writer(state.buffer[i]);
	}
}

/* Add to the internal buffer for the ANSI parser */
void ansi_buf_add(char c) {
	state.buffer[state.buflen] = c;
	state.buflen++;
	state.buffer[state.buflen] = '\0';
}

static void _ansi_put(char c);

static void ansi_put(char c) {
	spin_lock(&lock);
	_ansi_put(c);
	spin_unlock(&lock);
}

static void _ansi_put(char c) {
	switch (state.escape) {
		case 0:
			/* We are not escaped, check for escape character */
			if (c == ANSI_ESCAPE) {
				/*
				 * Enable escape mode, setup a buffer,
				 * fill the buffer, get out of here.
				 */
				state.escape    = 1;
				state.buflen    = 0;
				ansi_buf_add(c);
				return;
			} else {
				ansi_writer(c);
			}
			break;
		case 1:
			/* We're ready for [ */
			if (c == ANSI_BRACKET) {
				state.escape = 2;
				ansi_buf_add(c);
			} else if (c == ANSI_BRACKET_RIGHT) {
				state.escape = 3;
				ansi_buf_add(c);
			} else {
				/* This isn't a bracket, we're not actually escaped!
				 * Get out of here! */
				ansi_dump_buffer();
				ansi_writer(c);
				state.escape = 0;
				state.buflen = 0;
				return;
			}
			break;
		case 2:
			if (c >= ANSI_LOW && c <= ANSI_HIGH) {
				/* Woah, woah, let's see here. */
				char * pch;  /* tokenizer pointer */
				char * save; /* strtok_r pointer */
				char * argv[MAX_ARGS]; /* escape arguments */
				/* Get rid of the front of the buffer */
				strtok_r(state.buffer,"[",&save);
				pch = strtok_r(NULL,";",&save);
				/* argc = Number of arguments, obviously */
				int argc = 0;
				while (pch != NULL) {
					argv[argc] = (char *)pch;
					++argc;
					if (argc > MAX_ARGS)
						break;
					pch = strtok_r(NULL,";",&save);
				}
				/* Alright, let's do this */
				switch (c) {
					case ANSI_EXT_IOCTL:
						{
							if (argc > 0) {
								int arg = atoi(argv[0]);
								switch (arg) {
									case 1:
										redraw_cursor();
										break;
									case 1001:
										/* Local Echo Off */
										state.local_echo = 0;
										break;
									case 1002:
										/* Local Echo On */
										state.local_echo = 1;
										break;
									case 1555:
										if (argc > 1) {
											scale_fonts  = 1;
											font_scaling = atof(argv[1]);
											reinit();
										}
										break;
									case 1560:
										_unbuffered = 1;
										break;
									case 1561:
										_unbuffered = 0;
										break;
									case 2000:
										if (_windowed) {
											window_disable_alpha(window);
										}
										break;
									case 2001:
										if (_windowed) {
											window_enable_alpha(window);
										}
										break;
									case 3000:
										if (_windowed) {
											if (argc > 2) {
												uint16_t win_id = window->bufid;
												int width = atoi(argv[1]) * char_width + decor_left_width + decor_right_width;
												int height = atoi(argv[2]) * char_height + decor_top_height + decor_bottom_height;
												window_resize(window, window->x, window->y, width, height);
												resize_callback(window);
											}
										}
									default:
										break;
								}
							}
						}
						break;
					case ANSI_SCP:
						{
							state.save_x = ansi_get_csr_x();
							state.save_y = ansi_get_csr_y();
						}
						break;
					case ANSI_RCP:
						{
							ansi_set_csr(state.save_x, state.save_y);
						}
						break;
					case ANSI_SGR:
						/* Set Graphics Rendition */
						if (argc == 0) {
							/* Default = 0 */
							argv[0] = "0";
							argc    = 1;
						}
						for (int i = 0; i < argc; ++i) {
							int arg = atoi(argv[i]);
							if (arg >= 100 && arg < 110) {
								/* Bright background */
								state.bg = 8 + (arg - 100);
								state.flags |= ANSI_SPECBG;
							} else if (arg >= 90 && arg < 100) {
								/* Bright foreground */
								state.fg = 8 + (arg - 90);
							} else if (arg >= 40 && arg < 49) {
								/* Set background */
								state.bg = arg - 40;
								state.flags |= ANSI_SPECBG;
							} else if (arg == 49) {
								state.bg = DEFAULT_BG;
								state.flags &= ~ANSI_SPECBG;
							} else if (arg >= 30 && arg < 39) {
								/* Set Foreground */
								state.fg = arg - 30;
							} else if (arg == 39) {
								/* Default Foreground */
								state.fg = 7;
							} else if (arg == 9) {
								/* X-OUT */
								state.flags |= ANSI_CROSS;
							} else if (arg == 7) {
								/* INVERT: Swap foreground / background */
								uint32_t temp = state.fg;
								state.fg = state.bg;
								state.bg = temp;
							} else if (arg == 6) {
								/* proprietary RGBA color support */
								if (i == 0) { break; }
								if (i < argc) {
									int r = atoi(argv[i+1]);
									int g = atoi(argv[i+2]);
									int b = atoi(argv[i+3]);
									int a = atoi(argv[i+4]);
									if (a == 0) a = 1; /* Override a = 0 */
									uint32_t c = rgba(r,g,b,a);
									if (atoi(argv[i-1]) == 48) {
										state.bg = c;
										state.flags |= ANSI_SPECBG;
									} else if (atoi(argv[i-1]) == 38) {
										state.fg = c;
									}
									i += 4;
								}
							} else if (arg == 5) {
								/* Supposed to be blink; instead, support X-term 256 colors */
								if (i == 0) { break; }
								if (i < argc) {
									if (atoi(argv[i-1]) == 48) {
										/* Background to i+1 */
										state.bg = atoi(argv[i+1]);
										state.flags |= ANSI_SPECBG;
									} else if (atoi(argv[i-1]) == 38) {
										/* Foreground to i+1 */
										state.fg = atoi(argv[i+1]);
									}
									++i;
								}
							} else if (arg == 4) {
								/* UNDERLINE */
								state.flags |= ANSI_UNDERLINE;
							} else if (arg == 3) {
								/* ITALIC: Oblique */
								state.flags |= ANSI_ITALIC;
							} else if (arg == 2) {
								/* Konsole RGB color support */
								if (i == 0) { break; }
								if (i < argc - 2) {
									int r = atoi(argv[i+1]);
									int g = atoi(argv[i+2]);
									int b = atoi(argv[i+3]);
									uint32_t c = rgb(r,g,b);
									if (atoi(argv[i-1]) == 48) {
										/* Background to i+1 */
										state.bg = c;
										state.flags |= ANSI_SPECBG;
									} else if (atoi(argv[i-1]) == 38) {
										/* Foreground to i+1 */
										state.fg = c;
									}
									i += 3;
								}
							} else if (arg == 1) {
								/* BOLD/BRIGHT: Brighten the output color */
								state.flags |= ANSI_BOLD;
							} else if (arg == 0) {
								/* Reset everything */
								state.fg = DEFAULT_FG;
								state.bg = DEFAULT_BG;
								state.flags = DEFAULT_FLAGS;
							}
						}
						break;
					case ANSI_SHOW:
						if (argc > 0) {
							if (!strcmp(argv[0], "?1049")) {
								ansi_cls(2);
								ansi_set_csr(0,0);
							}
						}
						break;
					case ANSI_CUF:
						{
							int i = 1;
							if (argc) {
								i = atoi(argv[0]);
							}
							ansi_set_csr(min(ansi_get_csr_x() + i, state.width - 1), ansi_get_csr_y());
						}
						break;
					case ANSI_CUU:
						{
							int i = 1;
							if (argc) {
								i = atoi(argv[0]);
							}
							ansi_set_csr(ansi_get_csr_x(), max(ansi_get_csr_y() - i, 0));
						}
						break;
					case ANSI_CUD:
						{
							int i = 1;
							if (argc) {
								i = atoi(argv[0]);
							}
							ansi_set_csr(ansi_get_csr_x(), min(ansi_get_csr_y() + i, state.height - 1));
						}
						break;
					case ANSI_CUB:
						{
							int i = 1;
							if (argc) {
								i = atoi(argv[0]);
							}
							ansi_set_csr(max(ansi_get_csr_x() - i,0), ansi_get_csr_y());
						}
						break;
					case ANSI_CHA:
						if (argc < 1) {
							ansi_set_csr(0,ansi_get_csr_y());
						} else {
							ansi_set_csr(min(max(atoi(argv[0]), 1), state.width) - 1, ansi_get_csr_y());
						}
						break;
					case ANSI_CUP:
						if (argc < 2) {
							ansi_set_csr(0,0);
						} else {
							ansi_set_csr(min(max(atoi(argv[1]), 1), state.width) - 1, min(max(atoi(argv[0]), 1), state.height) - 1);
						}
						break;
					case ANSI_ED:
						if (argc < 1) {
							ansi_cls(0);
						} else {
							ansi_cls(atoi(argv[0]));
						}
						break;
					case ANSI_EL:
						{
							int what = 0, x = 0, y = 0;
							if (argc >= 1) {
								what = atoi(argv[0]);
							}
							if (what == 0) {
								x = ansi_get_csr_x();
								y = state.width;
							} else if (what == 1) {
								x = 0;
								y = ansi_get_csr_x();
							} else if (what == 2) {
								x = 0;
								y = state.width;
							}
							for (int i = x; i < y; ++i) {
								ansi_set_cell(i, ansi_get_csr_y(), ' ');
							}
						}
						break;
					case ANSI_DSR:
						{
							char out[24];
							sprintf(out, "\033[%d;%dR", csr_y + 1, csr_x + 1);
							input_buffer_stuff(out);
						}
						break;
					case ANSI_SU:
						{
							int how_many = 1;
							if (argc > 0) {
								how_many = atoi(argv[0]);
							}
							ansi_scroll(how_many);
						}
						break;
					case ANSI_SD:
						{
							int how_many = 1;
							if (argc > 0) {
								how_many = atoi(argv[0]);
							}
							ansi_scroll(-how_many);
						}
						break;
					case 'X':
						{
							int how_many = 1;
							if (argc > 0) {
								how_many = atoi(argv[0]);
							}
							for (int i = 0; i < how_many; ++i) {
								ansi_writer(' ');
							}
						}
						break;
					case 'd':
						if (argc < 1) {
							ansi_set_csr(ansi_get_csr_x(), 0);
						} else {
							ansi_set_csr(ansi_get_csr_x(), atoi(argv[0]) - 1);
						}
						break;
					default:
						/* Meh */
						break;
				}
				/* Set the states */
				if (state.flags & ANSI_BOLD && state.fg < 9) {
					ansi_set_color(state.fg % 8 + 8, state.bg);
				} else {
					ansi_set_color(state.fg, state.bg);
				}
				/* Clear out the buffer */
				state.buflen = 0;
				state.escape = 0;
				return;
			} else {
				/* Still escaped */
				ansi_buf_add(c);
			}
			break;
		case 3:
			if (c == '\007') {
				/* Tokenize on semicolons, like we always do */
				char * pch;  /* tokenizer pointer */
				char * save; /* strtok_r pointer */
				char * argv[MAX_ARGS]; /* escape arguments */
				/* Get rid of the front of the buffer */
				strtok_r(state.buffer,"]",&save);
				pch = strtok_r(NULL,";",&save);
				/* argc = Number of arguments, obviously */
				int argc = 0;
				while (pch != NULL) {
					argv[argc] = (char *)pch;
					++argc;
					if (argc > MAX_ARGS) break;
					pch = strtok_r(NULL,";",&save);
				}
				/* Start testing the first argument for what command to use */
				if (argv[0]) {
					if (!strcmp(argv[0], "1")) {
						if (argc > 1) {
							int len = min(TERMINAL_TITLE_SIZE, strlen(argv[1])+1);
							memcpy(terminal_title, argv[1], len);
							terminal_title[len-1] = '\0';
							terminal_title_length = len - 1;
							render_decors();
						}
					} /* Currently, no other options */
				}
				/* Clear out the buffer */
				state.buflen = 0;
				state.escape = 0;
				return;
			} else {
				/* Still escaped */
				ansi_buf_add(c);
			}
			break;
	}
}

void ansi_init(void (*writer)(char), int w, int y, void (*setcolor)(uint32_t, uint32_t),
		void (*setcsr)(int,int), int (*getcsrx)(void), int (*getcsry)(void), void (*setcell)(int,int,uint16_t),
		void (*cls)(int), void (*redraw_csr)(void), void (*scroll_term)(int)) {

	ansi_writer    = writer;
	ansi_set_color = setcolor;
	ansi_set_csr   = setcsr;
	ansi_get_csr_x = getcsrx;
	ansi_get_csr_y = getcsry;
	ansi_set_cell  = setcell;
	ansi_cls       = cls;
	redraw_cursor  = redraw_csr;
	ansi_scroll    = scroll_term;

	/* Terminal Defaults */
	state.fg     = DEFAULT_FG;    /* Light grey */
	state.bg     = DEFAULT_BG;    /* Black */
	state.flags  = DEFAULT_FLAGS; /* Nothing fancy*/
	state.width  = w;
	state.height = y;
	state.local_echo = 1;

	ansi_set_color(state.fg, state.bg);
}

void ansi_print(char * c) {
	uint32_t len = strlen(c);
	for (uint32_t i = 0; i < len; ++i) {
		ansi_put(c[i]);
	}
}

static void render_decors() {
	if (_windowed) {
		if (terminal_title_length) {
			render_decorations(window, ctx, terminal_title);
		} else {
			render_decorations(window, ctx, "Terminal");
		}
	}
}

static inline void term_set_point(uint16_t x, uint16_t y, uint32_t color ) {
	if (_windowed) {
		GFX(ctx, (x+decor_left_width),(y+decor_top_height)) = color;
	} else {
		if (ctx->depth == 32) {
			GFX(ctx, x,y) = color;
		} else if (ctx->depth == 24) {
			ctx->backbuffer[((y) * ctx->width + x) * 3 + 2] = _RED(color);
			ctx->backbuffer[((y) * ctx->width + x) * 3 + 1] = _GRE(color);
			ctx->backbuffer[((y) * ctx->width + x) * 3 + 0] = _BLU(color);
		}
	}
}

/* FreeType text rendering */

FT_Library   library;
FT_Face      face;
FT_Face      face_bold;
FT_Face      face_italic;
FT_Face      face_bold_italic;
FT_Face      face_extra;
FT_GlyphSlot slot;
FT_UInt      glyph_index;


void drawChar(FT_Bitmap * bitmap, int x, int y, uint32_t fg, uint32_t bg) {
	int i, j, p, q;
	int x_max = x + bitmap->width;
	int y_max = y + bitmap->rows;
	for (j = y, q = 0; j < y_max; j++, q++) {
		for ( i = x, p = 0; i < x_max; i++, p++) {
			uint32_t a = _ALP(fg);
			a = (a * bitmap->buffer[q * bitmap->width + p]) / 255;
			uint32_t tmp = rgba(_RED(fg),_GRE(fg),_BLU(fg),a);
			term_set_point(i,j, alpha_blend_rgba(premultiply(bg), premultiply(tmp)));
		}
	}
}

unsigned short * textmemptr = (unsigned short *)0xB8000;
void placech(unsigned char c, int x, int y, int attr) {
	unsigned short *where;
	unsigned att = attr << 8;
	where = textmemptr + (y * 80 + x);
	*where = c | att;
}

/* ANSI-to-VGA */
char vga_to_ansi[] = {
    0, 4, 2, 6, 1, 5, 3, 7,
    8,12,10,14, 9,13,11,15
};

void resize_callback(window_t * window) {
	window_width  = window->width  - decor_left_width - decor_right_width;
	window_height = window->height - decor_top_height - decor_bottom_height;

	reinit_graphics_window(ctx, window);

	reinit();
}

void focus_callback(window_t * window) {
	render_decors();
}

void
term_write_char(
		uint32_t val,
		uint16_t x,
		uint16_t y,
		uint32_t fg,
		uint32_t bg,
		uint8_t flags
		) {

	uint32_t _fg, _bg;

	if (_vga_mode) {
		if (fg > 15) fg = 7;
		if (bg > 15) bg = 0;
		placech(val, x, y, (vga_to_ansi[fg] & 0xF) | (vga_to_ansi[bg] << 4));
		/* Disable / update cursor? We have our own cursor... */
	} else {
		if (fg < PALETTE_COLORS) {
			_fg = term_colors[fg];
			_fg |= 0xFF000000;
		} else {
			_fg = fg;
		}
		if (bg < PALETTE_COLORS) {
			_bg = term_colors[bg];
			if (flags & ANSI_SPECBG) {
				_bg |= 0xFF000000;
			} else {
				_bg |= 0xBB000000;
			}
		} else {
			_bg = bg;
		}
		if (_use_freetype) {
			if (val == 0xFFFF) { return; } /* Unicode, do not redraw here */
			for (uint8_t i = 0; i < char_height; ++i) {
				for (uint8_t j = 0; j < char_width; ++j) {
					term_set_point(x+j,y+i,premultiply(_bg));
				}
			}
			if (flags & ANSI_WIDE) {
				for (uint8_t i = 0; i < char_height; ++i) {
					for (uint8_t j = char_width; j < 2 * char_width; ++j) {
						term_set_point(x+j,y+i,premultiply(_bg));
					}
				}
			}
			if (val < 32 || val == ' ') {
				return;
			}
			int pen_x = x;
			int pen_y = y + char_offset;
			int error;
			FT_Face * _font = NULL;
			
			if (flags & ANSI_EXTRA) {
				_font = &face_extra;
			} else if (flags & ANSI_BOLD && flags & ANSI_ITALIC) {
				_font = &face_bold_italic;
			} else if (flags & ANSI_ITALIC) {
				_font = &face_italic;
			} else if (flags & ANSI_BOLD) {
				_font = &face_bold;
			} else {
				_font = &face;
			}
			glyph_index = FT_Get_Char_Index(*_font, val);
			if (glyph_index == 0) {
				glyph_index = FT_Get_Char_Index(face_extra, val);
				_font = &face_extra;
			}
			error = FT_Load_Glyph(*_font, glyph_index,  FT_LOAD_DEFAULT);
			if (error) {
				char tmp[256];
				sprintf(tmp, "%d", val);
				ansi_print("Error loading glyph: ");
				ansi_print(tmp);
			};
			slot = (*_font)->glyph;
			if (slot->format == FT_GLYPH_FORMAT_OUTLINE) {
				error = FT_Render_Glyph((*_font)->glyph, FT_RENDER_MODE_NORMAL);
				if (error) return;
			}
			drawChar(&slot->bitmap, pen_x + slot->bitmap_left, pen_y - slot->bitmap_top, _fg, _bg);

			if (flags & ANSI_UNDERLINE) {
				for (uint8_t i = 0; i < char_width; ++i) {
					term_set_point(x + i, y + char_offset + 2, _fg);
				}
			}
			if (flags & ANSI_CROSS) {
				for (uint8_t i = 0; i < char_width; ++i) {
					term_set_point(x + i, y + char_offset - 5, _fg);
				}
			}
		} else {
			if (val > 128) {
				val = 4;
			}
			uint8_t * c = number_font[val];
			for (uint8_t i = 0; i < char_height; ++i) {
				for (uint8_t j = 0; j < char_width; ++j) {
					if (c[i] & (1 << (8-j))) {
						term_set_point(x+j,y+i,_fg);
					} else {
						term_set_point(x+j,y+i,_bg);
					}
				}
			}
		}
	}
	needs_redraw = 1;
}

static void cell_set(uint16_t x, uint16_t y, uint16_t c, uint32_t fg, uint32_t bg, uint8_t flags) {
	if (x >= term_width || y >= term_height) return;
	t_cell * cell = (t_cell *)((uintptr_t)term_buffer + (y * term_width + x) * sizeof(t_cell));
	cell->c     = c;
	cell->fg    = fg;
	cell->bg    = bg;
	cell->flags = flags;
}

static void cell_redraw(uint16_t x, uint16_t y) {
	if (x >= term_width || y >= term_height) return;
	t_cell * cell = (t_cell *)((uintptr_t)term_buffer + (y * term_width + x) * sizeof(t_cell));
	if (((uint32_t *)cell)[0] == 0x00000000) {
		term_write_char(' ', x * char_width, y * char_height, DEFAULT_FG, DEFAULT_BG, DEFAULT_FLAGS);
	} else {
		term_write_char(cell->c, x * char_width, y * char_height, cell->fg, cell->bg, cell->flags);
	}
}

static void cell_redraw_inverted(uint16_t x, uint16_t y) {
	if (x >= term_width || y >= term_height) return;
	t_cell * cell = (t_cell *)((uintptr_t)term_buffer + (y * term_width + x) * sizeof(t_cell));
	if (((uint32_t *)cell)[0] == 0x00000000) {
		term_write_char(' ', x * char_width, y * char_height, DEFAULT_BG, DEFAULT_FG, DEFAULT_FLAGS);
	} else {
		term_write_char(cell->c, x * char_width, y * char_height, cell->bg, cell->fg, cell->flags);
	}
}

void draw_cursor() {
	if (!cursor_on) return;
	timer_tick = 0;
	cell_redraw_inverted(csr_x, csr_y);
}

void term_redraw_all() { 
	for (uint16_t y = 0; y < term_height; ++y) {
		for (uint16_t x = 0; x < term_width; ++x) {
			cell_redraw(x,y);
		}
	}
}

void term_scroll(int how_much) {
	if (how_much >= term_height || -how_much >= term_height) {
		term_clear();
		return;
	}
	if (how_much == 0) {
		return;
	}
	if (how_much > 0) {
		/* Shift terminal cells one row up */
		memmove(term_buffer, (void *)((uintptr_t)term_buffer + sizeof(t_cell) * term_width), sizeof(t_cell) * term_width * (term_height - how_much));
		/* Reset the "new" row to clean cells */
		memset((void *)((uintptr_t)term_buffer + sizeof(t_cell) * term_width * (term_height - how_much)), 0x0, sizeof(t_cell) * term_width * how_much);
		if (_vga_mode) {
			/* In VGA mode, we can very quickly just redraw everything */
			term_redraw_all();
		} else {
			/* In graphical modes, we will shift the graphics buffer up as necessary */
			uintptr_t dst, src;
			size_t    siz = char_height * (term_height - how_much) * GFX_W(ctx) * GFX_B(ctx);
			if (_windowed) {
				/* Windowed mode must take borders into account */
				dst = (uintptr_t)ctx->backbuffer + (GFX_W(ctx) * decor_top_height) * GFX_B(ctx);
				src = (uintptr_t)ctx->backbuffer + (GFX_W(ctx) * (decor_top_height + char_height * how_much)) * GFX_B(ctx);
			} else {
				/* While fullscreen mode does not */
				dst = (uintptr_t)ctx->backbuffer;
				src = (uintptr_t)ctx->backbuffer + (GFX_W(ctx) *  char_height * how_much) * GFX_B(ctx);
			}
			/* Perform the shift */
			memmove((void *)dst, (void *)src, siz);
			/* And redraw the new rows */
			for (int i = 0; i < how_much; ++i) {
				for (uint16_t x = 0; x < term_width; ++x) {
					cell_redraw(x, term_height - how_much);
				}
			}
		}
	} else {
		how_much = -how_much;
		/* Shift terminal cells one row up */
		memmove((void *)((uintptr_t)term_buffer + sizeof(t_cell) * term_width), term_buffer, sizeof(t_cell) * term_width * (term_height - how_much));
		/* Reset the "new" row to clean cells */
		memset(term_buffer, 0x0, sizeof(t_cell) * term_width * how_much);
		if (_vga_mode) {
			/* In VGA mode, we can very quickly just redraw everything */
			term_redraw_all();
		} else {
			uintptr_t dst, src;
			size_t    siz = char_height * (term_height - how_much) * GFX_W(ctx) * GFX_B(ctx);
			if (_windowed) {
				src = (uintptr_t)ctx->backbuffer + (GFX_W(ctx) * decor_top_height) * GFX_B(ctx);
				dst = (uintptr_t)ctx->backbuffer + (GFX_W(ctx) * (decor_top_height + char_height * how_much)) * GFX_B(ctx);
			} else {
				/* While fullscreen mode does not */
				src = (uintptr_t)ctx->backbuffer;
				dst = (uintptr_t)ctx->backbuffer + (GFX_W(ctx) *  char_height * how_much) * GFX_B(ctx);
			}
			/* Perform the shift */
			memmove((void *)dst, (void *)src, siz);
			/* And redraw the new rows */
			for (int i = 0; i < how_much; ++i) {
				for (uint16_t x = 0; x < term_width; ++x) {
					cell_redraw(x, i);
				}
			}
		}
	}
}

uint32_t codepoint;
uint32_t unicode_state = 0;

int is_wide(uint32_t codepoint) {
	if (codepoint < 256) return 0;
	return mk_wcwidth_cjk(codepoint) == 2;
}

struct scrollback_row {
	unsigned short width;
	t_cell cells[];
};

#define MAX_SCROLLBACK 10240

list_t * scrollback_list = NULL;

uint32_t scrollback_offset = 0;

void save_scrollback() {
	/* Save the current top row for scrollback */
	return;
	if (!scrollback_list) {
		scrollback_list = list_create();
	}
	if (scrollback_list->length == MAX_SCROLLBACK) {
		free(list_dequeue(scrollback_list));
	}

	struct scrollback_row * row = malloc(sizeof(struct scrollback_row) + sizeof(t_cell) * term_width + 20);
	row->width = term_width;
	for (int i = 0; i < term_width; ++i) {
		t_cell * cell = (t_cell *)((uintptr_t)term_buffer + (i) * sizeof(t_cell));
		memcpy(&row->cells[i], cell, sizeof(t_cell));
	}

	list_insert(scrollback_list, row);
}
#if 0
	if (x >= term_width || y >= term_height) return;
	t_cell * cell = (t_cell *)((uintptr_t)term_buffer + (y * term_width + x) * sizeof(t_cell));
	if (((uint32_t *)cell)[0] == 0x00000000) {
		term_write_char(' ', x * char_width, y * char_height, DEFAULT_FG, DEFAULT_BG, DEFAULT_FLAGS);
	} else {
		term_write_char(cell->c, x * char_width, y * char_height, cell->fg, cell->bg, cell->flags);
	}
#endif

void redraw_scrollback() {
	return;
	if (!scrollback_offset) {
		term_redraw_all();
		return;
	}
	if (scrollback_offset < term_height) {
		for (int i = scrollback_offset; i < term_height; i++) {
			int y = i - scrollback_offset;
			for (int x = 0; x < term_width; ++x) {
				t_cell * cell = (t_cell *)((uintptr_t)term_buffer + (y * term_width + x) * sizeof(t_cell));
				if (((uint32_t *)cell)[0] == 0x00000000) {
					term_write_char(' ', x * char_width, i * char_height, DEFAULT_FG, DEFAULT_BG, DEFAULT_FLAGS);
				} else {
					term_write_char(cell->c, x * char_width, i * char_height, cell->fg, cell->bg, cell->flags);
				}
			}
		}

		node_t * node = scrollback_list->tail;
		for (int i = 0; i < scrollback_offset; ++i) {
			struct scrollback_row * row = (struct scrollback_row *)node->value;

			int y = scrollback_offset - 1 - i;
			int width = row->width;
			if (width > term_width) {
				width = term_width;
			} else {
				for (int x = row->width; x < term_width; ++x) {
					term_write_char(' ', x * char_width, y * char_height, DEFAULT_FG, DEFAULT_BG, DEFAULT_FLAGS);
				}
			}
			for (int x = 0; x < width; ++x) {
				t_cell * cell = &row->cells[x];
				if (((uint32_t *)cell)[0] == 0x00000000) {
					term_write_char(' ', x * char_width, y * char_height, DEFAULT_FG, DEFAULT_BG, DEFAULT_FLAGS);
				} else {
					term_write_char(cell->c, x * char_width, y * char_height, cell->fg, cell->bg, cell->flags);
				}
			}

			node = node->prev;
		}
	} else {
		node_t * node = scrollback_list->tail;
		for (int i = 0; i < scrollback_offset - term_height; ++i) {
			node = node->prev;
		}
		for (int i = scrollback_offset - term_height; i < scrollback_offset; ++i) {
			struct scrollback_row * row = (struct scrollback_row *)node->value;

			int y = scrollback_offset - 1 - i;
			int width = row->width;
			if (width > term_width) {
				width = term_width;
			} else {
				for (int x = row->width; x < term_width; ++x) {
					term_write_char(' ', x * char_width, y * char_height, DEFAULT_FG, DEFAULT_BG, DEFAULT_FLAGS);
				}
			}
			for (int x = 0; x < width; ++x) {
				t_cell * cell = &row->cells[x];
				if (((uint32_t *)cell)[0] == 0x00000000) {
					term_write_char(' ', x * char_width, y * char_height, DEFAULT_FG, DEFAULT_BG, DEFAULT_FLAGS);
				} else {
					term_write_char(cell->c, x * char_width, y * char_height, cell->fg, cell->bg, cell->flags);
				}
			}

			node = node->prev;
		}
	}
}

void term_write(char c) {
	cell_redraw(csr_x, csr_y);
	if (!decode(&unicode_state, &codepoint, (uint8_t)c)) {
		if (codepoint > 0xFFFF) {
			codepoint = '?';
			c = '?';
		}
		if (c == '\n') {
			if (csr_x == 0 && _hold_out) {
				_hold_out = 0;
				return;
			}
			csr_x = 0;
			++csr_y;
			draw_cursor();
		} else if (c == '\007') {
			/* bell */
			for (int i = 0; i < term_height; ++i) {
				for (int j = 0; j < term_width; ++j) {
					cell_redraw_inverted(j, i);
				}
			}
			/* XXX: sleep */
			syscall_nanosleep(0,10);
			term_redraw_all();
		} else if (c == '\r') {
			cell_redraw(csr_x,csr_y);
			csr_x = 0;
		} else if (c == '\b') {
			if (csr_x > 0) {
				--csr_x;
			}
			cell_redraw(csr_x, csr_y);
			draw_cursor();
		} else if (c == '\t') {
			csr_x += (8 - csr_x % 8);
			draw_cursor();
		} else {
			int wide = is_wide(codepoint);
			uint8_t flags = state.flags;
			if (wide && csr_x == term_width - 1) {
				csr_x = 0;
				++csr_y;
			}
			if (wide) {
				flags = flags | ANSI_WIDE;
			}
			cell_set(csr_x,csr_y, codepoint, current_fg, current_bg, flags);
			cell_redraw(csr_x,csr_y);
			csr_x++;
			if (wide && csr_x != term_width) {
				cell_set(csr_x, csr_y, 0xFFFF, current_fg, current_bg, state.flags);
				cell_redraw(csr_x,csr_y);
				cell_redraw(csr_x-1,csr_y);
				csr_x++;
			}
		}
		_hold_out = 0;
		if (csr_x == term_width) {
			_hold_out = 1;
			csr_x = 0;
			++csr_y;
		}
		if (csr_y == term_height) {
			save_scrollback();
			term_scroll(1);
			csr_y = term_height - 1;
		}
	} else if (unicode_state == UTF8_REJECT) {
		unicode_state = 0;
	}
	draw_cursor();
}

void
term_set_csr(int x, int y) {
	cell_redraw(csr_x,csr_y);
	csr_x = x;
	csr_y = y;
	draw_cursor();
}

int
term_get_csr_x() {
	return csr_x;
}

int
term_get_csr_y() {
	return csr_y;
}

void
term_set_csr_show(uint8_t on) {
	cursor_on = on;
}

void term_set_colors(uint32_t fg, uint32_t bg) {
	current_fg = fg;
	current_bg = bg;
}

void term_redraw_cursor() {
	if (term_buffer) {
		draw_cursor();
	}
}

void flip_cursor() {
	static uint8_t cursor_flipped = 0;
	if (cursor_flipped) {
		cell_redraw(csr_x, csr_y);
	} else {
		cell_redraw_inverted(csr_x, csr_y);
	}
	cursor_flipped = 1 - cursor_flipped;
}

void
term_set_cell(int x, int y, uint16_t c) {
	cell_set(x, y, c, current_fg, current_bg, 0);
	cell_redraw(x, y);
}

void term_redraw_cell(int x, int y) {
	if (x < 0 || y < 0 || x >= term_width || y >= term_height) return;
	cell_redraw(x,y);
}

void term_clear(int i) {
	if (i == 2) {
		/* Oh dear */
		csr_x = 0;
		csr_y = 0;
		memset((void *)term_buffer, 0x00, term_width * term_height * sizeof(t_cell));
		if (_windowed) {
			render_decors();
		}
		term_redraw_all();
	} else if (i == 0) {
		for (int x = csr_x; x < term_width; ++x) {
			term_set_cell(x, csr_y, ' ');
		}
		for (int y = csr_y + 1; y < term_height; ++y) {
			for (int x = 0; x < term_width; ++x) {
				term_set_cell(x, y, ' ');
			}
		}
	} else if (i == 1) {
		for (int y = 0; y < csr_y; ++y) {
			for (int x = 0; x < term_width; ++x) {
				term_set_cell(x, y, ' ');
			}
		}
		for (int x = 0; x < csr_x; ++x) {
			term_set_cell(x, csr_y, ' ');
		}
	}
}

char * loadMemFont(char * name, char * ident, size_t * size) {
	if (!_windowed) {
		FILE * f = fopen(name, "r");
		size_t s = 0;
		fseek(f, 0, SEEK_END);
		s = ftell(f);
		fseek(f, 0, SEEK_SET);
		char * font = malloc(s);
		fread(font, s, 1, f);
		fclose(f);
		*size = s;
		return font;
	} else {
		size_t s = 0;
		int error;
		char * font = (char *)syscall_shm_obtain(ident, &s);
		*size = s;
		return font;
	}
}

void setLoaded(int i, int yes) {
	uint32_t color = rgb(255,0,0);
	if (yes == 1) {
		color = rgb(0,255,0);
	}
	if (yes == 2) {
		color = rgb(0,0,255);
	}
	for (uint32_t j = 0; j < 8; ++j) {
		for (uint32_t k = 0; k < 8; ++k) {
			term_set_point(i * 8 + j, k, color);
		}
	}
}

#define INPUT_SIZE 1024
char input_buffer[INPUT_SIZE];
int  input_collected = 0;

void clear_input() {
	memset(input_buffer, 0x0, INPUT_SIZE);
	input_collected = 0;
}

uint32_t child_pid = 0;

int buffer_put(char c) {
	if (c == 8) {
		/* Backspace */
		if (input_collected > 0) {
			input_collected--;
			input_buffer[input_collected] = '\0';
			if (state.local_echo) {
				ansi_print("\010 \010");
			}
		}
		return 0;
	}
	if (c == 3) {
		syscall_send_signal(child_pid, 2);
		return 0;
	}
	if (c < 10 || (c > 10 && c < 32) || c > 126) {
		return 0;
	}
	input_buffer[input_collected] = c;
	if (state.local_echo) {
		ansi_put(c);
	}
	if (input_buffer[input_collected] == '\n') {
		input_collected++;
		return 1;
	}
	input_collected++;
	if (input_collected == INPUT_SIZE) {
		return 1;
	}
	return 0;
}

void handle_input(char c) {
	if (_unbuffered) {
		write(fd_master, &c, 1);
	} else {
		if (buffer_put(c)) {
			write(fd_master, input_buffer, input_collected);
			clear_input();
		}
	}
}

void handle_input_s(char * c) {
	while (*c) {
		handle_input(*c);
		c++;
	}
}

void key_event(int ret, key_event_t * event) {
	if (ret) {
		handle_input(event->key);
	} else {
		if (event->action == KEY_ACTION_UP) return;
		switch (event->keycode) {
			case KEY_ARROW_UP:
				handle_input_s("\033[A");
				break;
			case KEY_ARROW_DOWN:
				handle_input_s("\033[B");
				break;
			case KEY_ARROW_RIGHT:
				handle_input_s("\033[C");
				break;
			case KEY_ARROW_LEFT:
				handle_input_s("\033[D");
				break;
			case KEY_PAGE_UP:
				if (event->modifiers & KEY_MOD_LEFT_SHIFT) {
					int i = 0;
					while (i < 5 && scrollback_list && scrollback_offset < scrollback_list->length) {
						scrollback_offset ++;
						i++;
					}
					redraw_scrollback();
				}
				break;
			case KEY_PAGE_DOWN:
				if (event->modifiers & KEY_MOD_LEFT_SHIFT) {
					int i = 0;
					while (i < 5 && scrollback_list && scrollback_offset != 0) {
						scrollback_offset -= 1;
						i++;
					}
					redraw_scrollback();
				}
				break;
		}
	}
}

void * wait_for_exit(void * garbage) {
	syscall_wait(child_pid);
	/* Clean up */
	exit_application = 1;
	/* Exit */
}

void usage(char * argv[]) {
	printf(
			"Terminal Emulator\n"
			"\n"
			"usage: %s [-b] [-F] [-h]\n"
			"\n"
			" -F --fullscreen \033[3mRun in legacy fullscreen mode.\033[0m\n"
			" -b --bitmap     \033[3mUse the integrated bitmap font.\033[0m\n"
			" -h --help       \033[3mShow this help message.\033[0m\n"
			" -s --scale      \033[3mScale the font in FreeType mode by a given amount.\033[0m\n"
			"\n"
			" This terminal emulator provides basic support for VT220 escapes and\n"
			" XTerm extensions, including 256 color support and font effects.\n",
			argv[0]);
}

void reinit() {
	if (_use_freetype) {
		/* Reset font sizes */

		font_size   = 13;
		char_height = 17;
		char_width  = 8;
		char_offset = 13;

		if (scale_fonts) {
			/* Recalculate scaling */
			font_size   *= font_scaling;
			char_height *= font_scaling;
			char_width  *= font_scaling;
			char_offset *= font_scaling;
		}

		/* Initialize the freetype font pixel sizes */
		FT_Set_Pixel_Sizes(face, font_size, font_size);
		FT_Set_Pixel_Sizes(face_bold, font_size, font_size);
		FT_Set_Pixel_Sizes(face_italic, font_size, font_size);
		FT_Set_Pixel_Sizes(face_bold_italic, font_size, font_size);
		FT_Set_Pixel_Sizes(face_extra, font_size, font_size);
	}

	int old_width  = term_width;
	int old_height = term_height;

	if (_windowed) {
		term_width  = window_width  / char_width;
		term_height = window_height / char_height;
	} else if (_vga_mode) {
		if (!ctx) {
			/* This is only set so that the mouse scaling has only one code-path
			 * so reallocating this for resizes is pointless as VGA mode will never
			 * change its sizes. */
			ctx = malloc(sizeof(gfx_context_t));
			ctx->width = 800;
			ctx->height = 250;
		}
		/* Set the actual terminal size */
		term_width  = 80;
		term_height = 25;
		/* Set these to fake values, primarily for the mouse again */
		char_width  = 1;
		char_height = 1;
	} else {
		/* Non-windowed graphical mode */
		term_width  = ctx->width / char_width;
		term_height = ctx->height / char_height;
	}
	if (term_buffer) {
		t_cell * new_term_buffer = malloc(sizeof(t_cell) * term_width * term_height);

		memset(new_term_buffer, 0x0, sizeof(t_cell) * term_width * term_height);
		for (int row = 0; row < min(old_height, term_height); ++row) {
			for (int col = 0; col < min(old_width, term_width); ++col) {
				t_cell * old_cell = (t_cell *)((uintptr_t)term_buffer + (row * old_width + col) * sizeof(t_cell));
				t_cell * new_cell = (t_cell *)((uintptr_t)new_term_buffer + (row * term_width + col) * sizeof(t_cell));
				*new_cell = *old_cell;
			}
		}
		free(term_buffer);

		term_buffer = new_term_buffer;
	} else {
		term_buffer = malloc(sizeof(t_cell) * term_width * term_height);
		memset(term_buffer, 0x0, sizeof(t_cell) * term_width * term_height);
	}
	ansi_init(&term_write, term_width, term_height, &term_set_colors, &term_set_csr, &term_get_csr_x, &term_get_csr_y, &term_set_cell, &term_clear, &term_redraw_cursor, &term_scroll);

	mouse_x = ctx->width / 2;
	mouse_y = ctx->height / 2;

	if (!_vga_mode) {
		draw_fill(ctx, rgba(0,0,0, 0xbb));
		render_decors();
		term_redraw_all();
	}

	struct winsize w;
	w.ws_row = term_height;
	w.ws_col = term_width;
	ioctl(fd_master, TIOCSWINSZ, &w);

}

#if DEBUG_TERMINAL_WITH_SERIAL
DEFN_SYSCALL1(serial,  44, int);
int serial_fd;
void serial_put(uint8_t c) {
	if (c == '\033') {
		char out[3] = {'\\', 'E', 0};
		write(serial_fd, out, 2);
	} else if (c < 32) {
		char out[5] = {'\\', '0' + ((c / 8) / 8) % 8, '0' + (c / 8) % 8, '0' + c % 8, 0};
		write(serial_fd, out, 4);
	} else {
		char out[2] = {c, 0};
		write(serial_fd, out, 1);
	}
}
#endif

void * handle_incoming(void * garbage) {
	int mfd = syscall_mousedevice();

	unsigned char buf[1024];
	while (!exit_application) {
		struct stat _stat;
		fstat(mfd, &_stat);
		if (exit_application) {
			break;
		}
		if (_windowed) {
			w_keyboard_t * kbd = poll_keyboard();
			if (kbd != NULL) {
				key_event(kbd->ret, &kbd->event);
				free(kbd);
			}
		} else {
			while (_stat.st_size >= sizeof(mouse_device_packet_t)) {
				mouse_device_packet_t * packet = (mouse_device_packet_t *)&buf;
				int r = read(mfd, buf, sizeof(mouse_device_packet_t));
				if (packet->magic != MOUSE_MAGIC) {
					int r = read(mfd, buf, 1);
					goto fail_mouse;
				}
				cell_redraw(((mouse_x / MOUSE_SCALE) * term_width) / ctx->width, ((mouse_y / MOUSE_SCALE) * term_height) / ctx->height);
				/* Apply mouse movement */
				int c, l;
				c = abs(packet->x_difference);
				l = 0;
				while (c >>= 1) {
					l++;
				}
				mouse_x += packet->x_difference * l;
				c = abs(packet->y_difference);
				l = 0;
				while (c >>= 1) {
					l++;
				}
				mouse_y -= packet->y_difference * l;
				if (mouse_x < 0) mouse_x = 0;
				if (mouse_y < 0) mouse_y = 0;
				if (mouse_x >= ctx->width  * MOUSE_SCALE) mouse_x = (ctx->width - char_width)   * MOUSE_SCALE;
				if (mouse_y >= ctx->height * MOUSE_SCALE) mouse_y = (ctx->height - char_height) * MOUSE_SCALE;
				cell_redraw_inverted(((mouse_x / MOUSE_SCALE) * term_width) / ctx->width, ((mouse_y / MOUSE_SCALE) * term_height) / ctx->height);
				fstat(mfd, &_stat);
			}
fail_mouse:
			fstat(0, &_stat);
			if (_stat.st_size) {
				size_t r = read(0, buf, min(_stat.st_size, 1024));
				key_event_t event;
				for (size_t i = 0; i < r; ++i) {
					int ret = kbd_scancode(buf[i], &event);
					key_event(ret, &event);
				}
			}
		}
	}
	pthread_exit(0);
}

int main(int argc, char ** argv) {

	_windowed = 1;
	_use_freetype = 1;
	_login_shell = 0;

	static struct option long_opts[] = {
		{"fullscreen", no_argument,       0, 'F'},
		{"bitmap",     no_argument,       0, 'b'},
		{"vga",        no_argument,       0, 'V'},
		{"login",      no_argument,       0, 'l'},
		{"help",       no_argument,       0, 'h'},
		{"kernel",     no_argument,       0, 'k'},
		{"scale",      required_argument, 0, 's'},
		{"geometry",   required_argument, 0, 'g'},
		{0,0,0,0}
	};

	/* Read some arguments */
	int index, c;
	while ((c = getopt_long(argc, argv, "bhFVlks:g:", long_opts, &index)) != -1) {
		if (!c) {
			if (long_opts[index].flag == 0) {
				c = long_opts[index].val;
			}
		}
		switch (c) {
			case 'k':
				_force_kernel = 1;
				break;
			case 'l':
				_login_shell = 1;
				break;
			case 'V':
				_use_freetype = 0;
				_vga_mode = 1;
				_windowed = 0;
				break;
			case 'F':
				_windowed = 0;
				break;
			case 'b':
				_use_freetype = 0;
				break;
			case 'h':
				usage(argv);
				return 0;
				break;
			case 's':
				scale_fonts = 1;
				font_scaling = atof(optarg);
				break;
			case 'g':
				{
					char * c = strstr(optarg, "x");
					if (c) {
						*c = '\0';
						c++;
						window_width  = atoi(optarg);
						window_height = atoi(c);
					}
				}
				break;
			case '?':
				break;
			default:
				break;
		}
	}

#if DEBUG_TERMINAL_WITH_SERIAL
	serial_fd = syscall_serial(0x3F8);
#endif

	putenv("TERM=toaru");

	if (_windowed) {
		/* Initialize the windowing library */
		setup_windowing();

		int x = 20, y = 20;

		/* Create the window */
		window = window_create(x,y, window_width + decor_left_width + decor_right_width, window_height + decor_top_height + decor_bottom_height);
		resize_window_callback = resize_callback;
		focus_changed_callback = focus_callback;

		window_enable_alpha(window);

		/* Initialize the decoration library */
		init_decorations();

		/* Initialize the graphics context */
		ctx = init_graphics_window(window);

		/* Clear to black */
		draw_fill(ctx, rgb(0,0,0));
	} else if (_vga_mode) {
		/* Set some important VGA options */
		int temp = 0xFFFF;
		outb(14, 0x3D4);
		outb(temp >> 8, 0x3D5);
		outb(15, 0x3D4);
		outb(temp, 0x3D5);
	} else {
		ctx = init_graphics_fullscreen();
	}

	if (_use_freetype) {
		int error;
		error = FT_Init_FreeType(&library);
		if (error) return 1;

		char * font = NULL;
		size_t s;

		setLoaded(0,0);
		setLoaded(1,0);
		setLoaded(2,0);
		setLoaded(3,0);
		setLoaded(4,0);

		setLoaded(0,2);
		font = loadMemFont("/usr/share/fonts/DejaVuSansMono.ttf", WINS_SERVER_IDENTIFIER ".fonts.monospace", &s);
		error = FT_New_Memory_Face(library, font, s, 0, &face); if (error) return 1;
		setLoaded(0,1);

		setLoaded(1,2);
		font = loadMemFont("/usr/share/fonts/DejaVuSansMono-Bold.ttf", WINS_SERVER_IDENTIFIER ".fonts.monospace.bold", &s);
		error = FT_New_Memory_Face(library, font, s, 0, &face_bold); if (error) return 1;
		setLoaded(1,1);

		setLoaded(2,2);
		font = loadMemFont("/usr/share/fonts/DejaVuSansMono-Oblique.ttf", WINS_SERVER_IDENTIFIER ".fonts.monospace.italic", &s);
		error = FT_New_Memory_Face(library, font, s, 0, &face_italic); if (error) return 1;
		setLoaded(2,1);

		setLoaded(3,2);
		font = loadMemFont("/usr/share/fonts/DejaVuSansMono-BoldOblique.ttf", WINS_SERVER_IDENTIFIER ".fonts.monospace.bolditalic", &s);
		error = FT_New_Memory_Face(library, font, s, 0, &face_bold_italic); if (error) return 1;
		setLoaded(3,1);

		setLoaded(4,2);
		error = FT_New_Face(library, "/usr/share/fonts/VLGothic.ttf", 0, &face_extra);
		setLoaded(4,1);
	}

	syscall_openpty(&fd_master, &fd_slave, NULL, NULL, NULL);

	reinit();

	int pid = getpid();
	uint32_t f = fork();

	if (getpid() != pid) {
		syscall_dup2(fd_slave, 0);
		syscall_dup2(fd_slave, 1);
		syscall_dup2(fd_slave, 2);

		if (argv[optind] != NULL) {
			char * tokens[] = {argv[optind], NULL};
			int i = execvp(tokens[0], tokens);
			printf("Failed to execute requested startup application `%s`!\n", argv[optind]);
			printf("Your system is now unusable, and a restart will not be attempted.\n");
			syscall_print("core-tests : FATAL : Failed to execute requested startup binary.\n");
		} else {
			/*
			 * TODO: Check the public-readable passwd file to select which shell to run
			 */
			if (_login_shell) {
				char * tokens[] = {"/bin/login",NULL};
				int i = execvp(tokens[0], tokens);
			} else {
				char * tokens[] = {"/bin/sh",NULL};
				int i = execvp(tokens[0], tokens);
			}
		}

		exit_application = 1;

		return 1;
	} else {

		if (!_windowed || _force_kernel) {
			/* Request kernel output to this terminal */
			//syscall_system_function(4, (char **)fd_slave);
		}

		child_pid = f;

		pthread_t wait_for_exit_thread;
		pthread_create(&wait_for_exit_thread, NULL, wait_for_exit, NULL);

		pthread_t handle_incoming_thread;
		pthread_create(&handle_incoming_thread, NULL, handle_incoming, NULL);

		unsigned char buf[1024];
		struct stat _stat;
		while (!exit_application) {
			timer_tick++;
			if (timer_tick == (_windowed ? TIMER_TICK * 10 : TIMER_TICK)) {
				timer_tick = 0;
				flip_cursor();
			}

			fstat(fd_master, &_stat);
			if (_stat.st_size) {
				int r = read(fd_master, buf, min(_stat.st_size, 1024));
				for (uint32_t i = 0; i < r; ++i) {
					ansi_put(buf[i]);
#if DEBUG_TERMINAL_WITH_SERIAL
					serial_put(buf[i]);
#endif
				}
			}
			syscall_yield();
		}

	}

	if (_windowed) {
		teardown_windowing();
	}

	return 0;
}
