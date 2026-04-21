/**
 * @brief Virtual terminal emulator, for VGA text mode.
 *
 * Basically the same as @ref terminal.c but outputs to the VGA
 * text mode buffer instead of managing a graphical window.
 *
 * Supports >16 colors by using a dumb closest-match approach.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2026 K. Lange
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <getopt.h>
#include <errno.h>
#include <pty.h>
#include <sys/fswait.h>

#include <kernel/video.h>

#include <toaru/kbd.h>
#include <toaru/graphics.h>
#include <toaru/termemu.h>
#include <toaru/mouse.h>
#include <toaru/list.h>
#include <toaru/spinlock.h>

#include "vga-palette.h"

static int vga_text_fd = 0;
static volatile int exit_application = 0;
static bool terminal_login_shell_restricted = 0;

static char * selection_text = NULL;
static int _selection_count = 0;
static int _selection_i = 0;

/* Actual mouse cursor */
static int mouse_x = 0;
static int mouse_y = 0;
static int rel_mouse_x = 0;
static int rel_mouse_y = 0;
static int last_mouse_buttons = 0;
static unsigned int button_state = 0;
static int mouse_is_dragging = 0;
static int mouse_r[2] = {820, 2621};
static int old_x = 0;
static int old_y = 0;

struct input_data {
	size_t len;
	char data[];
};

struct Terminal_Private {
	int fd_master, fd_slave;
	pid_t child_pid;
	pthread_t input_buffer_thread;
	volatile int input_buffer_lock;
	int input_buffer_semaphore[2];
	list_t * input_buffer_queue;
	int thread_done;
};

static list_t * terminals = NULL;
static list_t * dead_terminals = NULL;
static term_state_t * active_terminal = NULL;

static term_state_t * current_terminal(void) {
	return active_terminal;
}

static term_state_t * terminal_create(int term_width, int term_height, int max_scrollback, int argc, char * argv[]);

static int color_distance(uint32_t a, uint32_t b) {
	int a_r = (a & 0xFF0000) >> 16;
	int a_g = (a & 0xFF00) >> 8;
	int a_b = (a & 0xFF);

	int b_r = (b & 0xFF0000) >> 16;
	int b_g = (b & 0xFF00) >> 8;
	int b_b = (b & 0xFF);

	int distance = 0;
	distance += abs(a_r - b_r) * 3;
	distance += abs(a_g - b_g) * 6;
	distance += abs(a_b - b_b) * 10;

	return distance;
}

static uint32_t vga_base_colors[] = {
	0x000000,
	0xAA0000,
	0x00AA00,
	0xAA5500,
	0x0000AA,
	0xAA00AA,
	0x00AAAA,
	0xAAAAAA,
	0x555555,
	0xFF5555,
	0x55AA55,
	0xFFFF55,
	0x5555FF,
	0xFF55FF,
	0x55FFFF,
	0xFFFFFF,
};

static int best_match(uint32_t a) {
	int best_distance = INT32_MAX;
	int best_index = 0;
	for (int j = 0; j < 16; ++j) {
		int distance = color_distance(a, vga_base_colors[j]);
		if (distance < best_distance) {
			best_index = j;
			best_distance = distance;
		}
	}
	return best_index;
}

static void count_selection(term_state_t * state, uint16_t x, uint16_t y) {
	term_cell_t * cell = termemu_cell_at(state,x,y);
	if (((uint32_t *)cell)[0] != 0x00000000) {
		char tmp[7];
		_selection_count += termemu_to_eight(cell->c, tmp);
	}
	if (x == current_terminal()->width - 1) {
		_selection_count++;
	}
}

static void write_selection(term_state_t * state, uint16_t x, uint16_t y) {
	term_cell_t * cell = termemu_cell_at(state,x,y);
	if (((uint32_t *)cell)[0] != 0x00000000) {
		char tmp[7];
		int count = termemu_to_eight(cell->c, tmp);
		for (int i = 0; i < count; ++i) {
			selection_text[_selection_i] = tmp[i];
			_selection_i++;
		}
	}
	if (x == current_terminal()->width - 1) {
		selection_text[_selection_i] = '\n';;
		_selection_i++;
	}
}

