/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2015-2018 K. Lange
 *
 * rline - a line reading library.
 *
 * Implements an interface similar to readline, providing more
 * complex line editing than what the raw tty interface supplies.
 */

#define _POSIX_C_SOURCE 1
#define _XOPEN_SOURCE 500
#include <stdint.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>

#include <toaru/kbd.h>
#include <toaru/rline.h>

static struct termios old;

static void set_unbuffered() {
	tcgetattr(fileno(stdin), &old);
	struct termios new = old;
	new.c_lflag &= (~ICANON & ~ECHO);
	tcsetattr(fileno(stdin), TCSAFLUSH, &new);
}

static void set_buffered() {
	tcsetattr(fileno(stdin), TCSAFLUSH, &old);
}


void rline_redraw(rline_context_t * context) {
	if (context->quiet) return;
	printf("\033[u%s\033[K", context->buffer);
	for (int i = context->offset; i < context->collected; ++i) {
		printf("\033[D");
	}
	fflush(stdout);
}

void rline_redraw_clean(rline_context_t * context) {
	if (context->quiet) return;
	printf("\033[u%s", context->buffer);
	for (int i = context->offset; i < context->collected; ++i) {
		printf("\033[D");
	}
	fflush(stdout);
}

char * rline_history[RLINE_HISTORY_ENTRIES];
int rline_history_count  = 0;
int rline_history_offset = 0;
int rline_scroll = 0;
char * rline_exit_string = "exit\n";

static char rline_temp[1024];

void rline_history_insert(char * str) {
	if (str[strlen(str)-1] == '\n') {
		str[strlen(str)-1] = '\0';
	}
	if (rline_history_count) {
		if (!strcmp(str, rline_history_prev(1))) {
			free(str);
			return;
		}
	}
	if (rline_history_count == RLINE_HISTORY_ENTRIES) {
		free(rline_history[rline_history_offset]);
		rline_history[rline_history_offset] = str;
		rline_history_offset = (rline_history_offset + 1) % RLINE_HISTORY_ENTRIES;
	} else {
		rline_history[rline_history_count] = str;
		rline_history_count++;
	}
}

void rline_history_append_line(char * str) {
	if (rline_history_count) {
		char ** s = &rline_history[(rline_history_count - 1 + rline_history_offset) % RLINE_HISTORY_ENTRIES];
		char * c = malloc(strlen(*s) + strlen(str) + 2);
		sprintf(c, "%s\n%s", *s, str);
		if (c[strlen(c)-1] == '\n') {
			c[strlen(c)-1] = '\0';
		}
		free(*s);
		*s = c;
	} else {
		/* wat */
	}
}

char * rline_history_get(int item) {
	return rline_history[(item + rline_history_offset) % RLINE_HISTORY_ENTRIES];
}

char * rline_history_prev(int item) {
	return rline_history_get(rline_history_count - item);
}

