#include <math.h>
#include <stdio.h>
#include <cairo.h>

#include "lib/window.h"
#include "lib/graphics.h"

window_t * window;
gfx_context_t * ctx;

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
}

void resize_callback(window_t * win) {
	reinit_graphics_window(ctx, window);
	render();
}


int main(int argc, char * argv[]) {
	setup_windowing();

	int width  = 500;
	int height = 500;

	resize_window_callback = resize_callback;
	window = window_create(100,100,500,500);
	ctx = init_graphics_window(window);
	draw_fill(ctx, rgba(0,0,0,127));
	window_enable_alpha(window);

	render();

	while (1) {
		w_keyboard_t * kbd = poll_keyboard();
		if (kbd != NULL) {
			if (kbd->key == 'q') {
				break;
			}
			free(kbd);
		}
	}


	teardown_windowing();

	return 0;
}