static char * copy_selection(void) {
	_selection_count = 0;
	termemu_iterate_selection(current_terminal(), count_selection);
	if (selection_text) free(selection_text);
	if (!_selection_count) return NULL;

	selection_text = calloc(_selection_count + 1, 1);
	_selection_i = 0;
	termemu_iterate_selection(current_terminal(), write_selection);

	if (selection_text[_selection_count-1] == '\n') {
		/* Don't end on a line feed */
		selection_text[_selection_count-1] = '\0';
	}

	return selection_text;
}

void * handle_input_writing(void * _state) {
	term_state_t * my_state = _state;
	struct Terminal_Private * my_term = my_state->priv;

	while (1) {

		/* Read one byte from semaphore; as long as semaphore has data,
		 * there is another input blob to write to the TTY */
		char tmp[1];
		int c = read(my_term->input_buffer_semaphore[0],tmp,1);
		if (c > 0) {
			/* Retrieve blob */
			spin_lock(&my_term->input_buffer_lock);
			node_t * blob = list_dequeue(my_term->input_buffer_queue);
			spin_unlock(&my_term->input_buffer_lock);
			/* No blobs? This shouldn't happen, but just in case, just continue */
			if (!blob) {
				continue;
			}
			/* Write blob data to the tty */
			struct input_data * value = blob->value;
			write(my_term->fd_master, value->data, value->len);
			free(blob->value);
			free(blob);
		} else {
			/* The pipe has closed, terminal is exiting */
			break;
		}
	}

	my_term->thread_done = 1;
	return NULL;
}

static void write_input_buffer(term_state_t * state, char * data, size_t len) {
	struct Terminal_Private * priv = state->priv;
	struct input_data * d = malloc(sizeof(struct input_data) + len);
	d->len = len;
	memcpy(&d->data, data, len);
	spin_lock(&priv->input_buffer_lock);
	list_insert(priv->input_buffer_queue, d);
	spin_unlock(&priv->input_buffer_lock);
	write(priv->input_buffer_semaphore[1], d, 1);
}

static void handle_input(char c) {
	write_input_buffer(current_terminal(), &c, 1);
	termemu_unscroll(current_terminal());
}

static void handle_input_s(char * c) {
	size_t len = strlen(c);
	write_input_buffer(current_terminal(), c, len);
	termemu_unscroll(current_terminal());
}

/* ANSI-to-VGA */
static char vga_to_ansi[] = {
	0, 4, 2, 6, 1, 5, 3, 7,
	8,12,10,14, 9,13,11,15
};

#include "ununicode.h"

static void term_write_char(uint32_t val, uint16_t x, uint16_t y, uint32_t fg, uint32_t bg, uint32_t flags) {
	if (flags & ANSI_INVERT) {
		uint32_t tmp = fg;
		fg = bg;
		bg = tmp;
	}

	if (val == L'▏') val = 179;
	else if (val > 128) val = ununicode(val);
	if (fg > 256) {
		fg = best_match(fg);
	}
	if (bg > 256) {
		bg = best_match(bg);
	}
	if (fg > 16) {
		fg = vga_colors[fg];
	}
	if (bg > 16) {
		bg = vga_colors[bg];
	}
	if (fg == 16) fg = 0;
	if (bg == 16) bg = 0;

	unsigned short attr = (vga_to_ansi[fg] & 0xF) | (vga_to_ansi[bg] << 4);
	unsigned short cell = (val | (attr << 8));
	pwrite(vga_text_fd, &cell, sizeof(unsigned short), (current_terminal()->width * y + x) * sizeof(unsigned short));
}

