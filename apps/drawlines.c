/**
 * @brief drawlines - Draw random lines into a GUI window
 *
 * The original compositor demo application, this dates all the
 * way back to the original pre-Yutani compositor. Opens a very
 * basic window (no decorations) and randomly fills it with
 * colorful lines.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2021 K. Lange
 */
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <time.h>
#include <sched.h>
#include <math.h>

#include <sys/fswait.h>

#include <toaru/yutani.h>
#include <toaru/graphics.h>
#include <toaru/decorations.h>

static int width, height;

static yutani_t * yctx;
static yutani_window_t * wina;
static gfx_context_t * _ctx;
static int should_exit = 0;
static int thick = 0;

static void redraw_borders(void) {
	render_decorations(wina, _ctx, "drawlines");
	flip(_ctx);
	yutani_flip(yctx, wina);
}

static void redraw_full(void) {
	draw_fill(_ctx, rgb(0,0,0));
	redraw_borders();
}

static void draw(void) {
	struct decor_bounds bounds;
	decor_get_bounds(wina, &bounds);

	width = wina->width - bounds.width;
	height = wina->height - bounds.height;

	gfx_context_t * ctx = init_graphics_subregion(_ctx, bounds.left_width, bounds.top_height, width, height);

	if (thick) {
		draw_line_aa(ctx, rand() % width, rand() % width, rand() % height, rand() % height, rgb(rand() % 255,rand() % 255,rand() % 255), (float)thick);
	} else {
		draw_line(ctx, rand() % width, rand() % width, rand() % height, rand() % height, rgb(rand() % 255,rand() % 255,rand() % 255));
	}

	free(ctx);
	flip(_ctx);
	yutani_flip(yctx, wina);
}

static void resize_finish(int w, int h) {
	yutani_window_resize_accept(yctx, wina, w, h);
	reinit_graphics_yutani(_ctx, wina);
	redraw_full();
	yutani_window_resize_done(yctx, wina);
}

static int show_usage(char * argv[]) {
#define X_S "\033[3m"
#define X_E "\033[0m"
	fprintf(stderr,
			"%s - graphical demo, draws lines randomly\n"
			"\n"
			"usage: %s [-t " X_S "thickness" X_E "]\n"
			"\n"
			" -t " X_S "thickness   draw with anti-aliasing and the specified thickness" X_E "\n"
			" -?             " X_S "show this help text" X_E "\n"
			"\n", argv[0], argv[0]);
	return 1;
}


int main (int argc, char ** argv) {
	width  = 500;
	height = 500;

	srand(time(NULL));

	int c;
	while ((c = getopt(argc, argv, "t:?")) != -1) {
		switch (c) {
			case 't':
				thick = atoi(optarg);
				break;
			case '?':
				return show_usage(argv);
		}
	}

	yctx = yutani_init();
	if (!yctx) {
		fprintf(stderr, "%s: failed to connect to compositor\n", argv[0]);
		return 1;
	}

	wina = yutani_window_create(yctx, width, height);

	init_decorations();

	yutani_window_move(yctx, wina, 100, 100);
	yutani_window_advertise_icon(yctx, wina, "drawlines", "drawlines");

	_ctx = init_graphics_yutani_double_buffer(wina);
	redraw_full();

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
						break;
				}
				switch (m->type) {
					case YUTANI_MSG_KEY_EVENT:
						{
							struct yutani_msg_key_event * ke = (void*)m->data;
							if (ke->wid == wina->wid) {
								if (ke->event.action == KEY_ACTION_DOWN && ke->event.keycode == 'q') {
									should_exit = 1;
									sched_yield();
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
		draw();
	}

	yutani_close(yctx, wina);

	return 0;
}