void rline_reverse_search(rline_context_t * context) {
	char input[512] = {0};
	int collected = 0;
	int start_at = 0;
	int changed = 0;
	fprintf(stderr, "\033[G\033[0m\033[s");
	fflush(stderr);
	key_event_state_t kbd_state = {0};
	char * match = "";
	int match_index = 0;
	while (1) {
		/* Find matches */
try_rev_search_again:
		if (collected && changed) {
			match = "";
			match_index = 0;
			for (int i = start_at; i < rline_history_count; i++) {
				char * c = rline_history_prev(i+1);
				if (strstr(c, input)) {
					match = c;
					match_index = i;
					break;
				}
			}
			if (!strcmp(match,"")) {
				if (start_at) {
					start_at = 0;
					goto try_rev_search_again;
				}
				collected--;
				input[collected] = '\0';
				if (collected) {
					goto try_rev_search_again;
				}
			}
		}
		fprintf(stderr, "\033[u(reverse-i-search)`%s': %s\033[K", input, match);
		fflush(stderr);
		changed = 0;

		uint32_t key_sym = kbd_key(&kbd_state, fgetc(stdin));
		switch (key_sym) {
			case KEY_NONE:
				break;
			case KEY_BACKSPACE:
				if (collected > 0) {
					collected--;
					input[collected] = '\0';
					start_at = 0;
					changed = 1;
				}
				break;
			case KEY_CTRL_C:
				printf("^C\n");
				return;
			case KEY_CTRL_R:
				start_at = match_index + 1;
				changed = 1;
				break;
			case KEY_ESCAPE:
			case KEY_ARROW_LEFT:
			case KEY_ARROW_RIGHT:
				context->cancel = 1;
			case '\n':
				memcpy(context->buffer, match, strlen(match) + 1);
				context->collected = strlen(match);
				context->offset = context->collected;
				if (!context->quiet && context->callbacks->redraw_prompt) {
					fprintf(stderr, "\033[G\033[K");
					context->callbacks->redraw_prompt(context);
				}
				fprintf(stderr, "\033[s");
				rline_redraw_clean(context);
				if (key_sym == '\n' && !context->quiet) {
					fprintf(stderr, "\n");
				}
				return;
			default:
				if (key_sym < KEY_NORMAL_MAX) {
					input[collected] = (char)key_sym;
					collected++;
					input[collected] = '\0';
					start_at = 0;
					changed = 1;
				}
				break;
		}
	}
}

static void history_previous(rline_context_t * context) {
	if (rline_scroll == 0) {
		memcpy(rline_temp, context->buffer, strlen(context->buffer) + 1);
	}
	if (rline_scroll < rline_history_count) {
		rline_scroll++;
		for (int i = 0; i < (int)strlen(context->buffer); ++i) {
			printf("\010 \010");
		}
		char * h = rline_history_prev(rline_scroll);
		memcpy(context->buffer, h, strlen(h) + 1);
		printf("\033[u%s\033[K", h);
		fflush(stdout);
	}
	context->collected = strlen(context->buffer);
	context->offset = context->collected;
}

static void history_next(rline_context_t * context) {
	if (rline_scroll > 1) {
		rline_scroll--;
		for (int i = 0; i < (int)strlen(context->buffer); ++i) {
			printf("\010 \010");
		}
		char * h = rline_history_prev(rline_scroll);
		memcpy(context->buffer, h, strlen(h) + 1);
		printf("%s", h);
		fflush(stdout);
	} else if (rline_scroll == 1) {
		for (int i = 0; i < (int)strlen(context->buffer); ++i) {
			printf("\010 \010");
		}
		rline_scroll = 0;
		memcpy(context->buffer, rline_temp, strlen(rline_temp) + 1);
		printf("\033[u%s\033[K", context->buffer);
		fflush(stdout);
	}
	context->collected = strlen(context->buffer);
	context->offset = context->collected;
}


/**
 * Insert characters at the current cursor offset.
 */
void rline_insert(rline_context_t * context, const char * what) {
	size_t insertion_length = strlen(what);

	if (context->collected + (int)insertion_length > context->requested) {
		insertion_length = context->requested - context->collected;
	}

	/* Move */
	memmove(&context->buffer[context->offset + insertion_length], &context->buffer[context->offset], context->collected - context->offset);
	memcpy(&context->buffer[context->offset], what, insertion_length);
	context->collected += insertion_length;
	context->offset += insertion_length;
}

static rline_callbacks_t _rline_null_callbacks = {NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL};