static void flip_display() {
	term_state_t * state = current_terminal();
	for (int y = 0; y < state->height; ++y) {
		for (int x = 0; x < state->width; ++x) {
			term_cell_t * cell_m = &state->term_mirror[y * state->width + x];
			term_cell_t * cell_d = &state->term_display[y * state->width + x];
			if (memcmp(cell_m, cell_d, sizeof(term_cell_t))) {
				*cell_d = *cell_m;
				term_write_char(cell_m->c, x, y, cell_m->fg, cell_m->bg, cell_m->flags);
			}
		}
	}
}

static void refresh_display(term_state_t * state) {
	memset(state->term_display, 0xFF, sizeof(term_cell_t) * state->width * state->height);
	flip_display();
}

static void new_tab() {
	if (terminals->length == 9) return;
	active_terminal = terminal_create(
		active_terminal->width,
		active_terminal->height,
		active_terminal->max_scrollback,
		0, NULL);
	refresh_display(active_terminal);
}

#define mod_Shift (event->modifiers & KEY_MOD_LEFT_SHIFT || event->modifiers & KEY_MOD_RIGHT_SHIFT)
#define mod_Ctrl  (event->modifiers & KEY_MOD_LEFT_CTRL  || event->modifiers & KEY_MOD_RIGHT_CTRL)
#define mod_Alt   (event->modifiers & KEY_MOD_LEFT_ALT   || event->modifiers & KEY_MOD_RIGHT_ALT)

