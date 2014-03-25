/*
 * Portable library for terminal emulation.
 */

#include "termemu.h"

#include <stdlib.h>
#include <math.h>
#include <syscall.h>

#include "lib/graphics.h"

#define USE_BELL 0
#define MAX_ARGS 1024

static wchar_t box_chars[] = L"▒␉␌␍␊°±␤␋┘┐┌└┼⎺⎻─⎼⎽├┤┴┬│≤≥";

static int volatile lock = 0;

static void spin_lock(int volatile * lock) {
	while(__sync_lock_test_and_set(lock, 0x01)) {
		syscall_yield();
	}
}

static void spin_unlock(int volatile * lock) {
	__sync_lock_release(lock);
}

/* Returns the lower of two shorts */
static uint16_t min(uint16_t a, uint16_t b) {
	return (a < b) ? a : b;
}

/* Returns the higher of two shorts */
static uint16_t max(uint16_t a, uint16_t b) {
	return (a > b) ? a : b;
}

/* State machine status */
static term_state_t state;

static void (*ansi_writer)(char) = NULL;
static void (*ansi_set_color)(uint32_t, uint32_t) = NULL;
static void (*ansi_set_csr)(int,int) = NULL;
static int  (*ansi_get_csr_x)(void) = NULL;
static int  (*ansi_get_csr_y)(void) = NULL;
static void (*ansi_set_cell)(int,int,uint16_t) = NULL;
static void (*ansi_cls)(int) = NULL;
static void (*ansi_scroll)(int) = NULL;
static void (*ansi_redraw_cursor)(void) = NULL;
static void (*ansi_input_buffer_stuff)(char *) = NULL;
static void (*ansi_set_font_size)(float) = NULL;
static void (*ansi_set_title)(char *) = NULL;

/* Write the contents of the buffer, as they were all non-escaped data. */
static void ansi_dump_buffer() {
	for (int i = 0; i < state.buflen; ++i) {
		ansi_writer(state.buffer[i]);
	}
}

/* Add to the internal buffer for the ANSI parser */
static void ansi_buf_add(char c) {
	state.buffer[state.buflen] = c;
	state.buflen++;
	state.buffer[state.buflen] = '\0';
}

term_state_t * ansi_state() {
	return &state;
}

static void _ansi_put(char c);

void ansi_put(char c) {
	spin_lock(&lock);
	_ansi_put(c);
	spin_unlock(&lock);
}

static int to_eight(uint16_t codepoint, uint8_t * out) {
	memset(out, 0x00, 4);

	if (codepoint < 0x0080) {
		out[0] = (uint8_t)codepoint;
	} else if (codepoint < 0x0800) {
		out[0] = 0xC0 | (codepoint >> 6);
		out[1] = 0x80 | (codepoint & 0x3F);
	} else {
		out[0] = 0xE0 | (codepoint >> 12);
		out[1] = 0x80 | ((codepoint >> 6) & 0x3F);
		out[2] = 0x80 | (codepoint & 0x3F);
	}

	return strlen(out);
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
			} else if (c == 0) {
				return;
			} else {
				if (state.box && c >= 'a' && c <= 'z') {
					char buf[4];
					char *w = (char *)&buf;
					to_eight(box_chars[c-'a'], w);
					while (*w) {
						ansi_writer(*w);
						w++;
					}
				} else {
					ansi_writer(c);
				}
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
			} else if (c == ANSI_OPEN_PAREN) {
				state.escape = 4;
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
										ansi_redraw_cursor();
										break;
									case 1555:
										if (argc > 1) {
											ansi_set_font_size(atof(argv[1]));
										}
										break;
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
								state.bg = TERM_DEFAULT_BG;
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
								state.fg = TERM_DEFAULT_FG;
								state.bg = TERM_DEFAULT_BG;
								state.flags = TERM_DEFAULT_FLAGS;
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
							sprintf(out, "\033[%d;%dR", ansi_get_csr_y + 1, ansi_get_csr_x + 1);
							ansi_input_buffer_stuff(out);
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
							ansi_set_title(argv[1]);
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
		case 4:
			if (c == '0') {
				state.box = 1;
			} else if (c == 'B') {
				state.box = 0;
			} else {
				ansi_dump_buffer();
				ansi_writer(c);
			}
			state.escape = 0;
			state.buflen = 0;
			break;
	}
}

void ansi_init(void (*writer)(char), int w, int y, void (*setcolor)(uint32_t, uint32_t),
		void (*setcsr)(int,int), int (*getcsrx)(void), int (*getcsry)(void), void (*setcell)(int,int,uint16_t),
		void (*cls)(int), void (*redraw_csr)(void), void (*scroll_term)(int), void (*stuff)(char *),
		void (*fontsize)(float), void (*settitle)(char *)) {

	ansi_writer    = writer;
	ansi_set_color = setcolor;
	ansi_set_csr   = setcsr;
	ansi_get_csr_x = getcsrx;
	ansi_get_csr_y = getcsry;
	ansi_set_cell  = setcell;
	ansi_cls       = cls;
	ansi_scroll    = scroll_term;

	ansi_redraw_cursor  = redraw_csr;
	ansi_input_buffer_stuff = stuff;
	ansi_set_font_size = fontsize;
	ansi_set_title  = settitle;

	/* Terminal Defaults */
	state.fg     = TERM_DEFAULT_FG;    /* Light grey */
	state.bg     = TERM_DEFAULT_BG;    /* Black */
	state.flags  = TERM_DEFAULT_FLAGS; /* Nothing fancy*/
	state.width  = w;
	state.height = y;
	state.box    = 0;

	ansi_set_color(state.fg, state.bg);
}
