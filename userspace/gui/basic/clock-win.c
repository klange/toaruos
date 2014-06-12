/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2014 Kevin Lange
 */
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
#include <cairo.h>
#include <unistd.h>
#include <sys/time.h>

#define PI 3.14159265

#include "lib/yutani.h"
#include "lib/graphics.h"

static yutani_t * yctx;
static yutani_window_t * window;
static gfx_context_t * w_ctx;
static int should_exit = 0;

static int32_t min(int32_t a, int32_t b) {
	return (a < b) ? a : b;
}

static int32_t max(int32_t a, int32_t b) {
	return (a > b) ? a : b;
}

static void draw(int secs) {
	struct tm * timeinfo = localtime((time_t *)&secs);
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
	yutani_flip(yctx, window);
}

int main (int argc, char ** argv) {
	int left   = 100;
	int top    = 100;
	int width  = 200;
	int height = 200;

	yctx = yutani_init();
	window = yutani_window_create(yctx, width, height);
	yutani_window_move(yctx, window, left, top);
	w_ctx = init_graphics_yutani_double_buffer(window);
	yutani_window_update_shape(yctx, window, YUTANI_SHAPE_THRESHOLD_CLEAR);

	while (!should_exit) {
		yutani_msg_t * m = yutani_poll_async(yctx);
		if (m) {
			switch (m->type) {
				case YUTANI_MSG_KEY_EVENT:
					{
						struct yutani_msg_key_event * ke = (void*)m->data;
						if (ke->event.action == KEY_ACTION_DOWN && ke->event.keycode == 'q') {
							should_exit = 1;
							goto done;
						}
					}
					break;
				case YUTANI_MSG_SESSION_END:
					should_exit = 1;
					goto done;
				default:
					break;
			}
			free(m);
		}
		struct timeval now;
		int last = 0;

		gettimeofday(&now, NULL); //time(NULL);
		if (now.tv_sec != last) {
			last = now.tv_sec;
			draw(last);
		}
		usleep(10000);
	}
done:

	yutani_close(yctx, window);

	return 0;
}

