/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014-2018 K. Lange
 *
 * Portable library for terminal emulation.
 */

#ifdef _KERNEL_
# include <kernel/system.h>
# include <kernel/types.h>
# include <kernel/logging.h>
static void _spin_lock(volatile int * foo) { return; }
static void _spin_unlock(volatile int * foo) { return; }
# define rgba(r,g,b,a) (((uint32_t)a * 0x1000000) + ((uint32_t)r * 0x10000) + ((uint32_t)g * 0x100) + ((uint32_t)b * 0x1))
# define rgb(r,g,b) rgba(r,g,b,0xFF)
# define atof(i) (0.0f)
#include <toaru/termemu.h>
#else
#include <stdlib.h>

#include <math.h>
#include <string.h>
#include <stdio.h>

#include <toaru/graphics.h>
#include <toaru/termemu.h>

#include <toaru/spinlock.h>
#define _spin_lock spin_lock
#define _spin_unlock spin_unlock
#endif

#define MAX_ARGS 1024

static wchar_t box_chars[] = L"▒␉␌␍␊°±␤␋┘┐┌└┼⎺⎻─⎼⎽├┤┴┬│≤≥";

/* Returns the lower of two shorts */
static uint16_t min(uint16_t a, uint16_t b) {
	return (a < b) ? a : b;
}

/* Returns the higher of two shorts */
static uint16_t max(uint16_t a, uint16_t b) {
	return (a > b) ? a : b;
}

/* Write the contents of the buffer, as they were all non-escaped data. */
static void ansi_dump_buffer(term_state_t * s) {
	for (int i = 0; i < s->buflen; ++i) {
		s->callbacks->writer(s->buffer[i]);
	}
}

/* Add to the internal buffer for the ANSI parser */
static void ansi_buf_add(term_state_t * s, char c) {
	if (s->buflen >= TERM_BUF_LEN-1) return;
	s->buffer[s->buflen] = c;
	s->buflen++;
	s->buffer[s->buflen] = '\0';
}

