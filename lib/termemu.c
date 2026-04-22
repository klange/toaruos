/**
 * @brief Terrible little ANSI escape parser.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014-2018 K. Lange
 */
#include <stdlib.h>

#include <math.h>
#include <string.h>
#include <stdio.h>
#include <wchar.h>
#include <sys/time.h>

#include <toaru/graphics.h>
#include <toaru/termemu.h>
#include <toaru/list.h>
#include <toaru/decodeutf8.h>

#include <toaru/spinlock.h>

#define MAX_ARGS 1024

static void term_scroll(term_state_t * state, int how_much);
static void term_set_colors(term_state_t *s, uint32_t fg, uint32_t bg);
static void term_set_csr_show(term_state_t * state, int on);
static void term_set_csr(term_state_t * state, int x, int y);
static void term_write(term_state_t * state, char c);
static void term_insert_delete_lines(term_state_t * state, int how_many);
static void term_set_cell(term_state_t * state, int x, int y, uint32_t c);
static void term_mirror_set(term_state_t * state, uint16_t x, uint16_t y, uint32_t val, uint32_t fg, uint32_t bg, uint32_t flags);
static void term_mirror_copy(term_state_t * state, uint16_t x, uint16_t y, term_cell_t * from);
static void term_mirror_copy_inverted(term_state_t * state, uint16_t x, uint16_t y, term_cell_t * from);
static void term_cell_set(term_state_t * state, uint16_t x, uint16_t y, uint32_t c, uint32_t fg, uint32_t bg, uint32_t flags);
static void term_draw_cursor(term_state_t * state);

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
		term_write(s, s->buffer[i]);
	}
}

/* Add to the internal buffer for the ANSI parser */
static void ansi_buf_add(term_state_t * s, char c) {
	if (s->buflen >= TERM_BUF_LEN-1) return;
	s->buffer[s->buflen] = c;
	s->buflen++;
	s->buffer[s->buflen] = '\0';
}

