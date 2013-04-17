/*
 * clock-win
 *
 * Windowed Clock Application
 *
 * Creates a window that displays an analog clock.
 */
#include <stdlib.h>
#include <assert.h>
#include <math.h>
#include <time.h>
#include <syscall.h>
#include <cairo.h>

struct timeval {
	unsigned int tv_sec;
	unsigned int tv_usec;
};

#define PI 3.14159265

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_CACHE_H

#include "lib/window.h"
#include "lib/graphics.h"

sprite_t * sprites[128];
sprite_t alpha_tmp;

window_t * window;
gfx_context_t * w_ctx;

void init_sprite(int i, char * filename, char * alpha) {
	sprites[i] = malloc(sizeof(sprite_t));
	load_sprite(sprites[i], filename);
	if (alpha) {
		sprites[i]->alpha = 1;
		load_sprite(&alpha_tmp, alpha);
		sprites[i]->masks = alpha_tmp.bitmap;
	} else {
		sprites[i]->alpha = 0;
	}
	sprites[i]->blank = 0x0;
}

int32_t min(int32_t a, int32_t b) {
	return (a < b) ? a : b;
}

int32_t max(int32_t a, int32_t b) {
	return (a > b) ? a : b;
}
void draw(int secs) {
	struct tm * timeinfo = localtime((time_t *)&secs);
#if 0
	printf("Hour: %d\n", timeinfo->tm_hour);
	printf("Min:  %d\n", timeinfo->tm_min);
	printf("Sec:  %d\n", timeinfo->tm_sec);
#endif
	draw_fill(w_ctx, rgba(0,0,0,0));

	int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, w_ctx->width);
	cairo_surface_t * surface = cairo_image_surface_create_for_data(w_ctx->backbuffer, CAIRO_FORMAT_ARGB32, w_ctx->width, w_ctx->height, stride);
	cairo_t * cr = cairo_create(surface);

	cairo_set_line_width(cr, 9);
	cairo_set_source_rgb(cr, 0.0,0.0,0.0);
	cairo_translate(cr, w_ctx->width / 2, w_ctx->height / 2);
	cairo_arc(cr, 0, 0, w_ctx->width / 2 - 10, 0, 2 * M_PI);
	cairo_stroke_preserve(cr);
	cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
	cairo_fill(cr);

	uint16_t win_width = w_ctx->width;

	{
		int r1 = win_width * 3 / 7 - 9;
		int r2 = win_width / 2 - 9;
		for (int val = 0; val < 12; val += 1) {
			double _val = (float)val / 12.0;
			_val *= 2.0 * PI;
			draw_line(w_ctx,
					  win_width / 2 + r1 * sin(_val), win_width / 2 + r2 * sin(_val),
			          win_width / 2 - r1 * cos(_val), win_width / 2 - r2 * cos(_val), rgb(0,0,0));
		}
	}
	{ /* Hours */
		double val = timeinfo->tm_hour;
		val += (double)timeinfo->tm_min / 60.0;
		if (val > 12.0)
			val -= 12.0;
		val /= 12.0;
		val *= 2.0 * PI;
		int radius = win_width * 1 / 4;
		int left = win_width / 2 + radius * sin(val);
		int top  = win_width / 2 - radius * cos(val);
		uint32_t color = rgb(0,0,0);
		draw_line_thick(w_ctx, win_width / 2, left, win_width / 2, top, color, 2);
	}
	{ /* Minutes */
		double val = timeinfo->tm_min;
		val += (double)timeinfo->tm_sec / 60.0;
		val /= 60.0;
		val *= 2.0 * PI;
		int radius = win_width * 3 / 7 - 9;
		int left = win_width / 2 + radius * sin(val);
		int top  = win_width / 2 - radius * cos(val);
		uint32_t color = rgb(0,0,0);
		draw_line_thick(w_ctx, win_width / 2, left, win_width / 2, top, color, 1);
	}
	{ /* Seconds */
		double val = timeinfo->tm_sec;
		val /= 60.0;
		val *= 2.0 * PI;
		int radius = win_width * 3 / 7 - 9;
		int left = win_width / 2 + radius * sin(val);
		int top  = win_width / 2 - radius * cos(val);
		uint32_t color = rgb(255,0,0);
		draw_line(w_ctx, win_width / 2, left, win_width / 2, top, color);
	}

	cairo_surface_flush(surface);
	cairo_destroy(cr);
	cairo_surface_flush(surface);
	cairo_surface_destroy(surface);

	flip(w_ctx);
}

void resize_callback(window_t * win) {
	reinit_graphics_window(w_ctx, window);
}

int main (int argc, char ** argv) {
	setup_windowing();

	int left   = 100;
	int top    = 100;
	int width  = 200;
	int height = 200;

	resize_window_callback = resize_callback;

	/* Do something with a window */
	window = window_create(left, top, width, height);
	w_ctx = init_graphics_window_double_buffer(window);

	struct timeval now;
	int last = 0;

	while (1) {
		syscall_gettimeofday(&now, NULL); //time(NULL);
		if (now.tv_sec != last) {
			last = now.tv_sec;
			draw(last);
		}
		char ch = 0;
		w_keyboard_t * kbd;
		do {
			kbd = poll_keyboard_async();
			if (kbd != NULL) {
				ch = kbd->key;
				free(kbd);
			}
		} while (kbd != NULL);
		if (ch == 'q') {
			goto done;
			break;
		}
		syscall_nanosleep(0,50);
	}
done:

	teardown_windowing();

	return 0;
}
