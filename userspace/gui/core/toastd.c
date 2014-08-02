/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014 Kevin Lange
 */
/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Toast (Notification) Daemon
 *
 */
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <math.h>
#include <signal.h>
#include <time.h>

#include "lib/yutani.h"
#include "lib/graphics.h"
#include "lib/shmemfonts.h"
#include "lib/pex.h"
#include "lib/pthread.h"
#include "lib/spinlock.h"

#include "lib/toastd.h"

static yutani_t * yctx;
static list_t * notifications;
static FILE * toastd_server;
static int notification_lock = 0;
static int exit_app = 0;
static sprite_t * toast_bg;

#define TOASTD_NAME "toastd"

#define TOAST_WIDTH  310
#define TOAST_HEIGHT 110
#define TOAST_PAD      8
#define TOAST_TEXT_X  10
#define TOAST_HEAD_Y  22
#define TOAST_BODY_Y  40
#define TOAST_LINE_HT 14
#define TOAST_HEAD_S  14
#define TOAST_BODY_S  12

#define TOAST_OFFSET_X 20
#define TOAST_OFFSET_Y 30

typedef struct {
	int    ttl;
	char * title;
	char * content;
	yutani_window_t * window;
	int    stack;
} notification_int_t;

static void add_toast(notification_t * incoming) {
	notification_int_t * toast = malloc(sizeof(notification_int_t));

	toast->ttl = incoming->ttl + time(NULL);
	toast->title = strdup(incoming->strings);
	toast->content = strdup(incoming->strings + 1 + strlen(incoming->strings));

	fprintf(stderr, "ttl=%d, title=\"%s\" content=\"%s\"\n", toast->ttl, toast->title, toast->content);

	spin_lock(&notification_lock);

	toast->window = yutani_window_create(yctx, TOAST_WIDTH, TOAST_HEIGHT);

	/* Find best location */
	int i = 0, hit_something = 1;
	while (hit_something) {
		hit_something = 0;
		foreach(node, notifications) {
			notification_int_t * toast = node->value;
			if (toast->stack == i) {
				i++;
				hit_something = 1;
			}
		}
	}

	toast->stack = i;

	yutani_window_move(yctx, toast->window, yctx->display_width - TOAST_WIDTH - TOAST_OFFSET_X, TOAST_OFFSET_Y + (TOAST_HEIGHT + TOAST_PAD) * i);

	gfx_context_t * ctx = init_graphics_yutani(toast->window);
	draw_sprite(ctx, toast_bg, 0, 0);

	set_font_face(FONT_SANS_SERIF_BOLD);
	set_font_size(TOAST_HEAD_S);
	draw_string(ctx, TOAST_TEXT_X, TOAST_HEAD_Y, rgb(255,255,255), toast->title);

	char * str = toast->content;
	char * end = toast->content + strlen(toast->content);
	unsigned int line = 0;
	while (str) {
		if (line == 5) break;

		char * next = strstr(str, "\n");
		if (next) {
			next[0] = '\0';
			next++;
		}

		set_font_face(FONT_SANS_SERIF);
		set_font_size(TOAST_BODY_S);
		draw_string(ctx, TOAST_TEXT_X, TOAST_BODY_Y + line * TOAST_LINE_HT, rgb(255,255,255), str);

		str = next;
		line++;
	}

	free(ctx);
	yutani_flip(yctx, toast->window);

	list_insert(notifications, toast);

	spin_unlock(&notification_lock);
}

static void * toastd_handler(void * garbage) {
	(void)garbage;

	setenv("TOASTD", TOASTD_NAME, 1);
	toastd_server = pex_bind(TOASTD_NAME);

	while (!exit_app) {
		pex_packet_t * p = calloc(PACKET_SIZE, 1);
		if (pex_listen(toastd_server, p) > 0) {

			if (p->size == 0) {
				/* Connection closed notification */
				free(p);
				continue;
			}

			notification_t * toast = (void *)p->data;
			add_toast(toast);

			free(p);
		}
	}
}

void sig_pass(int sig) {
	(void)sig;
}

static void * closer_handler(void * garbage) {
	signal(SIGWINCH, sig_pass);

	while (!exit_app) {
		time_t now = time(NULL);

		spin_lock(&notification_lock);

		list_t * tmp = list_create();

		foreach(node, notifications) {
			notification_int_t * toast = node->value;
			if (toast->window && toast->ttl <= now) {
				yutani_close(yctx, toast->window);
				free(toast->title);
				free(toast->content);
				list_insert(tmp, node);
			}
		}

		foreach(node, tmp) {
			list_delete(notifications, node->value);
		}
		list_free(tmp);
		free(tmp);
		spin_unlock(&notification_lock);

		usleep(500000);
	}

	foreach(node, notifications) {
		notification_int_t * toast = node->value;

		if (toast->window) {
			yutani_close(yctx, toast->window);
		}

	}
}

int main (int argc, char ** argv) {
	yctx = yutani_init();

	notifications = list_create();

	toast_bg = malloc(sizeof(sprite_t));
	load_sprite_png(toast_bg, "/usr/share/ttk/toast/default.png");

	init_shmemfonts();

	pthread_t toastd_thread;
	pthread_create(&toastd_thread, NULL, toastd_handler, NULL);

	pthread_t closer_thread;
	pthread_create(&closer_thread, NULL, closer_handler, NULL);

	while (!exit_app) {
		yutani_msg_t * m = yutani_poll(yctx);
		if (m) {
			switch (m->type) {
				case YUTANI_MSG_SESSION_END:
					exit_app = 1;
					break;
			}
			free(m);
		}
	}

	pthread_kill(toastd_thread, SIGINT);
	pthread_kill(closer_thread, SIGWINCH);

	return 0;
}