int termemu_to_eight(uint32_t codepoint, char * out) {
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
					termemu_to_eight(box_chars[c-'a'], w);
					while (*w) {
						term_write(s, *w);
						w++;
					}
				} else {
					term_write(s, c);
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
				s->save_x = s->x;
				s->save_y = s->y;
			} else if (c == '8') {
				s->escape = 0;
				s->buflen = 0;
				term_set_csr(s, s->save_x, s->save_y);
			} else if (c == 'c') {
				/*
				 * "Full reset"
				 * First, reset anything we own. Then call a callback
				 * to inform the app to reset things it owns (buffers, cursors).
				 */
				termemu_full_reset(s);
			} else {
				/* This isn't a bracket, we're not actually escaped!
				 * Get out of here! */
				ansi_dump_buffer(s);
				term_write(s, c);
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
										term_draw_cursor(s);
										break;
									default:
										break;
								}
							}
						}
						break;
					case ANSI_SCP:
						s->save_x = s->x;
						s->save_y = s->y;
						break;
					case ANSI_RCP:
						{
							term_set_csr(s, s->save_x, s->save_y);
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
							} else if (arg == 29) {
								/* not X-OUT */
								s->flags &= ~ANSI_CROSS;
							} else if (arg == 7) {
								/* INVERT: Swap foreground / background */
								s->flags |= ANSI_INVERT;
							} else if (arg == 27) {
								s->flags &= ~ANSI_INVERT;
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
								termemu_switch_buffer(s, 1);
							} else if (!strcmp(argv[0], "?1000")) {
								s->mouse_on |= TERMEMU_MOUSE_ENABLE;
							} else if (!strcmp(argv[0], "?1002")) {
								s->mouse_on |= TERMEMU_MOUSE_DRAG;
							} else if (!strcmp(argv[0], "?1006")) {
								s->mouse_on |= TERMEMU_MOUSE_SGR;
							} else if (!strcmp(argv[0], "?1007")) {
								s->mouse_on |= TERMEMU_MOUSE_ALTSCRL;
							} else if (!strcmp(argv[0], "?25")) {
								term_set_csr_show(s, 1);
							} else if (!strcmp(argv[0], "?2004")) {
								s->paste_mode = 1;
							}
							if (callbacks->state_change) callbacks->state_change(s);
						}
						break;
					case ANSI_HIDE:
						if (argc > 0) {
							if (!strcmp(argv[0], "?1049")) {
								termemu_switch_buffer(s, 0);
							} else if (!strcmp(argv[0], "?1000")) {
								s->mouse_on &= ~TERMEMU_MOUSE_ENABLE;
							} else if (!strcmp(argv[0], "?1002")) {
								s->mouse_on &= ~TERMEMU_MOUSE_DRAG;
							} else if (!strcmp(argv[0],"?1006")) {
								s->mouse_on &= ~TERMEMU_MOUSE_SGR;
							} else if (!strcmp(argv[0], "?1007")) {
								s->mouse_on &= ~TERMEMU_MOUSE_ALTSCRL;
							} else if (!strcmp(argv[0], "?25")) {
								term_set_csr_show(s, 0);
							} else if (!strcmp(argv[0], "?2004")) {
								s->paste_mode = 0;
							}
							if (callbacks->state_change) callbacks->state_change(s);
						}
						break;
					case ANSI_CUF:
						{
							int i = argc ? atoi(argv[0]) : 1;
							term_set_csr(s, min(s->x + i, s->width - 1), s->y);
						}
						break;
					case ANSI_CUU:
						{
							int i = argc ? atoi(argv[0]) : 1;
							term_set_csr(s, s->x, max(s->y - i, 0));
						}
						break;
					case ANSI_CUD:
						{
							int i = argc ? atoi(argv[0]) : 1;
							term_set_csr(s, s->x, min(s->y + i, s->height - 1));
						}
						break;
					case ANSI_CUB:
						{
							int i = argc ? atoi(argv[0]) : 1;
							term_set_csr(s, max(s->x - i,0), s->y);
						}
						break;
					case ANSI_CHA:
						if (argc < 1) {
							term_set_csr(s, 0, s->y);
						} else {
							term_set_csr(s, min(max(atoi(argv[0]), 1), s->width) - 1, s->y);
						}
						break;
					case ANSI_CUP:
						if (argc < 2) {
							term_set_csr(s, 0,0);
						} else {
							term_set_csr(s, min(max(atoi(argv[1]), 1), s->width) - 1, min(max(atoi(argv[0]), 1), s->height) - 1);
						}
						break;
					case ANSI_ED:
						termemu_clear(s, argc < 1 ? 0 : atoi(argv[0]));
						break;
					case ANSI_EL:
						{
							int what = 0, x = 0, y = 0;
							if (argc >= 1) {
								what = atoi(argv[0]);
							}
							if (what == 0) {
								x = s->x;
								y = s->width;
							} else if (what == 1) {
								x = 0;
								y = s->x;
							} else if (what == 2) {
								x = 0;
								y = s->width;
							}
							for (int i = x; i < y; ++i) {
								term_set_cell(s, i, s->y, ' ');
							}
						}
						break;
					case ANSI_DSR:
						{
							char out[27];
							sprintf(out, "\033[%d;%dR", s->y + 1, s->x + 1);
							callbacks->input_buffer_stuff(s, out);
						}
						break;
					case ANSI_SU:
						term_scroll(s, argc ? atoi(argv[0]) : 1);
						break;
					case ANSI_SD:
						term_scroll(s, -(argc ? atoi(argv[0]) : 1));
						break;
					case ANSI_IL:
						term_insert_delete_lines(s, argc ? atoi(argv[0]) : 1);
						break;
					case ANSI_DL:
						term_insert_delete_lines(s, -(argc ? atoi(argv[0]) : 1));
						break;
					case 'X':
						{
							int how_many = argc ? atoi(argv[0]) : 1;
							for (int i = 0; i < how_many; ++i) {
								term_write(s, ' ');
							}
						}
						break;
					case 'd':
						term_set_csr(s, s->x, argc < 1 ? 0 : (atoi(argv[0]) - 1));
						break;
					default:
						/* Meh */
						break;
				}
				/* Set the states */
				term_set_colors(s, (s->flags & ANSI_BOLD && s->fg < 9) ? (s->fg % 8 + 8) : s->fg, s->bg);
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
				if (argc && argv[0]) {
					if (!strcmp(argv[0], "0") || !strcmp(argv[0], "2")) {
						if (argc > 1 && callbacks->set_title) callbacks->set_title(s, argv[1]);
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
					term_write(s, c);
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
				term_write(s, c);
			}
			s->escape = 0;
			s->buflen = 0;
			break;
		case 5:
			if (c == 'q') {
				char out[24];
				sprintf(out, "\033T%d;%dq", callbacks->get_cell_width(s), callbacks->get_cell_height(s));
				callbacks->input_buffer_stuff(s, out);
				s->escape = 0;
				s->buflen = 0;
			} else if (c == 's') {
				s->img_collected = 0;
				s->escape = 6;
				s->img_size = sizeof(uint32_t) * callbacks->get_cell_width(s) * callbacks->get_cell_height(s);
				if (!s->img_data) {
					s->img_data = malloc(s->img_size);
				}
				memset(s->img_data, 0x00, s->img_size);
			} else {
				ansi_dump_buffer(s);
				term_write(s, c);
				s->escape = 0;
				s->buflen = 0;
			}
			break;
		case 6:
			s->img_data[s->img_collected++] = c;
			if (s->img_collected == s->img_size) {
				if (callbacks->set_cell_contents) callbacks->set_cell_contents(s, s->x, s->y, s->img_data);
				term_set_csr(s, min(s->x + 1, s->width - 1), s->y);
				s->escape = 0;
				s->buflen = 0;
			}
			break;
	}
}

