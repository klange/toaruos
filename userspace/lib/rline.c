/*
 * rline - a line reading library.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "kbd.h"
#include "rline.h"

void rline_redraw(rline_context_t * context) {
	printf("\033[u%s\033[K", context->buffer);
	for (int i = context->offset; i < context->collected; ++i) {
		printf("\033[D");
	}
	fflush(stdout);
}

void rline_redraw_clean(rline_context_t * context) {
	printf("\033[u%s", context->buffer);
	for (int i = context->offset; i < context->collected; ++i) {
		printf("\033[D");
	}
	fflush(stdout);
}

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
	};

	printf("\033[s");
	fflush(stdout);

	key_event_state_t kbd_state = {0};

	/* Read keys */
	while ((context.collected < context.requested) && (!context.newline)) {
		uint32_t key_sym = kbd_key(&kbd_state, fgetc(stdin));
		if (key_sym == KEY_NONE) continue;
		switch (key_sym) {
			case KEY_CTRL_C:
				printf("^C\n");
				context.buffer[0] = '\0';
				return 0;
			case KEY_CTRL_R:
				if (callbacks->rev_search) {
					callbacks->rev_search(&context);
					return context.collected;
				}
				continue;
			case KEY_ARROW_UP:
			case KEY_CTRL_P:
				if (callbacks->key_up) {
					callbacks->key_up(&context);
				}
				continue;
			case KEY_ARROW_DOWN:
			case KEY_CTRL_N:
				if (callbacks->key_down) {
					callbacks->key_down(&context);
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
			case KEY_CTRL_D:
				if (context.collected == 0) {
					printf("exit\n");
					sprintf(context.buffer, "exit\n");
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
					if (!context.offset) {
						continue;
					}
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
					fflush(stdout);
				}
				continue;
			case KEY_CTRL_L: /* ^L: Clear Screen, redraw prompt and buffer */
				printf("\033[H\033[2J");
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
	return context.collected;
}