int rline(char * buffer, int buf_size, rline_callbacks_t * callbacks) {
	/* Initialize context */
	rline_context_t context = {
		buffer,
		callbacks,
		0,
		buf_size,
		0,
		0,
		0,
		0,
		0,
	};

	if (!callbacks) {
		callbacks = &_rline_null_callbacks;
	}

	set_unbuffered();

	printf("\033[s");
	fflush(stdout);

	key_event_state_t kbd_state = {0};

	/* Read keys */
	while ((context.collected < context.requested) && (!context.newline)) {
		uint32_t key_sym = kbd_key(&kbd_state, fgetc(stdin));
		if (key_sym == KEY_NONE) continue;
		if (key_sym != '\t') context.tabbed = 0;
		switch (key_sym) {
			case KEY_CTRL_C:
				printf("^C\n");
				context.buffer[0] = '\0';
				set_buffered();
				return 0;
			case KEY_CTRL_R:
				if (callbacks->rev_search) {
					callbacks->rev_search(&context);
				} else {
					rline_reverse_search(&context);
				}
				if (context.cancel) {
					context.cancel = 0;
					continue;
				} else {
					set_buffered();
					return context.collected;
				}
			case KEY_ARROW_UP:
			case KEY_CTRL_P:
				if (callbacks->key_up) {
					callbacks->key_up(&context);
				} else {
					history_previous(&context);
				}
				continue;
			case KEY_ARROW_DOWN:
			case KEY_CTRL_N:
				if (callbacks->key_down) {
					callbacks->key_down(&context);
				} else {
					history_next(&context);
				}
				continue;
			case KEY_CTRL_ARROW_RIGHT:
				while (context.offset < context.collected && context.buffer[context.offset] == ' ') {
					context.offset++;
					printf("\033[C");
				}
				while (context.offset < context.collected) {
					context.offset++;
					printf("\033[C");
					if (context.buffer[context.offset] == ' ') break;
				}
				fflush(stdout);
				continue;
			case KEY_CTRL_ARROW_LEFT:
				if (context.offset == 0) continue;
				context.offset--;
				printf("\033[D");
				while (context.offset && context.buffer[context.offset] == ' ') {
					context.offset--;
					printf("\033[D");
				}
				while (context.offset > 0) {
					if (context.buffer[context.offset-1] == ' ') break;
					context.offset--;
					printf("\033[D");
				}
				fflush(stdout);
				continue;
			case KEY_ARROW_RIGHT:
				if (callbacks->key_right) {
					callbacks->key_right(&context);
				} else {
					if (context.offset < context.collected) {
						printf("\033[C");
						fflush(stdout);
						context.offset++;
					}
				}
				continue;
			case KEY_ARROW_LEFT:
				if (callbacks->key_left) {
					callbacks->key_left(&context);
				} else {
					if (context.offset > 0) {
						printf("\033[D");
						fflush(stdout);
						context.offset--;
					}
				}
				continue;
			case KEY_CTRL_A:
			case KEY_HOME:
				while (context.offset > 0) {
					printf("\033[D");
					context.offset--;
				}
				fflush(stdout);
				continue;
			case KEY_CTRL_E:
			case KEY_END:
				while (context.offset < context.collected) {
					printf("\033[C");
					context.offset++;
				}
				fflush(stdout);
				continue;
			case KEY_CTRL_K:
				context.collected = context.offset;
				printf("\033[K");
				fflush(stdout);
				continue;
			case KEY_CTRL_D:
				if (context.collected == 0) {
					printf(rline_exit_string);
					sprintf(context.buffer, rline_exit_string);
					set_buffered();
					return strlen(context.buffer);
				}
				/* Intentional fallthrough */
			case KEY_DEL:
				if (context.collected) {
					if (context.offset == context.collected) {
						continue;
					}
					int remaining = context.collected - context.offset;
					for (int i = 1; i < remaining; ++i) {
						printf("%c", context.buffer[context.offset + i]);
						context.buffer[context.offset + i - 1] = context.buffer[context.offset + i];
					}
					printf(" ");
					for (int i = 0; i < remaining; ++i) {
						printf("\033[D");
					}
					context.collected--;
					fflush(stdout);
				}
				continue;
			case KEY_BACKSPACE:
				if (context.collected) {
					int should_redraw = 0;
					if (!context.offset) {
						continue;
					}
					printf("\010 \010");
					if (context.buffer[context.offset-1] == '\t') {
						should_redraw = 1;
					}
					if (context.offset != context.collected) {
						int remaining = context.collected - context.offset;
						for (int i = 0; i < remaining; ++i) {
							printf("%c", context.buffer[context.offset + i]);
							context.buffer[context.offset + i - 1] = context.buffer[context.offset + i];
						}
						printf(" ");
						for (int i = 0; i < remaining + 1; ++i) {
							printf("\033[D");
						}
						context.offset--;
						context.collected--;
					} else {
						context.buffer[--context.collected] = '\0';
						context.offset--;
					}
					if (should_redraw) {
						rline_redraw_clean(&context);
					}
					fflush(stdout);
				}
				continue;
			case KEY_CTRL_L: /* ^L: Clear Screen, redraw prompt and buffer */
				printf("\033[H\033[2J");
				fflush(stdout);
				/* Flush before yielding control to potentially foreign environment. */
				if (callbacks->redraw_prompt) {
					callbacks->redraw_prompt(&context);
				}
				printf("\033[s");
				rline_redraw_clean(&context);
				continue;
			case KEY_CTRL_W:
				/*
				 * Erase word before cursor.
				 * If the character before the cursor is a space, delete it.
				 * Continue deleting until the previous character is a space.
				 */
				if (context.collected) {
					if (!context.offset) {
						continue;
					}
					do {
						printf("\010 \010");
						if (context.offset != context.collected) {
							int remaining = context.collected - context.offset;
							for (int i = 0; i < remaining; ++i) {
								printf("%c", context.buffer[context.offset + i]);
								context.buffer[context.offset + i - 1] = context.buffer[context.offset + i];
							}
							printf(" ");
							for (int i = 0; i < remaining + 1; ++i) {
								printf("\033[D");
							}
							context.offset--;
							context.collected--;
						} else {
							context.buffer[--context.collected] = '\0';
							context.offset--;
						}
					} while ((context.offset) && (context.buffer[context.offset-1] != ' '));
					fflush(stdout);
				}
				continue;
			case '\t':
				if (callbacks->tab_complete) {
					callbacks->tab_complete(&context);
				}
				continue;
			case '\n':
				while (context.offset < context.collected) {
					printf("\033[C");
					context.offset++;
				}
				if (context.collected < context.requested) {
					context.buffer[context.collected] = '\n';
					context.buffer[++context.collected] = '\0';
					context.offset++;
				}
				printf("\n");
				fflush(stdout);
				context.newline = 1;
				continue;
		}
		if (context.offset != context.collected) {
			for (int i = context.collected; i > context.offset; --i) {
				context.buffer[i] = context.buffer[i-1];
			}
			if (context.collected < context.requested) {
				context.buffer[context.offset] = (char)key_sym;
				context.buffer[++context.collected] = '\0';
				context.offset++;
			}
			for (int i = context.offset - 1; i < context.collected; ++i) {
				printf("%c", context.buffer[i]);
			}
			for (int i = context.offset; i < context.collected; ++i) {
				printf("\033[D");
			}
			fflush(stdout);
		} else {
			printf("%c", (char)key_sym);
			if (context.collected < context.requested) {
				context.buffer[context.collected] = (char)key_sym;
				context.buffer[++context.collected] = '\0';
				context.offset++;
			}
			fflush(stdout);
		}
	}

	/* Cap that with a null */
	context.buffer[context.collected] = '\0';
	set_buffered();
	return context.collected;
}


static char * last_prompt = NULL;
static void redraw_prompt(rline_context_t * c) {
	(void)c;
	printf("%s", last_prompt);
	fflush(stdout);
	return;
}
static void insert_tab(rline_context_t * c) {
	rline_insert(c, "\t");
	rline_redraw_clean(c);
}

void * rline_for_python(void * _stdin, void * _stdout, char * prompt) {
	last_prompt = prompt;

	rline_callbacks_t callbacks = {
		insert_tab, redraw_prompt, NULL,
		NULL, NULL, NULL, NULL, NULL
	};


	redraw_prompt(NULL);
	char * buf = malloc(1024);
	memset(buf, 0, 1024);
	rline(buf, 1024, &callbacks);
	rline_history_insert(strdup(buf));
	rline_scroll = 0;

	return buf;
}