static void key_event(int ret, key_event_t * event) {
	if (ret) {

		/* Ctrl-Shift-T: Open a new tab */
		if (mod_Shift && mod_Ctrl && !mod_Alt && (event->keycode == 't')) {
			new_tab();
			return;
		}

		/* Alt-n: Switch to tab. */
		if (mod_Alt && !mod_Ctrl && !mod_Shift && (event->keycode >= '1') && (event->keycode <= '9') && terminals->length > 1) {
			int term = event->keycode - '1';
			int i = 0;

			foreach (node, terminals) {
				if (i == term) {
					active_terminal = node->value;
					refresh_display(active_terminal);
					return;
				}
				i++;
			}
		}

		/* Ctrl-Shift-C: Copy selection */
		if (mod_Shift && mod_Ctrl && !mod_Alt && (event->keycode == 'c')) {
			if (current_terminal()->selection) {
				/* Copy selection */
				copy_selection();
			}
			return;
		}

		/* Ctrl-Shift-V: Paste clipboard */
		if (mod_Shift && mod_Ctrl && !mod_Alt && (event->keycode == 'v')) {
			/* Paste selection */
			if (selection_text) {
				if (current_terminal()->paste_mode) {
					handle_input_s("\033[200~");
					handle_input_s(selection_text);
					handle_input_s("\033[201~");
				} else {
					handle_input_s(selection_text);
				}
			}
			return;
		}

		if (event->modifiers & KEY_MOD_LEFT_ALT || event->modifiers & KEY_MOD_RIGHT_ALT) {
			handle_input('\033');
		}
		if ((event->modifiers & KEY_MOD_LEFT_SHIFT || event->modifiers & KEY_MOD_RIGHT_SHIFT) &&
		    event->key == '\t') {
			handle_input_s("\033[Z");
			return;
		}

		/* ENTER = reads as linefeed, should be carriage return */
		if (event->keycode == 10) {
			handle_input('\r');
			return;
		}

		/* BACKSPACE = reads as ^H, should be ^? */
		if (event->keycode == 8) {
			handle_input(0x7F);
			return;
		}

		handle_input(event->key);
	} else {
		if (event->action == KEY_ACTION_UP) return;
		switch (event->keycode) {
			case KEY_F1:
				handle_input_s("\033OP");
				break;
			case KEY_F2:
				handle_input_s("\033OQ");
				break;
			case KEY_F3:
				handle_input_s("\033OR");
				break;
			case KEY_F4:
				handle_input_s("\033OS");
				break;
			case KEY_F5:
				handle_input_s("\033[15~");
				break;
			case KEY_F6:
				handle_input_s("\033[17~");
				break;
			case KEY_F7:
				handle_input_s("\033[18~");
				break;
			case KEY_F8:
				handle_input_s("\033[19~");
				break;
			case KEY_F9:
				handle_input_s("\033[20~");
				break;
			case KEY_F10:
				handle_input_s("\033[21~");
				break;
			case KEY_F11:
				handle_input_s("\033[23~");
				break;
			case KEY_F12:
				handle_input_s("\033[24~");
				break;
			case KEY_ARROW_UP:
				if (event->modifiers & KEY_MOD_LEFT_SHIFT && event->modifiers & KEY_MOD_LEFT_CTRL) {
					handle_input_s("\033[6A");
				} else if (event->modifiers & KEY_MOD_LEFT_CTRL) {
					handle_input_s("\033[5A");
				} else if (event->modifiers & KEY_MOD_LEFT_SHIFT && event->modifiers & KEY_MOD_LEFT_ALT) {
					handle_input_s("\033[4A");
				} else if (event->modifiers & KEY_MOD_LEFT_ALT) {
					handle_input_s("\033[3A");
				} else if (event->modifiers & KEY_MOD_LEFT_SHIFT) {
					handle_input_s("\033[2A");
				} else {
					handle_input_s("\033[A");
				}
				break;
			case KEY_ARROW_DOWN:
				if (event->modifiers & KEY_MOD_LEFT_SHIFT && event->modifiers & KEY_MOD_LEFT_CTRL) {
					handle_input_s("\033[6B");
				} else if (event->modifiers & KEY_MOD_LEFT_CTRL) {
					handle_input_s("\033[5B");
				} else if (event->modifiers & KEY_MOD_LEFT_SHIFT && event->modifiers & KEY_MOD_LEFT_ALT) {
					handle_input_s("\033[4B");
				} else if (event->modifiers & KEY_MOD_LEFT_ALT) {
					handle_input_s("\033[3B");
				} else if (event->modifiers & KEY_MOD_LEFT_SHIFT) {
					handle_input_s("\033[2B");
				} else {
					handle_input_s("\033[B");
				}
				break;
			case KEY_ARROW_RIGHT:
				if (event->modifiers & KEY_MOD_LEFT_SHIFT && event->modifiers & KEY_MOD_LEFT_CTRL) {
					handle_input_s("\033[6C");
				} else if (event->modifiers & KEY_MOD_LEFT_CTRL) {
					handle_input_s("\033[5C");
				} else if (event->modifiers & KEY_MOD_LEFT_SHIFT && event->modifiers & KEY_MOD_LEFT_ALT) {
					handle_input_s("\033[4C");
				} else if (event->modifiers & KEY_MOD_LEFT_ALT) {
					handle_input_s("\033[3C");
				} else if (event->modifiers & KEY_MOD_LEFT_SHIFT) {
					handle_input_s("\033[2C");
				} else {
					handle_input_s("\033[C");
				}
				break;
			case KEY_ARROW_LEFT:
				if (event->modifiers & KEY_MOD_LEFT_SHIFT && event->modifiers & KEY_MOD_LEFT_CTRL) {
					handle_input_s("\033[6D");
				} else if (event->modifiers & KEY_MOD_LEFT_CTRL) {
					handle_input_s("\033[5D");
				} else if (event->modifiers & KEY_MOD_LEFT_SHIFT && event->modifiers & KEY_MOD_LEFT_ALT) {
					handle_input_s("\033[4D");
				} else if (event->modifiers & KEY_MOD_LEFT_ALT) {
					handle_input_s("\033[3D");
				} else if (event->modifiers & KEY_MOD_LEFT_SHIFT) {
					handle_input_s("\033[2D");
				} else {
					handle_input_s("\033[D");
				}
				break;
			case KEY_PAGE_UP:
				if (event->modifiers & KEY_MOD_LEFT_SHIFT) {
					if (current_terminal()->active_buffer != 1) termemu_scroll_up(current_terminal(), current_terminal()->height/2);
				} else {
					handle_input_s("\033[5~");
				}
				break;
			case KEY_PAGE_DOWN:
				if (event->modifiers & KEY_MOD_LEFT_SHIFT) {
					if (current_terminal()->active_buffer != 1) termemu_scroll_down(current_terminal(), current_terminal()->height/2);
				} else {
					handle_input_s("\033[6~");
				}
				break;
			case KEY_HOME:
				if (event->modifiers & KEY_MOD_LEFT_SHIFT) {
					termemu_scroll_top(current_terminal());
				} else {
					handle_input_s("\033[H");
				}
				break;
			case KEY_END:
				if (event->modifiers & KEY_MOD_LEFT_SHIFT) {
					termemu_unscroll(current_terminal());
				} else {
					handle_input_s("\033[F");
				}
				break;
			case KEY_DEL:
				handle_input_s("\033[3~");
				break;
		}
	}
}