void termemu_put(term_state_t * s, char c) {
	spin_lock(&s->lock);
	_ansi_put(s, c);
	spin_unlock(&s->lock);
}

term_state_t * termemu_init(int w, int h, int max_scrollback, term_callbacks_t * callbacks_in) {
	term_state_t * s = calloc(1, sizeof(term_state_t));

	/* Terminal Defaults */
	s->fg     = TERM_DEFAULT_FG;    /* Light grey */
	s->bg     = TERM_DEFAULT_BG;    /* Black */
	s->flags  = TERM_DEFAULT_FLAGS; /* Nothing fancy*/
	s->width  = w;
	s->height = h;
	s->box    = 0;
	s->callbacks = callbacks_in;
	term_set_colors(s, s->fg, s->bg);
	s->mouse_on = 0;

	s->term_buffer_a = calloc(w * h, sizeof(term_cell_t));
	s->term_buffer_b = calloc(w * h, sizeof(term_cell_t));
	s->term_mirror   = calloc(w * h, sizeof(term_cell_t));
	s->term_display  = malloc(sizeof(term_cell_t) * w * h);
	memset(s->term_display, 0xFF, sizeof(term_cell_t) * w * h);
	s->term_buffer = s->term_buffer_a;
	s->cursor_on = 1;
	s->focused = 1;
	s->max_scrollback = max_scrollback;
	s->scrollback_list = list_create();
	s->scrollback_offset = 0;

	return s;
}

void termemu_free(term_state_t * state) {
	free(state->term_buffer_a);
	free(state->term_buffer_b);
	free(state->term_mirror);
	free(state->term_display);
	list_destroy(state->scrollback_list);
	list_free(state->scrollback_list);
	free(state->scrollback_list);

	if (state->img_data) free(state->img_data);

	free(state);
}

static term_cell_t * copy_terminal(term_state_t * state, int new_width, int new_height, int old_width, int old_height, term_cell_t * term_buffer) {
	term_cell_t * new_term_buffer = malloc(sizeof(term_cell_t) * new_width * new_height);

	memset(new_term_buffer, 0x0, sizeof(term_cell_t) * new_width * new_height);

	int offset = 0;
	if (state->height < old_height) {
		while (state->y >= state->height) {
			offset++;
			old_height--;
			state->y--;
		}
	}
	for (int row = 0; row < min(old_height, new_height); ++row) {
		for (int col = 0; col < min(old_width, new_width); ++col) {
			term_cell_t * old_cell = &term_buffer[(row+offset) * old_width + col];
			term_cell_t * new_cell = &new_term_buffer[row * new_width + col];
			*new_cell = *old_cell;
		}
	}
	if (state->x >= new_width) {
		state->x = new_width-1;
	}

	return new_term_buffer;
}

int termemu_reinit(term_state_t * state, int w, int h) {
	term_cell_t * new_a = copy_terminal(state, w, h, state->width, state->height, state->term_buffer_a);
	term_cell_t * new_b = copy_terminal(state, w, h, state->width, state->height, state->term_buffer_b);
	free(state->term_buffer_a);
	state->term_buffer_a = new_a;
	free(state->term_buffer_b);
	state->term_buffer_b = new_b;
	state->term_buffer = (state->active_buffer == 0) ? new_a : new_b;
	state->term_mirror = realloc(state->term_mirror, sizeof(term_cell_t) * w * h);
	memcpy(state->term_mirror, state->term_buffer, sizeof(term_cell_t) * w * h);
	state->term_display = realloc(state->term_display, sizeof(term_cell_t) * w * h);
	memset(state->term_display, 0xFF, sizeof(term_cell_t) * w * h);
	state->width = w;
	state->height = h;
	return 0;
}

/* Call a function for each selected cell */
void termemu_iterate_selection(term_state_t * state, void (*func)(term_state_t * s, uint16_t x, uint16_t y)) {
	if (!state->selection) return;
	if (state->selection_end_y < state->selection_start_y) {
		for (int x = state->selection_end_x; x < state->width; ++x) {
			func(state, x, state->selection_end_y);
		}
		for (int y = state->selection_end_y + 1; y < state->selection_start_y; ++y) {
			for (int x = 0; x < state->width; ++x) {
				func(state, x, y);
			}
		}
		for (int x = 0; x <= state->selection_start_xx; ++x) {
			func(state, x, state->selection_start_y);
		}
	} else if (state->selection_start_y == state->selection_end_y) {
		if (state->selection_start_x > state->selection_end_x) {
			for (int x = state->selection_end_x; x <= state->selection_start_xx; ++x) {
				func(state, x, state->selection_start_y);
			}
		} else {
			for (int x = state->selection_start_x; x <= state->selection_end_x || x <= state->selection_start_xx; ++x) {
				func(state, x, state->selection_start_y);
			}
		}
	} else {
		for (int x = state->selection_start_x; x < state->width; ++x) {
			func(state, x, state->selection_start_y);
		}
		for (int y = state->selection_start_y + 1; y < state->selection_end_y; ++y) {
			for (int x = 0; x < state->width; ++x) {
				func(state, x, y);
			}
		}
		for (int x = 0; x <= state->selection_end_x; ++x) {
			func(state, x, state->selection_end_y);
		}
	}
}

