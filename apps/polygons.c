/**
 * @brief Draw filled polygons from line segments.
 *
 * This is an older version of the polygon rasterizer that turned
 * into the TrueType gylph rasterizer. Still makes for a neat
 * little graphical demo. Should probably be updated to use
 * the glyph rasterization code instead of its own oudated
 * copy though...
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021 K. Lange
 */

#include <stdio.h>

#include <sys/fswait.h>

#include <toaru/yutani.h>
#include <toaru/graphics.h>
#include <toaru/text.h>

#define min(a,b) ((a) < (b) ? (a) : (b))

static int left, top, width, height;

static yutani_t * yctx;
static yutani_window_t * wina;
static gfx_context_t * ctx;
static int should_exit = 0;

static struct TT_Contour * shape = NULL;
static struct TT_Shape * finalizedShape = NULL;
static int last_x, last_y;
static uint32_t myColor = 0;

static void draw(void) {
	draw_fill(ctx, rgba(0,0,0,10));

	if (shape) {
		if (finalizedShape) {
			/* Oh boy */
			tt_path_paint(ctx, finalizedShape, myColor);
		}
	}
}

static void finish_draw(void) {
	flip(ctx);
	yutani_flip(yctx, wina);
}

int main (int argc, char ** argv) {
	left   = 100;
	top    = 100;
	width  = 500;
	height = 500;

	yctx = yutani_init();
	if (!yctx) {
		fprintf(stderr, "%s: failed to connect to compositor\n", argv[0]);
		return 1;
	}

	wina = yutani_window_create(yctx, width, height);
	yutani_window_move(yctx, wina, left, top);
	yutani_window_advertise_icon(yctx, wina, "polygons", "polygons");

	ctx = init_graphics_yutani_double_buffer(wina);
	draw();
	finish_draw();

	while (!should_exit) {
		int fds[1] = {fileno(yctx->sock)};
		int index = fswait2(1,fds,20);
		if (index == 0) {
			yutani_msg_t * m = yutani_poll(yctx);
			while (m) {
				switch (m->type) {
					case YUTANI_MSG_KEY_EVENT:
						{
							struct yutani_msg_key_event * ke = (void*)m->data;
							if (ke->event.action == KEY_ACTION_DOWN && ke->event.keycode == 'q') {
								should_exit = 1;
							}
						}
						break;
					case YUTANI_MSG_WINDOW_MOUSE_EVENT:
						{
							struct yutani_msg_window_mouse_event * me = (void*)m->data;
							float x = (float)me->new_x;
							float y = (float)me->new_y;
							if (me->command == YUTANI_MOUSE_EVENT_DOWN && me->buttons & YUTANI_MOUSE_BUTTON_LEFT) {
								if (!shape) {
									shape = tt_contour_start(x, y);
								} else {
									shape = tt_contour_line_to(shape, x, y);
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
								last_x = x;
								last_y = y;
								myColor = rgb(rand() % 255,rand() % 255,rand() % 255);
								draw();
								finish_draw();
							} else if (shape) {
								draw();
								draw_line(ctx,
									last_x,
									x,
									last_y,
									y,
									rgb(0,200,0));
								finish_draw();
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