static int usage(char * argv[]) {
#define X_S "\033[3m"
#define X_E "\033[0m"
	fprintf(stderr,
			"VGA Terminal Emulator\n"
			"\n"
			"usage: %s [-l] [" X_S "command..." X_E "]\n"
			"\n"
			" You probably don't want to run this directly.\n"
			"\n"
			" -l --login  " X_S "Start a login shell." X_E "\n"
			" -h --help   " X_S "Show this help message." X_E "\n"
			"\n",
			argv[0]);
	return 1;
}

static int term_char_size(term_state_t * s) {
	return 0;
}

static void input_buffer_stuff(term_state_t * state, char * str) {
	size_t len = strlen(str);
	write_input_buffer(state, str, len);
}

static term_callbacks_t term_callbacks = {
	NULL,
	NULL,
	input_buffer_stuff,
	NULL,
	NULL,
	term_char_size,
	term_char_size,
	NULL,
	NULL,
};

static int check_for_exit(void) {

	/* If something has set exit_application, we should exit. */
	if (exit_application) return 1;

	/* See if any dead terminals can be cleaned up */
	while (dead_terminals->length) {
		struct Terminal_Private * term = dead_terminals->head->value;
		if (!term->thread_done) break;
		node_t * head = list_dequeue(dead_terminals);
		free(head);
		list_free(term->input_buffer_queue);
		free(term->input_buffer_queue);
		/* TODO we don't actually have a good way to clean up pthreads */
		free(term);
	}

	pid_t pid = waitpid(-1, NULL, WNOHANG);

	/* If the child has exited, we should exit. */
	term_state_t * matched = NULL;
	node_t * matched_node = NULL;
	foreach (node, terminals) {
		term_state_t * term = node->value;
		struct Terminal_Private * priv = term->priv;
		if (pid == priv->child_pid) {
			matched = term;
			matched_node = node;
			break;
		}
	}

	if (!matched) return 0;

	if (current_terminal() == matched) {
		if (matched_node->prev) {
			active_terminal = matched_node->prev->value;
		} else if (matched_node->next) {
			active_terminal = matched_node->next->value;
		}
	}

	refresh_display(active_terminal);

	list_delete(terminals, matched_node);
	free(matched_node);

	if (terminals->length == 0) return (exit_application = 1);

	struct Terminal_Private * priv = matched->priv;
	close(priv->input_buffer_semaphore[1]); /* Kills the input processing thread */
	close(priv->fd_master); /* Hangs up the TTY */
	close(priv->fd_slave);

	list_insert(dead_terminals, priv);
	termemu_free(matched);

	return 0;
}

