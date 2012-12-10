/*
 * Based on the Wayland/Weston drag and drop demo:
 * Copyright © 2010 Kristian Høgsberg
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */


#include <math.h>
#include <stdio.h>
#include <cairo.h>

#include "lib/window.h"
#include "lib/graphics.h"
#include "lib/list.h"

window_t * window;
gfx_context_t * ctx;
list_t * snowflakes;

int width, height;

struct snowflake {
	int x;
	int y;
	cairo_surface_t * surface;
};

#define random rand
#define item_width 64
#define item_height 64

int windspeed = 2;
int gravity   = 5;

struct snowflake * create_snowflake() {

	struct snowflake * item = malloc(sizeof(struct snowflake));

	item->x = random() % width;
	item->y = random() % height;

	const int petal_count = 3 + random() % 5;
	const double r1 = 20 + random() % 10;
	const double r2 = 5 + random() % 12;
	const double u = (10 + random() % 90) / 100.0;
	const double v = (random() % 90) / 100.0;

	cairo_t *cr;
	int i;
	double t, dt = 2 * M_PI / (petal_count * 2);
	double x1, y1, x2, y2, x3, y3;

	item->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, item_width, item_height);

	cr = cairo_create(item->surface);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_rgba(cr, 0, 0, 0, 0);
	cairo_paint(cr);

	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
	cairo_translate(cr, item_width / 2, item_height / 2);
	t = random();
	cairo_move_to(cr, cos(t) * r1, sin(t) * r1);
	for (i = 0; i < petal_count; i++, t += dt * 2) {
		x1 = cos(t) * r1;
		y1 = sin(t) * r1;
		x2 = cos(t + dt) * r2;
		y2 = sin(t + dt) * r2;
		x3 = cos(t + 2 * dt) * r1;
		y3 = sin(t + 2 * dt) * r1;

		cairo_curve_to(cr,
			       x1 - y1 * u, y1 + x1 * u,
			       x2 + y2 * v, y2 - x2 * v,
			       x2, y2);

		cairo_curve_to(cr,
			       x2 - y2 * v, y2 + x2 * v,
			       x3 + y3 * u, y3 - x3 * u,
			       x3, y3);
	}

	cairo_close_path(cr);

	cairo_set_source_rgba(cr,
			      0.5 + (random() % 50) / 49.0,
			      0.5 + (random() % 50) / 49.0,
			      0.5 + (random() % 50) / 49.0,
			      0.5 + (random() % 100) / 99.0);

	cairo_fill_preserve(cr);

	cairo_set_line_width(cr, 1);
	cairo_set_source_rgba(cr,
			      0.5 + (random() % 50) / 49.0,
			      0.5 + (random() % 50) / 49.0,
			      0.5 + (random() % 50) / 49.0,
			      0.5 + (random() % 100) / 99.0);
	cairo_stroke(cr);

	cairo_destroy(cr);;

	return item;
}

void render() {
	/* Clear window */
	draw_fill(ctx, rgba(0,0,0,0));

	/* Set up cairo context */
	int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, window->width);
	cairo_surface_t * surface = cairo_image_surface_create_for_data(ctx->backbuffer, CAIRO_FORMAT_ARGB32, window->width, window->height, stride);
	cairo_t * cr = cairo_create(surface);

	/* Draw some snowflakes */
	foreach(node, snowflakes) {
		struct snowflake * item = node->value;
		cairo_save(cr);

		cairo_set_source_surface(cr, item->surface, item->x, item->y);
		cairo_paint(cr);

		item->x += windspeed;
		item->y += gravity;

		if (item->y > height + item_height) {
			item->y = -item_height;
		}
		if (item->x > width + item_width) {
			item->x = -item_width;
		}

		cairo_restore(cr);
	}

	cairo_surface_flush(surface);
	cairo_destroy(cr);
	cairo_surface_flush(surface);
	cairo_surface_destroy(surface);

	flip(ctx);
}

void resize_callback(window_t * win) {
	reinit_graphics_window(ctx, window);
	render();
}


int main(int argc, char * argv[]) {
	setup_windowing();

	resize_window_callback = resize_callback;

	width  = wins_globals->server_width;
	height = wins_globals->server_height;

	window = window_create(0,0,width,height);
	ctx = init_graphics_window_double_buffer(window);
	draw_fill(ctx, rgba(0,0,0,0));
	flip(ctx);
	window_enable_alpha(window);

	snowflakes = list_create();
	for (int i = 0; i < 100; ++i) {
		list_insert(snowflakes, create_snowflake());
	}

	render();

	while (1) {
		w_keyboard_t * kbd = poll_keyboard();
		if (kbd != NULL) {
			if (kbd->key == 'q') {
				break;
			}
			free(kbd);
		}
		render();
	}


	teardown_windowing();

	return 0;
}