static int to_eight(uint32_t codepoint, char * out) {
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


static void _ansi_put(term_state_t * s, char c) {
	term_callbacks_t * callbacks = s->callbacks;
	switch (s->escape) {
		case 0:
			/* We are not escaped, check for escape character */
			if (c == ANSI_ESCAPE) {
				/*
				 * Enable escape mode, setup a buffer,
				 * fill the buffer, get out of here.
				 */
				s->escape    = 1;
				s->buflen    = 0;
				ansi_buf_add(s, c);
				return;
			} else if (c == 0) {
				return;
			} else {
				if (s->box && c >= 'a' && c <= 'z') {
					char buf[7];
					char *w = (char *)&buf;
					to_eight(box_chars[c-'a'], w);
					while (*w) {
						callbacks->writer(*w);
						w++;
					}
				} else {
					callbacks->writer(c);
				}
			}
			break;
		case 1:
			/* We're ready for [ */
			if (c == ANSI_BRACKET) {
				s->escape = 2;
				ansi_buf_add(s, c);
			} else if (c == ANSI_BRACKET_RIGHT) {
				s->escape = 3;
				ansi_buf_add(s, c);
			} else if (c == ANSI_OPEN_PAREN) {
				s->escape = 4;
				ansi_buf_add(s, c);
			} else if (c == 'T') {
				s->escape = 5;
				ansi_buf_add(s, c);
			} else if (c == '7') {
				s->escape = 0;
				s->buflen = 0;
				s->save_x = callbacks->get_csr_x();
				s->save_y = callbacks->get_csr_y();
			} else if (c == '8') {
				s->escape = 0;
				s->buflen = 0;
				callbacks->set_csr(s->save_x, s->save_y);
			} else {
				/* This isn't a bracket, we're not actually escaped!
				 * Get out of here! */
				ansi_dump_buffer(s);
				callbacks->writer(c);
				s->escape = 0;
				s->buflen = 0;
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
				strtok_r(s->buffer,"[",&save);
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
										callbacks->redraw_cursor();
										break;
									default:
										break;
								}
							}
						}
						break;
					case ANSI_SCP:
						{
							s->save_x = callbacks->get_csr_x();
							s->save_y = callbacks->get_csr_y();
						}
						break;
					case ANSI_RCP:
						{
							callbacks->set_csr(s->save_x, s->save_y);
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
								s->bg = 8 + (arg - 100);
								s->flags |= ANSI_SPECBG;
							} else if (arg >= 90 && arg < 100) {
								/* Bright foreground */
								s->fg = 8 + (arg - 90);
							} else if (arg >= 40 && arg < 49) {
								/* Set background */
								s->bg = arg - 40;
								s->flags |= ANSI_SPECBG;
							} else if (arg == 49) {
								s->bg = TERM_DEFAULT_BG;
								s->flags &= ~ANSI_SPECBG;
							} else if (arg >= 30 && arg < 39) {
								/* Set Foreground */
								s->fg = arg - 30;
							} else if (arg == 39) {
								/* Default Foreground */
								s->fg = 7;
							} else if (arg == 24) {
								/* Underline off */
								s->flags &= ~ANSI_UNDERLINE;
							} else if (arg == 23) {
								/* Oblique off */
								s->flags &= ~ANSI_ITALIC;
							} else if (arg == 21 || arg == 22) {
								/* Bold off */
								s->flags &= ~ANSI_BOLD;
							} else if (arg == 9) {
								/* X-OUT */
								s->flags |= ANSI_CROSS;
							} else if (arg == 7) {
								/* INVERT: Swap foreground / background */
								uint32_t temp = s->fg;
								s->fg = s->bg;
								s->bg = temp;
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
										s->bg = c;
										s->flags |= ANSI_SPECBG;
									} else if (atoi(argv[i-1]) == 38) {
										s->fg = c;
									}
									i += 4;
								}
							} else if (arg == 5) {
								/* Supposed to be blink; instead, support X-term 256 colors */
								if (i == 0) { break; }
								if (i < argc) {
									if (atoi(argv[i-1]) == 48) {
										/* Background to i+1 */
										s->bg = atoi(argv[i+1]);
										s->flags |= ANSI_SPECBG;
									} else if (atoi(argv[i-1]) == 38) {
										/* Foreground to i+1 */
										s->fg = atoi(argv[i+1]);
									}
									++i;
								}
							} else if (arg == 4) {
								/* UNDERLINE */
								s->flags |= ANSI_UNDERLINE;
							} else if (arg == 3) {
								/* ITALIC: Oblique */
								s->flags |= ANSI_ITALIC;
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
										s->bg = c;
										s->flags |= ANSI_SPECBG;
									} else if (atoi(argv[i-1]) == 38) {
										/* Foreground to i+1 */
										s->fg = c;
									}
									i += 3;
								}
							} else if (arg == 1) {
								/* BOLD/BRIGHT: Brighten the output color */
								s->flags |= ANSI_BOLD;
							} else if (arg == 0) {
								/* Reset everything */
								s->fg = TERM_DEFAULT_FG;
								s->bg = TERM_DEFAULT_BG;
								s->flags = TERM_DEFAULT_FLAGS;
							}
						}
						break;
					case ANSI_SHOW:
						if (argc > 0) {
							if (!strcmp(argv[0], "?1049")) {
								if (callbacks->switch_buffer) callbacks->switch_buffer(1);
							} else if (!strcmp(argv[0], "?1000")) {
								s->mouse_on |= TERMEMU_MOUSE_ENABLE;
							} else if (!strcmp(argv[0], "?1002")) {
								s->mouse_on |= TERMEMU_MOUSE_DRAG;
							} else if (!strcmp(argv[0], "?1006")) {
								s->mouse_on |= TERMEMU_MOUSE_SGR;
							} else if (!strcmp(argv[0], "?25")) {
								callbacks->set_csr_on(1);
							} else if (!strcmp(argv[0], "?2004")) {
								s->paste_mode = 1;
							}
						}
						break;
					case ANSI_HIDE:
						if (argc > 0) {
							if (!strcmp(argv[0], "?1049")) {
								if (callbacks->switch_buffer) callbacks->switch_buffer(0);
							} else if (!strcmp(argv[0], "?1000")) {
								s->mouse_on &= ~TERMEMU_MOUSE_ENABLE;
							} else if (!strcmp(argv[0], "?1002")) {
								s->mouse_on &= ~TERMEMU_MOUSE_DRAG;
							} else if (!strcmp(argv[0],"?1006")) {
								s->mouse_on &= ~TERMEMU_MOUSE_SGR;
							} else if (!strcmp(argv[0], "?25")) {
								callbacks->set_csr_on(0);
							} else if (!strcmp(argv[0], "?2004")) {
								s->paste_mode = 0;
							}
						}
						break;
					case ANSI_CUF:
						{
							int i = 1;
							if (argc) {
								i = atoi(argv[0]);
							}
							callbacks->set_csr(min(callbacks->get_csr_x() + i, s->width - 1), callbacks->get_csr_y());
						}
						break;
					case ANSI_CUU:
						{
							int i = 1;
							if (argc) {
								i = atoi(argv[0]);
							}
							callbacks->set_csr(callbacks->get_csr_x(), max(callbacks->get_csr_y() - i, 0));
						}
						break;
					case ANSI_CUD:
						{
							int i = 1;
							if (argc) {
								i = atoi(argv[0]);
							}
							callbacks->set_csr(callbacks->get_csr_x(), min(callbacks->get_csr_y() + i, s->height - 1));
						}
						break;
					case ANSI_CUB:
						{
							int i = 1;
							if (argc) {
								i = atoi(argv[0]);
							}
							callbacks->set_csr(max(callbacks->get_csr_x() - i,0), callbacks->get_csr_y());
						}
						break;
					case ANSI_CHA:
						if (argc < 1) {
							callbacks->set_csr(0,callbacks->get_csr_y());
						} else {
							callbacks->set_csr(min(max(atoi(argv[0]), 1), s->width) - 1, callbacks->get_csr_y());
						}
						break;
					case ANSI_CUP:
						if (argc < 2) {
							callbacks->set_csr(0,0);
						} else {
							callbacks->set_csr(min(max(atoi(argv[1]), 1), s->width) - 1, min(max(atoi(argv[0]), 1), s->height) - 1);
						}
						break;
					case ANSI_ED:
						if (argc < 1) {
							callbacks->cls(0);
						} else {
							callbacks->cls(atoi(argv[0]));
						}
						break;
					case ANSI_EL:
						{
							int what = 0, x = 0, y = 0;
							if (argc >= 1) {
								what = atoi(argv[0]);
							}
							if (what == 0) {
								x = callbacks->get_csr_x();
								y = s->width;
							} else if (what == 1) {
								x = 0;
								y = callbacks->get_csr_x();
							} else if (what == 2) {
								x = 0;
								y = s->width;
							}
							for (int i = x; i < y; ++i) {
								callbacks->set_cell(i, callbacks->get_csr_y(), ' ');
							}
						}
						break;
					case ANSI_DSR:
						{
							char out[24];
							sprintf(out, "\033[%d;%dR", callbacks->get_csr_y() + 1, callbacks->get_csr_x() + 1);
							callbacks->input_buffer_stuff(out);
						}
						break;
					case ANSI_SU:
						{
							int how_many = 1;
							if (argc > 0) {
								how_many = atoi(argv[0]);
							}
							callbacks->scroll(how_many);
						}
						break;
					case ANSI_SD:
						{
							int how_many = 1;
							if (argc > 0) {
								how_many = atoi(argv[0]);
							}
							callbacks->scroll(-how_many);
						}
						break;
					case ANSI_IL:
						{
							int how_many = 1;
							if (argc > 0) {
								how_many = atoi(argv[0]);
							}
							callbacks->insert_delete_lines(how_many);
						}
						break;
					case ANSI_DL:
						{
							int how_many = 1;
							if (argc > 0) {
								how_many = atoi(argv[0]);
							}
							callbacks->insert_delete_lines(-how_many);
						}
						break;
					case 'X':
						{
							int how_many = 1;
							if (argc > 0) {
								how_many = atoi(argv[0]);
							}
							for (int i = 0; i < how_many; ++i) {
								callbacks->writer(' ');
							}
						}
						break;
					case 'd':
						if (argc < 1) {
							callbacks->set_csr(callbacks->get_csr_x(), 0);
						} else {
							callbacks->set_csr(callbacks->get_csr_x(), atoi(argv[0]) - 1);
						}
						break;
					default:
						/* Meh */
						break;
				}
				/* Set the states */
				if (s->flags & ANSI_BOLD && s->fg < 9) {
					callbacks->set_color(s->fg % 8 + 8, s->bg);
				} else {
					callbacks->set_color(s->fg, s->bg);
				}
				/* Clear out the buffer */
				s->buflen = 0;
				s->escape = 0;
				return;
			} else {
				/* Still escaped */
				ansi_buf_add(s, c);
			}
			break;
		case 3:
			if (c == '\007') {
				/* Tokenize on semicolons, like we always do */
				char * pch;  /* tokenizer pointer */
				char * save; /* strtok_r pointer */
				char * argv[MAX_ARGS]; /* escape arguments */
				/* Get rid of the front of the buffer */
				strtok_r(s->buffer,"]",&save);
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
							callbacks->set_title(argv[1]);
						}
					} /* Currently, no other options */
				}
				/* Clear out the buffer */
				s->buflen = 0;
				s->escape = 0;
				return;
			} else {
				/* Still escaped */
				if (c == '\n' || s->buflen == 255) {
					ansi_dump_buffer(s);
					callbacks->writer(c);
					s->buflen = 0;
					s->escape = 0;
					return;
				}
				ansi_buf_add(s, c);
			}
			break;
		case 4:
			if (c == '0') {
				s->box = 1;
			} else if (c == 'B') {
				s->box = 0;
			} else {
				ansi_dump_buffer(s);
				callbacks->writer(c);
			}
			s->escape = 0;
			s->buflen = 0;
			break;
		case 5:
			if (c == 'q') {
				char out[24];
				sprintf(out, "\033T%d;%dq", callbacks->get_cell_width(), callbacks->get_cell_height());
				callbacks->input_buffer_stuff(out);
				s->escape = 0;
				s->buflen = 0;
			} else if (c == 's') {
				s->img_collected = 0;
				s->escape = 6;
				s->img_size = sizeof(uint32_t) * callbacks->get_cell_width() * callbacks->get_cell_height();
				if (!s->img_data) {
					s->img_data = malloc(s->img_size);
				}
				memset(s->img_data, 0x00, s->img_size);
			} else {
				ansi_dump_buffer(s);
				callbacks->writer(c);
				s->escape = 0;
				s->buflen = 0;
			}
			break;
		case 6:
			s->img_data[s->img_collected++] = c;
			if (s->img_collected == s->img_size) {
				callbacks->set_cell_contents(callbacks->get_csr_x(), callbacks->get_csr_y(), s->img_data);
				callbacks->set_csr(min(callbacks->get_csr_x() + 1, s->width - 1), callbacks->get_csr_y());
				s->escape = 0;
				s->buflen = 0;
			}
			break;
	}
}

void ansi_put(term_state_t * s, char c) {
	_spin_lock(&s->lock);
	_ansi_put(s, c);
	_spin_unlock(&s->lock);
}

term_state_t * ansi_init(term_state_t * s, int w, int y, term_callbacks_t * callbacks_in) {

	if (!s) {
		s = malloc(sizeof(term_state_t));
	}

	memset(s, 0x00, sizeof(term_state_t));

	/* Terminal Defaults */
	s->fg     = TERM_DEFAULT_FG;    /* Light grey */
	s->bg     = TERM_DEFAULT_BG;    /* Black */
	s->flags  = TERM_DEFAULT_FLAGS; /* Nothing fancy*/
	s->width  = w;
	s->height = y;
	s->box    = 0;
	s->callbacks = callbacks_in;
	s->callbacks->set_color(s->fg, s->bg);
	s->mouse_on = 0;

	return s;
}