static void mouse_event(int button, int x, int y) {
	if (current_terminal()->mouse_on & TERMEMU_MOUSE_SGR) {
		char buf[100];
		sprintf(buf,"\033[<%d;%d;%d%c", button == 3 ? 0 : button, x+1, y+1, button == 3 ? 'm' : 'M');
		handle_input_s(buf);
	} else {
		char buf[7];
		sprintf(buf, "\033[M%c%c%c", button + 32, x + 33, y + 33);
		handle_input_s(buf);
	}
}

static void redraw_mouse(void) {
	/* Redraw previous cursor position */
	if (old_x != mouse_x || old_y != mouse_y) {
		term_cell_t * cell_d = &current_terminal()->term_display[old_y * current_terminal()->width + old_x];
		cell_d->c = 0xFF;
		termemu_redraw_scrollback(current_terminal());
		termemu_redraw_selection(current_terminal());
	}
	term_cell_t * cell = &current_terminal()->term_mirror[old_y * current_terminal()->width + old_x];
	int current_background = cell->bg;
	/* Get new cursor position character */
	int cursor_color = (current_background == 12) ? 15 : 12;
	term_write_char(L'▲', mouse_x, mouse_y, cursor_color, current_background, 0);
	old_x = mouse_x;
	old_y = mouse_y;
}

static void handle_mouse_event(mouse_device_packet_t * packet) {
	if (mouse_x < 0) mouse_x = 0;
	if (mouse_y < 0) mouse_y = 0;
	if (mouse_x >= current_terminal()->width)  mouse_x = current_terminal()->width - 1;
	if (mouse_y >= current_terminal()->height) mouse_y = current_terminal()->height - 1;

	if (current_terminal()->mouse_on & TERMEMU_MOUSE_ENABLE) {
		/* TODO: Handle shift */
		if (packet->buttons & MOUSE_SCROLL_UP) {
			mouse_event(32+32, mouse_x, mouse_y);
		} else if (packet->buttons & MOUSE_SCROLL_DOWN) {
			mouse_event(32+32+1, mouse_x, mouse_y);
		}

		if (packet->buttons != button_state) {
			if (packet->buttons & LEFT_CLICK && !(button_state & LEFT_CLICK)) mouse_event(0, mouse_x, mouse_y);
			if (packet->buttons & MIDDLE_CLICK && !(button_state & MIDDLE_CLICK)) mouse_event(1, mouse_x, mouse_y);
			if (packet->buttons & RIGHT_CLICK && !(button_state & MIDDLE_CLICK)) mouse_event(2, mouse_x, mouse_y);
			if (!(packet->buttons & LEFT_CLICK) && (button_state & LEFT_CLICK)) mouse_event(3, mouse_x, mouse_y);
			if (!(packet->buttons & MIDDLE_CLICK) && (button_state & MIDDLE_CLICK)) mouse_event(3, mouse_x, mouse_y);
			if (!(packet->buttons & RIGHT_CLICK) && (button_state & MIDDLE_CLICK)) mouse_event(3, mouse_x, mouse_y);
			button_state = packet->buttons;
		} else if (current_terminal()->mouse_on & TERMEMU_MOUSE_DRAG) {
			if (old_x != mouse_x || old_y != mouse_y) {
				if (button_state & LEFT_CLICK) mouse_event(32, mouse_x, mouse_y);
				if (button_state & MIDDLE_CLICK) mouse_event(33, mouse_x, mouse_y);
				if (button_state & RIGHT_CLICK) mouse_event(34, mouse_x, mouse_y);
			}
		}

		redraw_mouse();
		return;
	}
	if (mouse_is_dragging) {
		if (packet->buttons & LEFT_CLICK) {
			termemu_selection_drag(current_terminal(), mouse_x, mouse_y);
		} else {
			mouse_is_dragging = 0;
		}
	} else if (packet->buttons & LEFT_CLICK) {
		termemu_selection_click(current_terminal(), mouse_x, mouse_y);
		mouse_is_dragging = 1;
	}
	redraw_mouse();
}

