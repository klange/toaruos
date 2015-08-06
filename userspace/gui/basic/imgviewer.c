/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2015 Kevin Lange
 *
 * Image Viewer
 *
 */
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <syscall.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>

#include "lib/yutani.h"
#include "lib/graphics.h"
#include "lib/decorations.h"
#include "gui/ttk/ttk.h"

#include "lib/trace.h"
#define TRACE_APP_NAME "image-viewer"

static yutani_t * yctx;

static yutani_window_t * win;
static gfx_context_t * ctx;

static cairo_surface_t * surface_win;

static cairo_t * cr_win;

static int should_exit = 0;

static int center_x(int x) {
	return (yctx->display_width - x) / 2;
}

static int center_y(int y) {
	return (yctx->display_height - y) / 2;
}

static int center_win_x(int x) {
	return (win->width - x) / 2;
}

static int button_width = 100;
static int button_height = 32;
static int button_focused = 0;
static void draw_next_button(int is_exit) {
	if (button_focused == 1) {
		/* hover */
		_ttk_draw_button_hover(cr_win, center_win_x(button_width), 400, button_width, button_height, is_exit ? "Exit" : "Next");
	} else if (button_focused == 2) {
		/* Down */
		_ttk_draw_button_select(cr_win, center_win_x(button_width), 400, button_width, button_height, is_exit ? "Exit" : "Next");
	} else {
		/* Something else? */
		_ttk_draw_button(cr_win, center_win_x(button_width), 400, button_width, button_height, is_exit ? "Exit" : "Next");
	}
}

static void draw_centered_label(int y, int size, char * label) {
	set_font_face(FONT_SANS_SERIF);
	set_font_size(size);

	int x = center_win_x(draw_string_width(label));
	draw_string(ctx, x, y, rgb(0,0,0), label);
}

static sprite_t image;
static char * file_name = NULL;
static void draw_image(void) {
	draw_sprite(ctx, &image, decor_left_width, decor_top_height);
}

static void redraw(void) {
	draw_fill(ctx, rgb(TTK_BACKGROUND_DEFAULT));
	render_decorations(win, ctx, file_name);
	draw_image();
	flip(ctx);
	yutani_flip(yctx, win);
}

int main(int argc, char * argv[]) {

	if (argc < 2) {
		fprintf(stderr, "Usage: %s image_file\n", argv[0]);
		return 1;
	}

	file_name = argv[1];

	load_sprite_png(&image, argv[1]);

	yctx = yutani_init();

	init_decorations();

	win = yutani_window_create(yctx, image.width + decor_width(), image.height + decor_height());
	yutani_window_move(yctx, win, center_x(image.width + decor_width()), center_y(image.height + decor_height()));
	ctx = init_graphics_yutani_double_buffer(win);

	int stride;

	stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, win->width);
	surface_win = cairo_image_surface_create_for_data(ctx->backbuffer, CAIRO_FORMAT_ARGB32, win->width, win->height, stride);
	cr_win = cairo_create(surface_win);

	yutani_window_advertise_icon(yctx, win, "Image Viewer", "image-viewer");

	redraw();

	yutani_focus_window(yctx, win->wid);

	while (!should_exit) {
		yutani_msg_t * m = yutani_poll(yctx);

		if (m) {
			switch (m->type) {
				case YUTANI_MSG_KEY_EVENT:
					{
						struct yutani_msg_key_event * ke = (void*)m->data;
						if (ke->event.key == 'q' && ke->event.action == KEY_ACTION_DOWN) {
							should_exit = 1;
						}
					}
					break;
				case YUTANI_MSG_WINDOW_FOCUS_CHANGE:
					{
						struct yutani_msg_window_focus_change * wf = (void*)m->data;
						if (wf->wid == win->wid) {
							win->focused = wf->focused;
							redraw();
						}
					}
					break;
				case YUTANI_MSG_WINDOW_MOUSE_EVENT:
					{
						struct yutani_msg_window_mouse_event * me = (void*)m->data;
						if (me->wid != win->wid) break;
						int result = decor_handle_event(yctx, m);
						switch (result) {
							case DECOR_CLOSE:
								should_exit = 1;
								break;
							default:
								/* Other actions */
								break;
						}
					}
					break;
				case YUTANI_MSG_SESSION_END:
					should_exit = 1;
					break;
				default:
					break;
			}
			free(m);
		}
	}

	return 0;
}
