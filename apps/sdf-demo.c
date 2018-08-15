/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 *
 * sdf-demo - SDF font rasterizer demo
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <getopt.h>

#include <toaru/yutani.h>
#include <toaru/graphics.h>
#include <toaru/decorations.h>
#include <toaru/sdf.h>

#define DIRECT_OFFSET(x,y) ((x) + (y) * window->width)

/*
 * Macros make verything easier.
 */
#define SPRITE(sprite,x,y) sprite->bitmap[sprite->width * (y) + (x)]

#define GFX_(xpt, ypt) ((uint32_t *)window->buffer)[DIRECT_OFFSET(xpt+decor_left_width,ypt+decor_top_height)]

/* Pointer to graphics memory */
static yutani_t * yctx;
static yutani_window_t * window = NULL;
static gfx_context_t * ctx = NULL;

static int width = 500;
static int height = 500;

static int left = 200;
static int top = 200;

static int size = 16;

static void decors() {
	render_decorations(window, ctx, "SDF Demo");
}

void redraw() {
	draw_fill(ctx, rgb(255,255,255));

	decors();

	draw_sdf_string(ctx, 30, 30, "ABCDEFGHIJKLMNOPQRSTUVWXYZABC", size, rgb(0,0,0), SDF_FONT_THIN);
	draw_sdf_string(ctx, 30, 60, "abcdefghijklmnopqrstuvwxyzabc", size, rgb(0,0,0), SDF_FONT_THIN);
	draw_sdf_string(ctx, 30, 90, "ABCDEFGHIJKLMNOPQRSTUVWXYZABC", size, rgb(0,0,0), SDF_FONT_BOLD);
	draw_sdf_string(ctx, 30,120, "abcdefghijklmnopqrstuvwxyzabc", size, rgb(0,0,0), SDF_FONT_BOLD);
}

void resize_finish(int w, int h) {
	yutani_window_resize_accept(yctx, window, w, h);
	reinit_graphics_yutani(ctx, window);

	width  = w - decor_left_width - decor_right_width;
	height = h - decor_top_height - decor_bottom_height;

	redraw();

	yutani_window_resize_done(yctx, window);
	yutani_flip(yctx, window);
}


int main(int argc, char * argv[]) {

	yctx = yutani_init();
	if (!yctx) {
		fprintf(stderr, "%s: failed to connect to compositor\n", argv[0]);
		return 1;
	}
	init_decorations();

	window = yutani_window_create(yctx, width + decor_width(), height + decor_height());
	yutani_window_move(yctx, window, left, top);

	yutani_window_advertise_icon(yctx, window, "SDF Demo", "sdf");

	ctx = init_graphics_yutani(window);

	redraw();
	yutani_flip(yctx, window);

	int playing = 1;
	while (playing) {
		yutani_msg_t * m = yutani_poll(yctx);
		if (m) {
			switch (m->type) {
				case YUTANI_MSG_KEY_EVENT:
					{
						struct yutani_msg_key_event * ke = (void*)m->data;
						if (ke->event.action == KEY_ACTION_DOWN && ke->event.keycode == 'q') {
							playing = 0;
						} else if (ke->event.action == KEY_ACTION_DOWN) {
							if (size <= 20) {
								size += 1;
							} else if (size > 20) {
								size += 5;
							}
							if (size > 100) {
								size = 1;
							}
							redraw();
							yutani_flip(yctx,window);
						}
					}
					break;
				case YUTANI_MSG_WINDOW_FOCUS_CHANGE:
					{
						struct yutani_msg_window_focus_change * wf = (void*)m->data;
						yutani_window_t * win = hashmap_get(yctx->windows, (void*)wf->wid);
						if (win) {
							win->focused = wf->focused;
							decors();
							yutani_flip(yctx, window);
						}
					}
					break;
				case YUTANI_MSG_RESIZE_OFFER:
					{
						struct yutani_msg_window_resize * wr = (void*)m->data;
						resize_finish(wr->width, wr->height);
					}
					break;
				case YUTANI_MSG_WINDOW_MOUSE_EVENT:
					{
						int result = decor_handle_event(yctx, m);
						switch (result) {
							case DECOR_CLOSE:
								playing = 0;
								break;
							default:
								/* Other actions */
								break;
						}
					}
					break;
				case YUTANI_MSG_SESSION_END:
					playing = 0;
					break;
				default:
					break;
			}
		}
		free(m);
	}

	yutani_close(yctx, window);

	return 0;
}