static void handle_mouse(mouse_device_packet_t * packet) {
	rel_mouse_x += packet->x_difference;
	rel_mouse_y -= packet->y_difference;

	mouse_x = rel_mouse_x / 20;
	mouse_y = rel_mouse_y / 40;

	handle_mouse_event(packet);
}

static void handle_mouse_abs(mouse_device_packet_t * packet) {
	mouse_x = packet->x_difference / mouse_r[0];
	mouse_y = packet->y_difference / mouse_r[1];

	rel_mouse_x = mouse_x * 20;
	rel_mouse_y = mouse_y * 40;

	handle_mouse_event(packet);
}

static void terminal_set_size(term_state_t * state) {
	struct Terminal_Private * term = state->priv;
	struct winsize w;
	w.ws_row = state->height;
	w.ws_col = state->width;
	w.ws_xpixel = 0;
	w.ws_ypixel = 0;
	ioctl(term->fd_master, TIOCSWINSZ, &w);
}

static term_state_t * terminal_create(int term_width, int term_height, int max_scrollback, int argc, char * argv[]) {
	struct Terminal_Private * priv = calloc(1, sizeof(struct Terminal_Private));
	term_state_t * out = termemu_init(term_width, term_height, max_scrollback, &term_callbacks);
	out->priv = priv;

	list_insert(terminals, out);

	pipe(priv->input_buffer_semaphore);
	priv->input_buffer_queue = list_create();
	pthread_create(&priv->input_buffer_thread, NULL, handle_input_writing, out);

	/* Open a PTY */
	openpty(&priv->fd_master, &priv->fd_slave, NULL, NULL, NULL);
	terminal_set_size(out);

	priv->child_pid = fork();

	if (!priv->child_pid) {
		setsid();
		/* Prepare stdin/out/err */
		dup2(priv->fd_slave, 0);
		dup2(priv->fd_slave, 1);
		dup2(priv->fd_slave, 2);

		ioctl(STDIN_FILENO, TIOCSCTTY, &(int){1});
		tcsetpgrp(STDIN_FILENO, getpid());

		signal(SIGHUP, SIG_DFL);

		/* Set the TERM environment variable. */
		putenv("TERM=toaru-vga");

		/* Execute requested initial process */
		if (terminal_login_shell_restricted) {
			char * tokens[] = {"/bin/login-loop",NULL};
			execvp(tokens[0], tokens);
		} else if (argc) {
			/* Run something specified by the terminal startup */
			execvp(argv[0], argv);
		} else {
			/* Run the user's shell */
			char * shell = getenv("SHELL");
			if (!shell) shell = "/bin/sh"; /* fallback */
			char * tokens[] = {shell,NULL};
			execvp(tokens[0], tokens);
			exit(1);
		}

		exit(127);
	}

	return out;
}

