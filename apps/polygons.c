/**
 * @brief Draw filled polygons from line segments.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021-2026 K. Lange
 */

#include <stdio.h>

#include <sys/fswait.h>

#include <toaru/yutani.h>
#include <toaru/graphics.h>
#include <toaru/text.h>
#include <toaru/decorations.h>

#define min(a,b) ((a) < (b) ? (a) : (b))

static yutani_t * yctx;
static yutani_window_t * wina;
static gfx_context_t * _ctx;
static gfx_context_t * ctx = NULL;
static int should_exit = 0;

static struct TT_Contour * shape = NULL;
static struct TT_Shape * finalizedShape = NULL;
static int last_x, last_y;
static uint32_t myColor = 0;
static int is_degenerate = 1;

static void draw(void) {
	draw_fill(ctx, rgb(255,255,255));

	if (shape) {
		if (is_degenerate == 2) {
			float x1, y1, x2, y2;
			if (tt_contour_get_edge(shape, -1, &x1, &y1, &x2, &y2) >= 0) {
				draw_line(ctx, x1, x2, y1, y2, rgb(0,0,0));
			}
		}
		if (finalizedShape) {
			/* Oh boy */
			tt_path_paint(ctx, finalizedShape, myColor);
		}
	}
}

static void finish_draw(void) {
	flip(_ctx);
	yutani_flip(yctx, wina);
}

static void redraw_borders(void) {
	render_decorations(wina, _ctx, "polygons");
}

static void reinit_subcontext(void) {
	if (ctx) free(ctx);
	struct decor_bounds bounds;
	decor_get_bounds(wina, &bounds);

	ctx = init_graphics_subregion(_ctx, bounds.left_width, bounds.top_height, wina->width - bounds.width, wina->height - bounds.height);
	redraw_borders();
}

static void resize_finish(int w, int h) {
	yutani_window_resize_accept(yctx, wina, w, h);
	reinit_graphics_yutani(_ctx, wina);
	reinit_subcontext();
	draw();
	finish_draw();
	yutani_window_resize_done(yctx, wina);
}

int main (int argc, char ** argv) {
	yctx = yutani_init();
	if (!yctx) {
		fprintf(stderr, "%s: failed to connect to compositor\n", argv[0]);
		return 1;
	}

	init_decorations();

	wina = yutani_window_create(yctx, 500, 500);
	yutani_window_move(yctx, wina, 100, 100);
	yutani_window_advertise_icon(yctx, wina, "polygons", "polygons");

	_ctx = init_graphics_yutani_double_buffer(wina);
	reinit_subcontext();
	draw();
	finish_draw();

	while (!should_exit) {
		int fds[1] = {fileno(yctx->sock)};
		int index = fswait2(1,fds,20);
		if (index == 0) {
			yutani_msg_t * m = yutani_poll(yctx);
			while (m) {
				switch (decor_handle_event_flags(yctx, m, DECOR_HANDLE_SIMPLE)) {
					case DECOR_CLOSE:
						should_exit = 1;
						break;
					case DECOR_REDRAW:
						redraw_borders();
						finish_draw();
						break;
				}
				switch (m->type) {
					case YUTANI_MSG_KEY_EVENT:
						{
							struct yutani_msg_key_event * ke = (void*)m->data;
							if (ke->wid == wina->wid) {
								if (ke->event.action == KEY_ACTION_DOWN && ke->event.keycode == 'q') {
									should_exit = 1;
								}
							}
						}
						break;
					case YUTANI_MSG_WINDOW_MOUSE_EVENT:
						{
							struct yutani_msg_window_mouse_event * me = (void*)m->data;
							if (me->wid == wina->wid) {
								struct decor_bounds bounds;
								decor_get_bounds(wina, &bounds);
								float x = (float)me->new_x - bounds.left_width;
								float y = (float)me->new_y - bounds.top_height;
								if (x < 0.0 || y < 0.0 || x >= wina->width - bounds.width || y >= wina->height - bounds.height) break;
								if (me->command == YUTANI_MOUSE_EVENT_DOWN && me->buttons & YUTANI_MOUSE_BUTTON_LEFT) {
									if (!shape) {
										shape = tt_contour_start(x, y);
										is_degenerate = 1;
									} else {
										shape = tt_contour_line_to(shape, x, y);
										if (is_degenerate == 1) is_degenerate = 2;
										else is_degenerate = 0;
									}
									last_x = x;
									last_y = y;
									if (finalizedShape) free(finalizedShape);
									finalizedShape = tt_contour_finish(shape);
									myColor = rgb(rand() % 255,rand() % 255,rand() % 255);
									draw();
									finish_draw();
								} else if (me->buttons & YUTANI_MOUSE_BUTTON_RIGHT) {
									shape = tt_contour_move_to(shape, x, y);
									is_degenerate = 1;
									last_x = x;
									last_y = y;
									myColor = rgb(rand() % 255,rand() % 255,rand() % 255);
									draw();
									finish_draw();
								} else if (shape) {
									int inside  = finalizedShape ? tt_path_contains(finalizedShape, x, y) : 0;
									draw();
									draw_line(ctx,
										last_x,
										x,
										last_y,
										y,
										inside ? rgb(200,0,0) : rgb(0,200,0));
									finish_draw();
								}
							}
						}
						break;
					case YUTANI_MSG_RESIZE_OFFER:
						{
							struct yutani_msg_window_resize * wr = (void*)m->data;
							if (wr->wid == wina->wid) {
								resize_finish(wr->width, wr->height);
							}
						}
						break;
					case YUTANI_MSG_WINDOW_CLOSE:
					case YUTANI_MSG_SESSION_END:
						should_exit = 1;
						break;
					default:
						break;
				}
				free(m);
				m = yutani_poll_async(yctx);
			}
		}
	}

	yutani_close(yctx, wina);

	return 0;
}
