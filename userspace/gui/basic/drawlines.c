/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2014 Kevin Lange
 */
/*
 * drawlines
 *
 * Test application to draw lines to a window.
 */
#include <stdlib.h>
#include <assert.h>
#include <syscall.h>
#include <unistd.h>

#include "lib/yutani.h"
#include "lib/graphics.h"
#include "lib/pthread.h"

static int left, top, width, height;

static yutani_t * yctx;
static yutani_window_t * wina;
static gfx_context_t * ctx;
static int should_exit = 0;

static int32_t min(int32_t a, int32_t b) {
	return (a < b) ? a : b;
}

static int32_t max(int32_t a, int32_t b) {
	return (a > b) ? a : b;
}

void * draw_thread(void * garbage) {
	(void)garbage;
	while (!should_exit) {
		draw_line(ctx, rand() % width, rand() % width, rand() % height, rand() % height, rgb(rand() % 255,rand() % 255,rand() % 255));
		yutani_flip(yctx, wina);
		usleep(16666);
	}
	pthread_exit(0);
}

int main (int argc, char ** argv) {
	left   = 100;
	top    = 100;
	width  = 500;
	height = 500;

	yctx = yutani_init();
	wina = yutani_window_create(yctx, width, height);
	yutani_window_move(yctx, wina, left, top);

	ctx = init_graphics_yutani(wina);
	draw_fill(ctx, rgb(0,0,0));

	pthread_t thread;
	pthread_create(&thread, NULL, draw_thread, NULL);

	while (!should_exit) {
		yutani_msg_t * m = yutani_poll(yctx);
		if (m) {
			switch (m->type) {
				case YUTANI_MSG_KEY_EVENT:
					{
						struct yutani_msg_key_event * ke = (void*)m->data;
						if (ke->event.action == KEY_ACTION_DOWN && ke->event.keycode == 'q') {
							should_exit = 1;
							syscall_yield();
						}
					}
					break;
				case YUTANI_MSG_SESSION_END:
					should_exit = 1;
					break;
				default:
					break;
			}
		}
		free(m);
	}

	yutani_close(yctx, wina);

	return 0;
}