static void term_cell_redraw_offset(term_state_t * state, uint16_t x, uint16_t _y) {
	int y = _y;
	int i = y;

	y -= state->scrollback_offset;

	if (y >= 0) {
		term_mirror_copy(state, x,i,&state->term_buffer[y * state->width + x]);
	} else {
		node_t * node = state->scrollback_list->tail;
		for (; y < -1; y++) {
			if (!node) break;
			node = node->prev;
		}
		if (node) {
			struct TermemuScrollbackRow * row = (struct TermemuScrollbackRow *)node->value;
			if (row && x < row->width) {
				term_mirror_copy(state, x,i,&row->cells[x]);
			} else {
				term_mirror_set(state, x,i,' ',TERM_DEFAULT_FG, TERM_DEFAULT_BG, TERM_DEFAULT_FLAGS);
			}
		}
	}
}

static void term_cell_redraw_offset_inverted(term_state_t * state, uint16_t x, uint16_t _y) {
	int y = _y;
	int i = y;

	y -= state->scrollback_offset;

	if (y >= 0) {
		term_mirror_copy_inverted(state, x,i,&state->term_buffer[y * state->width + x]);
	} else {
		node_t * node = state->scrollback_list->tail;
		for (; y < -1; y++) {
			if (!node) break;
			node = node->prev;
		}
		if (node) {
			struct TermemuScrollbackRow * row = (struct TermemuScrollbackRow *)node->value;
			if (row && x < row->width) {
				term_mirror_copy_inverted(state, x,i,&row->cells[x]);
			} else {
				term_mirror_set(state, x, i, ' ', TERM_DEFAULT_BG, TERM_DEFAULT_FG, TERM_DEFAULT_FLAGS|ANSI_SPECBG);
			}
		}
	}
}


/* Redraw the selection with the selection hint (inversion) */
void termemu_redraw_selection(term_state_t * state) {
	termemu_iterate_selection(state, term_cell_redraw_offset_inverted);
}

term_cell_t * termemu_cell_at(term_state_t * state, uint16_t x, uint16_t _y) {
	int y = _y;
	y -= state->scrollback_offset;
	if (y >= 0) {
		return &state->term_buffer[y * state->width + x];
	} else {
		node_t * node = state->scrollback_list->tail;
		for (; y < -1; y++) {
			if (!node) break;
			node = node->prev;
		}
		if (node) {
			struct TermemuScrollbackRow * row = (struct TermemuScrollbackRow *)node->value;
			if (row && x < row->width) {
				return &row->cells[x];
			}
		}
	}
	return NULL;
}

void termemu_mark_cell(term_state_t * state, uint16_t x, uint16_t y) {
	term_cell_t * c = termemu_cell_at(state, x,y);
	if (c) {
		c->flags |= ANSI_MARKED;
	}
}

void termemu_mark_selection(term_state_t * state) {
	termemu_iterate_selection(state, termemu_mark_cell);
}

void termemu_red_cell(term_state_t * state, uint16_t x, uint16_t y) {
	term_cell_t * c = termemu_cell_at(state, x,y);
	if (c) {
		if (c->flags & ANSI_MARKED) {
			c->flags &= ~(ANSI_MARKED);
		} else {
			c->flags |= ANSI_RED;
		}
	}
}

void termemu_flip_selection(term_state_t * state) {
	termemu_iterate_selection(state, termemu_red_cell);
	for (int y = 0; y < state->height; ++y) {
		for (int x = 0; x < state->width; ++x) {
			term_cell_t * c = termemu_cell_at(state, x,y);
			if (c) {
				if (c->flags & ANSI_MARKED) term_cell_redraw_offset(state,x,y);
				if (c->flags & ANSI_RED) term_cell_redraw_offset_inverted(state,x,y);
				c->flags &= ~(ANSI_MARKED | ANSI_RED);
			}
		}
	}
}