int main(int argc, char ** argv) {
	static struct option long_opts[] = {
		{"login",      no_argument,       0, 'l'},
		{"help",       no_argument,       0, 'h'},
		{0,0,0,0}
	};

	/* Read some arguments */
	int index, c;
	while ((c = getopt_long(argc, argv, "hl", long_opts, &index)) != -1) {
		switch (c) {
			case 'l':
				terminal_login_shell_restricted = 1;
				break;
			case 'h':
			case '?':
				return usage(argv);
		}
	}

	if (terminal_login_shell_restricted && optind != argc) {
		fprintf(stderr, "%s: arguments may not be provided with '--login'\n", argv[0]);
		return 1;
	}

#define TEXT_MODE_DEVICE "/dev/vga0"
	vga_text_fd = open(TEXT_MODE_DEVICE, O_RDWR);
	if (vga_text_fd < 0) {
		fprintf(stderr, "%s: %s: %s\n", argv[0], TEXT_MODE_DEVICE, strerror(errno));
		return 1;
	}

	int term_width, term_height;

	ioctl(vga_text_fd, IO_VID_WIDTH,  &term_width);
	ioctl(vga_text_fd, IO_VID_HEIGHT, &term_height);
	ioctl(vga_text_fd, IO_VGA_MOUSE_ADJ, &mouse_r);

	fflush(stdin);
	system("cursor-off");

	terminals = list_create();
	dead_terminals = list_create();

	active_terminal = terminal_create(term_width, term_height, 10000, argc-optind, &argv[optind]);

	int kfd = open("/dev/kbd", O_RDONLY);
	key_event_t event;
	int vmmouse = 0;
	mouse_device_packet_t packet;

	int mfd = open("/dev/mouse", O_RDONLY);
	int amfd = open("/dev/absmouse", O_RDONLY);
	if (amfd == -1) {
		amfd = open("/dev/vmmouse", O_RDONLY);
		vmmouse = 1;
	}

	key_event_state_t kbd_state = {0};

	/* Prune any keyboard input we got before the terminal started. */
	struct stat s;
	fstat(kfd, &s);
	for (unsigned int i = 0; i < s.st_size; i++) {
		char tmp[1];
		read(kfd, tmp, 1);
	}

	size_t fds_size = 1;
	int * fds = malloc(sizeof(int));
	int * res = malloc(sizeof(int));
	term_state_t ** term = malloc(sizeof(term_state_t*));
	int _kfd_offset = 0;
	int _mfd_offset = 0;
	int _amfd_offset = 0;

	#define BUF_SIZE 4096
	unsigned char buf[4096];
	while (!exit_application) {
		if (check_for_exit()) break;


		if (fds_size != 3 + terminals->length) {
			fds_size = 3 + terminals->length;
			fds = realloc(fds, fds_size * sizeof(int));
			term = realloc(term, fds_size * sizeof(term_state_t *));
			size_t i = 0;
			foreach(node, terminals) {
				term[i] = node->value;
				struct Terminal_Private * priv = term[i]->priv;
				fds[i] = priv->fd_master;
				i++;
			}
			fds[(_kfd_offset = (i++))] = kfd;
			fds[(_mfd_offset = (i++))] = mfd;
			fds[(_amfd_offset = (i++))] = amfd;
			res = realloc(res, fds_size * sizeof(int));
		}

		fswait3(fds_size + (amfd == -1 ? -1 : 0), fds, 200, res);

		termemu_maybe_flip_cursor(current_terminal());

		for (size_t i = 0; i < terminals->length; ++i) {
			if (res[i]) {
				struct Terminal_Private * priv = term[i]->priv;
				ssize_t r = read(priv->fd_master, buf, BUF_SIZE);
				for (ssize_t j = 0; j < r; ++j) {
					termemu_put(term[i], buf[j]);
				}
			}
		}

		if (res[_kfd_offset]) {
			int r = read(kfd, buf, 1);
			for (int i = 0; i < r; ++i) {
				if (kbd_scancode(&kbd_state, buf[i], &event)) {
					key_event(event.action == KEY_ACTION_DOWN && event.key, &event);
				}
			}
		}

		if (res[_mfd_offset]) {
			/* mouse event */
			int r = read(mfd, (char *)&packet, sizeof(mouse_device_packet_t));
			if (r > 0) {
				last_mouse_buttons = packet.buttons;
				handle_mouse(&packet);
			}
		}

		if (amfd != -1 && res[_amfd_offset]) {
			int r = read(amfd, (char *)&packet, sizeof(mouse_device_packet_t));
			if (r > 0) {
				if (!vmmouse) {
					packet.buttons = last_mouse_buttons & 0xF;
				} else {
					last_mouse_buttons = packet.buttons;
				}
				handle_mouse_abs(&packet);
			}
		}

		flip_display();
	}

	foreach(node, terminals) {
		struct Terminal_Private * priv = ((term_state_t*)node->value)->priv;
		close(priv->input_buffer_semaphore[1]);
	}

	return 0;
}
