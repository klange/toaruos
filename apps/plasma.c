/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2018 K. Lange
 *
 * plasma - Draw animated plasma in a window
 *
 */
#include <stdlib.h>
#include <assert.h>
#include <math.h>
#include <wait.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>

#include <toaru/yutani.h>
#include <toaru/graphics.h>
#include <toaru/decorations.h>
#include <toaru/spinlock.h>
#include <toaru/menu.h>

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

void sigint_handler() {
	should_exit = 1;
}

void redraw_borders() {
	render_decorations(wina, ctx, "Plasma");
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
		sched_yield();
	}
	return NULL;
}

void resize_finish(int w, int h) {
	yutani_window_resize_accept(yctx, wina, w, h);
	reinit_graphics_yutani(ctx, wina);

	struct decor_bounds bounds;
	decor_get_bounds(wina, &bounds);

	win_width  = w - bounds.width;
	win_height = h - bounds.height;
	off_x = bounds.left_width;
	off_y = bounds.top_height;

	yutani_window_resize_done(yctx, wina);
}

int main (int argc, char ** argv) {
	yctx = yutani_init();
	if (!yctx) {
		fprintf(stderr, "%s: failed to connect to compositor\n", argv[0]);
		return 1;
	}

	win_width  = 300;
	win_height = 300;

	init_decorations();

	struct decor_bounds bounds;
	decor_get_bounds(NULL, &bounds);

	/* Do something with a window */
	wina = yutani_window_create(yctx, win_width + bounds.width, win_height + bounds.height);
	yutani_window_move(yctx, wina, 300, 300);

	decor_get_bounds(wina, &bounds);
	off_x = bounds.left_width;
	off_y = bounds.top_height;
	win_width  = wina->width - bounds.width;
	win_height = wina->height - bounds.height;

	ctx = init_graphics_yutani_double_buffer(wina);

	draw_fill(ctx, rgb(0,0,0));
	redraw_borders();
	flip(ctx);
	yutani_flip(yctx, wina);

	yutani_window_advertise_icon(yctx, wina, "Plasma", "plasma");

	pthread_t thread;
	pthread_create(&thread, NULL, draw_thread, NULL);

	signal(SIGINT, sigint_handler);
	while (!should_exit) {
		yutani_msg_t * m = yutani_poll(yctx);
		while (m) {
			menu_process_event(yctx, m);
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
						if (win && win == wina) {
							win->focused = wf->focused;
						}
					}
					break;
				case YUTANI_MSG_WINDOW_CLOSE:
				case YUTANI_MSG_SESSION_END:
					should_exit = 1;
					break;
				case YUTANI_MSG_RESIZE_OFFER:
					{
						struct yutani_msg_window_resize * wr = (void*)m->data;
						if (wr->wid == wina->wid) {
							spin_lock(&draw_lock);
							resize_finish(wr->width, wr->height);
							spin_unlock(&draw_lock);
						}
					}
					break;
				case YUTANI_MSG_WINDOW_MOUSE_EVENT:
					{
						struct yutani_msg_window_mouse_event * me = (void*)m->data;
						switch (decor_handle_event(yctx, m)) {
							case DECOR_CLOSE:
								should_exit = 1;
								break;
							case DECOR_RIGHT:
								decor_show_default_menu(wina, wina->x + me->new_x, wina->y + me->new_y);
								break;
						}
					}
					break;
				default:
					break;
			}
			free(m);
			m = yutani_poll_async(yctx);
		}
	}

	wait(NULL);

	yutani_close(yctx, wina);
	return 0;
}