void termemu_redraw_scrollback(term_state_t * state) {
	if (!state->scrollback_offset) {
		termemu_redraw_all(state);
		return;
	}
	if (state->scrollback_offset < state->height) {
		for (int i = state->scrollback_offset; i < state->height; i++) {
			int y = i - state->scrollback_offset;
			for (int x = 0; x < state->width; ++x) {
				term_mirror_copy(state, x,i,&state->term_buffer[y * state->width + x]);
			}
		}

		node_t * node = state->scrollback_list->tail;
		for (int i = 0; i < state->scrollback_offset; ++i) {
			struct TermemuScrollbackRow * row = (struct TermemuScrollbackRow *)node->value;

			int y = state->scrollback_offset - 1 - i;
			int width = row->width;
			if (width > state->width) {
				width = state->width;
			} else {
				for (int x = row->width; x < state->width; ++x) {
					term_mirror_set(state, x, y, ' ', TERM_DEFAULT_FG, TERM_DEFAULT_BG, TERM_DEFAULT_FLAGS);
				}
			}
			for (int x = 0; x < width; ++x) {
				term_mirror_copy(state, x,y,&row->cells[x]);
			}

			node = node->prev;
		}
	} else {
		node_t * node = state->scrollback_list->tail;
		for (int i = 0; i < state->scrollback_offset - state->height; ++i) {
			node = node->prev;
		}
		for (int i = state->scrollback_offset - state->height; i < state->scrollback_offset; ++i) {
			struct TermemuScrollbackRow * row = (struct TermemuScrollbackRow *)node->value;

			int y = state->scrollback_offset - 1 - i;
			int width = row->width;
			if (width > state->width) {
				width = state->width;
			} else {
				for (int x = row->width; x < state->width; ++x) {
					term_mirror_set(state, x, y, ' ', TERM_DEFAULT_FG, TERM_DEFAULT_BG, TERM_DEFAULT_FLAGS);
				}
			}
			for (int x = 0; x < width; ++x) {
				term_mirror_copy(state, x,y,&row->cells[x]);
			}

			node = node->prev;
		}
	}
}


/* Scroll the view up (scrollback) */
void termemu_scroll_up(term_state_t * state, int amount) {
	int i = 0;
	while (i < amount && state->scrollback_list && state->scrollback_offset < (ssize_t)state->scrollback_list->length) {
		state->scrollback_offset ++;
		i++;
	}
	termemu_redraw_scrollback(state);
}

/* Scroll the view down (scrollback) */
void termemu_scroll_down(term_state_t * state, int amount) {
	int i = 0;
	while (i < amount && state->scrollback_list && state->scrollback_offset != 0) {
		state->scrollback_offset -= 1;
		i++;
	}
	termemu_redraw_scrollback(state);
}

static void term_cell_redraw(term_state_t * state, uint16_t x, uint16_t y) {
	if (x >= state->width || y >= state->height) return;
	term_mirror_copy(state, x,y,&state->term_buffer[y * state->width + x]);
}

/* Redraw text cell inverted. */
static void term_cell_redraw_inverted(term_state_t * state, uint16_t x, uint16_t y) {
	/* Avoid cells out of range. */
	if (x >= state->width || y >= state->height) return;
	term_mirror_copy_inverted(state, x,y,&state->term_buffer[y * state->width + x]);
}

/* Redraw text cell with a surrounding box (used by cursor) */
static void term_cell_redraw_box(term_state_t * state, uint16_t x, uint16_t y) {
	if (x >= state->width || y >= state->height) return;
	term_cell_t cell = state->term_buffer[y * state->width + x];
	cell.flags |= ANSI_BORDER;
	term_mirror_copy(state, x,y,&cell);
}

/* Draw the cursor cell */
static void render_cursor(term_state_t * state) {
	if (!state->cursor_on) return;
	if (!state->focused) {
		/* An unfocused terminal should draw an unfilled box. */
		term_cell_redraw_box(state, state->x, state->y);
	} else {
		/* A focused terminal draws a solid box. */
		term_cell_redraw_inverted(state, state->x, state->y);
	}
}

/* A soft request to draw the cursor. */
static void term_draw_cursor(term_state_t * state) {
	if (!state->cursor_on) return;
	state->cursor_flipped = 0;
	render_cursor(state);
}

/* FIXME */
void termemu_draw_cursor(term_state_t * state) {
	term_draw_cursor(state);
}

static void term_undraw_cursor(term_state_t * state) {
	term_cell_redraw(state, state->x, state->y);
}

static uint64_t get_ticks(void) {
	struct timeval now;
	gettimeofday(&now, NULL);
	return (uint64_t)now.tv_sec * 1000000LL + (uint64_t)now.tv_usec;
}

void termemu_maybe_flip_cursor(term_state_t * state) {
	uint64_t ticks = get_ticks();
	if (ticks > state->mouse_ticks + 600000LL) {
		state->mouse_ticks = ticks;
		if (state->scrollback_offset != 0) {
			return; /* Don't flip cursor while drawing scrollback */
		}
		if (state->focused && state->cursor_flipped) {
			term_cell_redraw(state, state->x, state->y);
		} else {
			render_cursor(state);
		}
		state->cursor_flipped = 1 - state->cursor_flipped;
	}
}

