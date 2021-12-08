/**
 * @brief TrueType font previewer
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021 K. Lange
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include <toaru/yutani.h>
#include <toaru/graphics.h>
#include <toaru/decorations.h>
#include <toaru/menu.h>
#include <toaru/text.h>

/* Pointer to graphics memory */
static yutani_t * yctx;
static yutani_window_t * window = NULL;
static gfx_context_t * ctx = NULL;
static struct TT_Font * tt_font = NULL;

static int decor_left_width = 0;
static int decor_top_height = 0;
static int decor_right_width = 0;
static int decor_bottom_height = 0;
static int decor_width = 0;
static int decor_height = 0;

static int width = 640;
static int height = 480;

char * tt_get_name_string(struct TT_Font * font, int identifier);
char * preview_string = "The quick brown fox jumps over the lazy dog.";
char * tt_font_name = NULL;
char window_title[1024] = "Font Preview";

void redraw(void) {
	draw_fill(ctx, rgb(255,255,255));

	int y = 10;

	if (tt_font_name) {
		tt_set_size(tt_font, 48);
		y += 48;
		tt_draw_string(ctx, tt_font, decor_left_width + 10, decor_top_height + y, tt_font_name, rgb(0,0,0));
		y += 10;
	}

	tt_set_size(tt_font, 22);
	y += 26;
	tt_draw_string(ctx, tt_font, decor_left_width + 10, decor_top_height + y, "abcdefghijklmnopqrstuvwxyz", rgb(0,0,0));
	y += 26;
	tt_draw_string(ctx, tt_font, decor_left_width + 10, decor_top_height + y, "ABCDEFGHIJKLMNOPQRSTUVWXYZ", rgb(0,0,0));
	y += 26;
	tt_draw_string(ctx, tt_font, decor_left_width + 10, decor_top_height + y, "0123456789.:,;(*!?')", rgb(0,0,0));
	y += 10;

	int sizes[] = {7,10,13,16,19,22,25,48,64,92,0};
	for (int * s = sizes; *s; ++s) {
		tt_set_size(tt_font, *s);
		y += *s + 4;
		tt_draw_string(ctx, tt_font, decor_left_width + 10, decor_top_height + y, preview_string, rgb(0,0,0));
	}

	render_decorations(window, ctx, window_title);

	flip(ctx);
}

void resize_finish(int w, int h) {
	yutani_window_resize_accept(yctx, window, w, h);
	reinit_graphics_yutani(ctx, window);

	struct decor_bounds bounds;
	decor_get_bounds(window, &bounds);

	decor_left_width = bounds.left_width;
	decor_top_height = bounds.top_height;
	decor_right_width = bounds.right_width;
	decor_bottom_height = bounds.bottom_height;
	decor_width = bounds.width;
	decor_height = bounds.height;

	width  = w - decor_left_width - decor_right_width;
	height = h - decor_top_height - decor_bottom_height;

	redraw();

	yutani_window_resize_done(yctx, window);
	yutani_flip(yctx, window);
}

int main(int argc, char * argv[]) {

	if (argc < 2) {
		fprintf(stderr, "usage: %s FONT\n", argv[0]);
		return 1;
	}

	tt_font = tt_font_from_file(argv[1]);

	if (!tt_font) {
		fprintf(stderr, "%s: failed to load font\n", argv[0]);
		return 1;
	}

	yctx = yutani_init();
	if (!yctx) {
		fprintf(stderr, "%s: failed to connect to compositor\n", argv[0]);
		return 1;
	}
	init_decorations();

	if (argc > 2) {
		preview_string = argv[2];
	} else {
		char * maybe = tt_get_name_string(tt_font, 19);
		if (maybe) preview_string = maybe;
	}

	struct decor_bounds bounds;
	decor_get_bounds(NULL, &bounds);

	decor_left_width = bounds.left_width;
	decor_top_height = bounds.top_height;
	decor_right_width = bounds.right_width;
	decor_bottom_height = bounds.bottom_height;
	decor_width = bounds.width;
	decor_height = bounds.height;

	window = yutani_window_create(yctx, width + decor_width, height + decor_height);
	yutani_window_move(yctx, window, 100, 100);

	tt_font_name = tt_get_name_string(tt_font, 4);

	if (tt_font_name) {
		sprintf(window_title, "%s - Font Preview", tt_font_name);
	}

	yutani_window_advertise_icon(yctx, window, window_title, "font");

	ctx = init_graphics_yutani_double_buffer(window);

	redraw();
	yutani_flip(yctx, window);

	int playing = 1;
	while (playing) {
		yutani_msg_t * m = yutani_poll(yctx);
		while (m) {
			if (menu_process_event(yctx, m)) {
				redraw();
				yutani_flip(yctx, window);
			}
			switch (m->type) {
				case YUTANI_MSG_WINDOW_FOCUS_CHANGE:
					{
						struct yutani_msg_window_focus_change * wf = (void*)m->data;
						yutani_window_t * win = hashmap_get(yctx->windows, (void*)(uintptr_t)wf->wid);
						if (win && win == window) {
							win->focused = wf->focused;
							redraw();
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
						struct yutani_msg_window_mouse_event * me = (void*)m->data;
						int result = decor_handle_event(yctx, m);
						switch (result) {
							case DECOR_CLOSE:
								playing = 0;
								break;
							case DECOR_RIGHT:
								/* right click in decoration, show appropriate menu */
								decor_show_default_menu(window, window->x + me->new_x, window->y + me->new_y);
								break;
							default:
								/* Other actions */
								break;
						}
					}
					break;
				case YUTANI_MSG_WINDOW_CLOSE:
				case YUTANI_MSG_SESSION_END:
					playing = 0;
					break;
				default:
					break;
			}
			free(m);
			m = yutani_poll_async(yctx);
		}
	}

	yutani_close(yctx, window);

	return 0;
}

