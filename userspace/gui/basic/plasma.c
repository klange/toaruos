/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2014 Kevin Lange
 */
/*
 * test-gfx
 *
 * Windowed graphical test application.
 */
#include <stdlib.h>
#include <assert.h>
#include <math.h>
#include <syscall.h>

#include "lib/yutani.h"
#include "lib/graphics.h"
#include "lib/decorations.h"
#include "lib/pthread.h"
#include "lib/spinlock.h"

#define dist(a,b,c,d) sqrt((double)(((a) - (c)) * ((a) - (c)) + ((b) - (d)) * ((b) - (d))))

static yutani_t * yctx;
static yutani_window_t * wina;
static int should_exit = 0;

uint16_t win_width;
uint16_t win_height;

uint16_t off_x;
uint16_t off_y;

static int volatile draw_lock = 0;

gfx_context_t * ctx;

void redraw_borders() {
	render_decorations(wina, ctx, "🔥 Plasma 🔥");
}

uint32_t hsv_to_rgb(int h, float s, float v) {
	float c  = v * s;
	float hp = (float)h / 42.6666666f;
	float x = c * (1.0 - fabs(fmod(hp, 2) - 1.0));
	float m = v - c;
	float rp, gp, bp;
	if (hp < 1.0)      { rp = c; gp = x; bp = 0; }
	else if (hp < 2.0) { rp = x; gp = c; bp = 0; }
	else if (hp < 3.0) { rp = 0; gp = c; bp = x; }
	else if (hp < 4.0) { rp = 0; gp = x; bp = c; }
	else if (hp < 5.0) { rp = x; gp = 0; bp = c; }
	else if (hp < 6.0) { rp = c; gp = 0; bp = x; }
	else               { rp = 0; gp = 0; bp = 0; }
	return rgb((rp + m) * 255, (gp + m) * 255, (bp + m) * 255);
}

void * draw_thread(void * garbage) {
	(void)garbage;

	double time = 0;

	/* Generate a palette */
	uint32_t palette[256];
	for (int x = 0; x < 256; ++x) {
		palette[x] = hsv_to_rgb(x,1.0,1.0);
	}

	while (!should_exit) {

		time += 1.0;

		int w = win_width;
		int h = win_height;

		spin_lock(&draw_lock);
		for (int x = 0; x < win_width; ++x) {
			for (int y = 0; y < win_height; ++y) {
				double value = sin(dist(x + time, y, 128.0, 128.0) / 8.0)
					+ sin(dist(x, y, 64.0, 64.0) / 8.0)
					+ sin(dist(x, y + time / 7, 192.0, 64) / 7.0)
					+ sin(dist(x, y, 192.0, 100.0) / 8.0);
				GFX(ctx, x + off_x, y + off_y) = palette[(int)((value + 4) * 32)];
			}
		}
		redraw_borders();
		flip(ctx);
		yutani_flip(yctx, wina);
		spin_unlock(&draw_lock);
		syscall_yield();
	}
}

void resize_finish(int w, int h) {
	yutani_window_resize_accept(yctx, wina, w, h);
	reinit_graphics_yutani(ctx, wina);

	win_width  = w - decor_width();
	win_height = h - decor_height();

	yutani_window_resize_done(yctx, wina);
}

int main (int argc, char ** argv) {
	yctx = yutani_init();

	win_width  = 100;
	win_height = 100;

	init_decorations();

	off_x = decor_left_width;
	off_y = decor_top_height;

	/* Do something with a window */
	wina = yutani_window_create(yctx, win_width + decor_width(), win_height + decor_height());
	yutani_window_move(yctx, wina, 300, 300);

	ctx = init_graphics_yutani_double_buffer(wina);

	draw_fill(ctx, rgb(0,0,0));
	redraw_borders();
	flip(ctx);
	yutani_flip(yctx, wina);

	yutani_window_advertise(yctx, wina, "Graphics Test");

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
						}
					}
					break;
				case YUTANI_MSG_WINDOW_FOCUS_CHANGE:
					{
						struct yutani_msg_window_focus_change * wf = (void*)m->data;
						yutani_window_t * win = hashmap_get(yctx->windows, (void*)wf->wid);
						if (win) {
							win->focused = wf->focused;
						}
					}
					break;
				case YUTANI_MSG_SESSION_END:
					should_exit = 1;
					break;
				case YUTANI_MSG_RESIZE_OFFER:
					{
						struct yutani_msg_window_resize * wr = (void*)m->data;
						spin_lock(&draw_lock);
						resize_finish(wr->width, wr->height);
						spin_unlock(&draw_lock);
					}
					break;
				case YUTANI_MSG_WINDOW_MOUSE_EVENT:
					if (decor_handle_event(yctx, m) == DECOR_CLOSE) {
						should_exit = 1;
					}
					break;
				default:
					break;
			}
			free(m);
		}
	}

	yutani_close(yctx, wina);
	return 0;
}