static void term_save_scrollback(term_state_t * state, int row_num) {
	if (state->active_buffer == 1) return;

	/* If the scrollback is already full, remove the oldest element. */
	struct TermemuScrollbackRow * row = NULL;
	node_t * n = NULL;

	if (state->max_scrollback && state->scrollback_list->length == state->max_scrollback) {
		n = list_dequeue(state->scrollback_list);
		row = n->value;
		if (row->width < state->width) {
			free(row);
			row = NULL;
		}
	}

	if (!row) {
		row = malloc(sizeof(struct TermemuScrollbackRow) + sizeof(term_cell_t) * state->width);
		row->width = state->width;
	}

	if (!n) {
		list_insert(state->scrollback_list, row);
	} else {
		n->value = row;
		list_append(state->scrollback_list, n);
	}

	for (int i = 0; i < state->width; ++i) {
		term_cell_t * cell = &state->term_buffer[row_num * state->width + i];
		memcpy(&row->cells[i], cell, sizeof(term_cell_t));
	}
}

static void term_normalize_x(term_state_t * state, int setting_lcf) {
	if (state->x >= state->width) {
		state->x = state->width - 1;
		if (setting_lcf) {
			state->h = 1;
		}
	}
}

static void term_normalize_y(term_state_t * state) {
	if (state->y == state->height) {
		term_save_scrollback(state, 0);
		term_scroll(state, 1);
		state->y = state->height - 1;
	}
}

static int is_wide(uint32_t codepoint) {
	if (codepoint < 256) return 0;
	return wcwidth(codepoint) == 2;
}

static void term_shift_region(term_state_t * state, int top, int height, int how_much) {
	if (how_much == 0) return;

	int destination, source;
	int count, new_top, new_bottom;
	if (how_much > height) {
		count = 0;
		new_top = top;
		new_bottom = top + height;
	} else if (how_much > 0) {
		destination = state->width * top;
		source = state->width * (top + how_much);
		count = height - how_much;
		new_top = top + height - how_much;
		new_bottom = top + height;
	} else if (how_much < 0) {
		destination = state->width * (top - how_much);
		source = state->width * top;
		count = height + how_much;
		new_top = top;
		new_bottom = top - how_much;
	}

	/* Move from top+how_much to top */
	if (count) memmove(state->term_buffer + destination, state->term_buffer + source, count * state->width * sizeof(term_cell_t));

	/* Clear new lines at bottom */
	for (int i = new_top; i < new_bottom; ++i) {
		for (uint16_t x = 0; x < state->width; ++x) {
			term_cell_set(state, x, i, ' ', state->current_fg, state->current_bg, state->flags);
		}
	}

	termemu_redraw_all(state);
}


static void term_insert_delete_lines(term_state_t * state, int how_many) {
	if (how_many == 0) return;

	if (how_many > 0) {
		/* Insert lines is equivalent to scrolling from the current line */
		term_shift_region(state,state->y,state->height-state->y,-how_many);
	} else {
		term_shift_region(state,state->y,state->height-state->y,-how_many);
	}
}


static void term_write(term_state_t * state, char c) {
	static uint32_t unicode_state = 0;
	static uint32_t codepoint = 0;

	if (!decode(&unicode_state, &codepoint, (uint8_t)c)) {
		uint32_t o = codepoint;
		codepoint = 0;

		switch (c) {
			case '\a':
				/* boop */
				if (state->callbacks->bell) state->callbacks->bell(state);
				return;

			case '\r':
				term_undraw_cursor(state);
				state->x = state->h = 0;
				term_draw_cursor(state);
				return;

			case '\t':
				term_undraw_cursor(state);
				state->x += (8 - state->x % 8);
				term_normalize_x(state, 0);
				term_draw_cursor(state);
				return;

			case '\v':
			case '\f':
			case '\n':
				term_undraw_cursor(state);
				state->h = 0;
				++state->y;
				term_normalize_y(state);
				term_draw_cursor(state);
				return;

			case '\b':
				if (state->x > 0) {
					term_undraw_cursor(state);
					--state->x;
					term_draw_cursor(state);
				}
				state->h = 0;
				return;

			default: {
				int wide = is_wide(o);
				uint32_t flags = state->flags;

				term_undraw_cursor(state);

				if (state->h || (wide && state->x == state->width - 1)) {
					state->x = state->h = 0;
					++state->y;
					term_normalize_y(state);
				}

				if (wide) {
					flags = flags | ANSI_WIDE;
				}

				term_cell_set(state, state->x,state->y, o, state->current_fg, state->current_bg, flags);
				term_cell_redraw(state, state->x,state->y);
				state->x++;

				if (wide && state->x != state->width) {
					term_cell_set(state, state->x, state->y, 0xFFFF, state->current_fg, state->current_bg, state->flags);
					term_cell_redraw(state, state->x,state->y);
					term_cell_redraw(state, state->x-1,state->y);
					state->x++;
				}

				term_normalize_x(state, 1);
				term_draw_cursor(state);
				return;
			}
		}

	} else if (unicode_state == UTF8_REJECT) {
		unicode_state = 0;
		codepoint = 0;
	}
}

