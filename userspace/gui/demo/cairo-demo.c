/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2014 Kevin Lange
 */
#include <math.h>
#include <stdio.h>
#include <cairo.h>

#include "lib/yutani.h"
#include "lib/graphics.h"

static yutani_t * yctx;
static yutani_window_t * window;
static gfx_context_t * ctx;

void render() {
	draw_fill(ctx, rgba(0,0,0,127));

	int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, window->width);
	cairo_surface_t * surface = cairo_image_surface_create_for_data(ctx->buffer, CAIRO_FORMAT_ARGB32, window->width, window->height, stride);
	cairo_t * cr = cairo_create(surface);

	cairo_set_line_width (cr, 6);

	cairo_rectangle (cr, 12, 12, 232, 70);
	cairo_new_sub_path (cr); cairo_arc (cr, 64, 64, 40, 0, 2*M_PI);
	cairo_new_sub_path (cr); cairo_arc_negative (cr, 192, 64, 40, 0, -2*M_PI);

	cairo_set_fill_rule (cr, CAIRO_FILL_RULE_EVEN_ODD);
	cairo_set_source_rgb (cr, 0, 0.7, 0); cairo_fill_preserve (cr);
	cairo_set_source_rgb (cr, 0, 0, 0); cairo_stroke (cr);

	cairo_translate (cr, 0, 128);
	cairo_rectangle (cr, 12, 12, 232, 70);
	cairo_new_sub_path (cr); cairo_arc (cr, 64, 64, 40, 0, 2*M_PI);
	cairo_new_sub_path (cr); cairo_arc_negative (cr, 192, 64, 40, 0, -2*M_PI);

	cairo_set_fill_rule (cr, CAIRO_FILL_RULE_WINDING);
	cairo_set_source_rgb (cr, 0, 0, 0.9); cairo_fill_preserve (cr);
	cairo_set_source_rgb (cr, 0, 0, 0); cairo_stroke (cr);

	cairo_surface_flush(surface);
	cairo_destroy(cr);
	cairo_surface_flush(surface);
	cairo_surface_destroy(surface);

	yutani_flip(yctx, window);
}

int main(int argc, char * argv[]) {

	int width  = 500;
	int height = 500;

	yctx = yutani_init();
	window = yutani_window_create(yctx,500,500);
	yutani_window_move(yctx, window, 100, 100);
	ctx = init_graphics_yutani(window);
	draw_fill(ctx, rgba(0,0,0,127));

	render();

	while (1) {
		yutani_msg_t * m = yutani_poll(yctx);
		if (m) {
			switch (m->type) {
				case YUTANI_MSG_KEY_EVENT:
					{
						struct yutani_msg_key_event * ke = (void*)m->data;
						if (ke->event.action == KEY_ACTION_DOWN && ke->event.keycode == 'q') {
							free(m);
							goto done;
						}
					}
					break;
				case YUTANI_MSG_SESSION_END:
					goto done;
				default:
					break;
			}
		}
		free(m);
	}

done:
	yutani_close(yctx, window);

	return 0;
}