static void term_set_csr(term_state_t * state, int x, int y) {
	term_cell_redraw(state, state->x,state->y);
	if (x < 0) x = 0;
	if (x >= state->width) x = state->width - 1;
	if (y < 0) y = 0;
	if (y >= state->height) y = state->height - 1;
	state->x = x;
	state->y = y;
	state->h = 0;
	term_draw_cursor(state);
}

static void term_set_csr_show(term_state_t * state, int on) {
	state->cursor_on = on;
	if (on) {
		term_draw_cursor(state);
	}
}

static void term_set_colors(term_state_t *s, uint32_t fg, uint32_t bg) {
	s->current_fg = fg;
	s->current_bg = bg;
}

static void term_set_cell(term_state_t * state, int x, int y, uint32_t c) {
	term_cell_set(state, x, y, c, state->current_fg, state->current_bg, state->flags);
	term_cell_redraw(state, x, y);
}

void termemu_clear(term_state_t * state, int i) {
	if (i == 2) {
		/* Save everything up to and including the current line to scrollback,
		 * do not move the cursor, clear the whole screen. */
		for (int i = 0; i <= state->y; ++i) {
			term_save_scrollback(state, i);
		}
		memset((void *)state->term_buffer, 0x00, state->width * state->height * sizeof(term_cell_t));
		termemu_redraw_all(state);
	} else if (i == 0) {
		/* Clear after cursor */
		for (int x = state->x; x < state->width; ++x) {
			term_set_cell(state, x, state->y, ' ');
		}
		for (int y = state->y + 1; y < state->height; ++y) {
			for (int x = 0; x < state->width; ++x) {
				term_set_cell(state, x, y, ' ');
			}
		}
	} else if (i == 1) {
		/* Clear before cursor */
		for (int y = 0; y < state->y; ++y) {
			for (int x = 0; x < state->width; ++x) {
				term_set_cell(state, x, y, ' ');
			}
		}
		for (int x = 0; x < state->x; ++x) {
			term_set_cell(state, x, state->y, ' ');
		}
	} else if (i == 3) {
		/* Clear scrollback */
		if (state->scrollback_list) {
			while (state->scrollback_list->length) {
				node_t * n = list_dequeue(state->scrollback_list);
				free(n->value);
				free(n);
			}
			state->scrollback_offset = 0;
		}
	}

	if (state->callbacks->cls) state->callbacks->cls(state, i);
}

void termemu_switch_buffer(term_state_t * state, int buffer) {
	if (buffer != 0 && buffer != 1) return;
	if (buffer != state->active_buffer) {
		state->active_buffer = buffer;
		state->term_buffer = state->active_buffer == 0 ? state->term_buffer_a : state->term_buffer_b;

#define SWAP(T,a,b) do { T _a = a; a = b; b = _a; } while(0);

		SWAP(int, state->x, state->orig_x);
		SWAP(int, state->y, state->orig_y);
		SWAP(uint32_t, state->current_fg, state->orig_fg);
		SWAP(uint32_t, state->current_bg, state->orig_bg);

		termemu_redraw_all(state);
	}
}

static void term_scroll(term_state_t * state, int how_much) {
	term_shift_region(state, 0, state->height, how_much);
	if (state->callbacks->scroll) state->callbacks->scroll(state, how_much);
}

static void term_mirror_set(term_state_t * state, uint16_t x, uint16_t y, uint32_t val, uint32_t fg, uint32_t bg, uint32_t flags) {
	if (x >= state->width || y >= state->height) return;
	term_cell_t * cell = &state->term_mirror[y * state->width + x];
	cell->c = val;
	cell->fg = fg;
	cell->bg = bg;
	cell->flags = flags;
}

static void term_mirror_copy(term_state_t * state, uint16_t x, uint16_t y, term_cell_t * from) {
	if (x >= state->width || y >= state->height) return;
	term_cell_t * cell = &state->term_mirror[y * state->width + x];
	if (!from->c && !from->fg && !from->bg) {
		cell->c = ' ';
		cell->fg = TERM_DEFAULT_FG;
		cell->bg = TERM_DEFAULT_BG;
		cell->flags = from->flags;
	} else {
		*cell = *from;
	}
}

static void term_mirror_copy_inverted(term_state_t * state, uint16_t x, uint16_t y, term_cell_t * from) {
	if (x >= state->width || y >= state->height) return;
	term_cell_t * cell = &state->term_mirror[y * state->width + x];
	if (!from->c && !from->fg && !from->bg) {
		cell->c = ' ';
		cell->fg = TERM_DEFAULT_BG;
		cell->bg = TERM_DEFAULT_FG;
		cell->flags = from->flags | ANSI_INVERTED;
	} else if (from->flags & ANSI_EXT_IMG) {
		cell->c = ' ';
		cell->fg = from->fg;
		cell->bg = from->bg;
		cell->flags = from->flags | ANSI_SPECBG | ANSI_INVERTED;
	} else {
		cell->c = from->c;
		cell->fg = from->bg;
		cell->bg = from->fg;
		cell->flags = from->flags | ANSI_SPECBG | ANSI_INVERTED;
	}
}

/* Set a terminal cell */
static void term_cell_set(term_state_t * state, uint16_t x, uint16_t y, uint32_t c, uint32_t fg, uint32_t bg, uint32_t flags) {
	/* Avoid setting cells out of range. */
	if (x >= state->width || y >= state->height) return;

	/* Calculate the cell position in the terminal buffer */
	term_cell_t * cell = &state->term_buffer[y * state->width + x];

	/* Set cell attributes */
	cell->c     = c;
	cell->fg    = fg;
	cell->bg    = bg;
	cell->flags = flags;
}

void termemu_redraw_all(term_state_t * state) {
	for (int i = 0; i < state->height; i++) {
		for (int x = 0; x < state->width; ++x) {
			term_mirror_copy(state, x,i,&state->term_buffer[i * state->width + x]);
		}
	}
}

void termemu_unscroll(term_state_t * state) {
	if (state->scrollback_offset != 0) {
		state->scrollback_offset = 0;
		termemu_redraw_all(state);
	}
}

void termemu_scroll_top(term_state_t * state) {
	if (state->scrollback_list) {
		state->scrollback_offset = state->scrollback_list->length;
		termemu_redraw_scrollback(state);
	}
}

void termemu_selection_click(term_state_t * state, int new_x, int new_y) {
	termemu_redraw_scrollback(state);
	uint64_t now = get_ticks();
	if (now - state->last_click < 500000UL && (new_x == state->selection_start_x && new_y == state->selection_start_y)) {
		/* Double click */
		while (state->selection_start_x > 0) {
			term_cell_t * c = termemu_cell_at(state, state->selection_start_x-1, state->selection_start_y);
			if (!c || c->c == ' ' || !c->c) break;
			state->selection_start_x--;
		}
		while (state->selection_end_x < state->width - 1) {
			term_cell_t * c = termemu_cell_at(state, state->selection_end_x+1, state->selection_end_y);
			if (!c || c->c == ' ' || !c->c) break;
			state->selection_end_x++;
		}
		state->selection_start_xx = state->selection_end_x;
		state->selection = 1;
	} else {
		state->last_click = get_ticks();
		state->selection_start_x = new_x;
		state->selection_start_xx = new_x;
		state->selection_start_y = new_y;
		state->selection_end_x = new_x;
		state->selection_end_y = new_y;
		state->selection = 0;
	}
	termemu_redraw_selection(state);
}

void termemu_selection_drag(term_state_t * state, int new_x, int new_y) {
	termemu_mark_selection(state);
	state->selection_end_x = new_x;
	state->selection_end_y = new_y;
	state->selection = 1;
	termemu_flip_selection(state);
}

void termemu_full_reset(term_state_t * s) {
	s->escape = 0;
	s->buflen = 0;
	s->mouse_on = 0;
	s->save_x = 0;
	s->save_y = 0;
	s->box = 0;
	s->fg = TERM_DEFAULT_FG;
	s->bg = TERM_DEFAULT_BG;
	s->flags = TERM_DEFAULT_FLAGS;
	s->current_fg = s->fg;
	s->current_bg = s->bg;
	s->x = 0;
	s->y = 0;
	s->h = 0;
	s->orig_x = 0;
	s->orig_y = 0;
	s->orig_fg = s->fg;
	s->orig_bg = s->bg;
	s->active_buffer = 0;
	s->term_buffer = s->term_buffer_a;
	s->cursor_on = 1;
	memset(s->term_buffer_a, 0x00, s->width * s->height * sizeof(term_cell_t));
	memset(s->term_buffer_b, 0x00, s->width * s->height * sizeof(term_cell_t));
	memset(s->term_mirror,   0x00, s->width * s->height * sizeof(term_cell_t));
	if (s->callbacks->full_reset) s->callbacks->full_reset(s);
	if (s->callbacks->state_change) s->callbacks->state_change(s);
}
