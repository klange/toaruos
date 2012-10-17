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
#include "lib/decorations.h"

sprite_t * sprites[128];
sprite_t alpha_tmp;

uint16_t win_width;
uint16_t win_height;

window_t * window;
gfx_context_t * w_ctx;

int center_x(int x) {
	return (win_width - x) / 2;
}

int center_y(int y) {
	return (win_height - y) / 2;
}

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
	draw_fill(w_ctx, rgb(255,255,255));

	{
		int r1 = win_width * 3 / 7;
		int r2 = win_width / 2;
		for (int val = 0; val < 12; val += 1) {
			double _val = (float)val / 12.0;
			_val *= 2.0 * PI;
			draw_line(w_ctx,
					  decor_left_width + win_width / 2 + r1 * sin(_val), decor_left_width + win_width / 2 + r2 * sin(_val),
			          decor_top_height + win_width / 2 - r1 * cos(_val), decor_top_height + win_width / 2 - r2 * cos(_val), rgb(0,0,0));
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
		draw_line_thick(w_ctx, decor_left_width + win_width / 2, decor_left_width + left, decor_top_height + win_width / 2, decor_top_height + top, color, 2);
	}
	{ /* Minutes */
		double val = timeinfo->tm_min;
		val += (double)timeinfo->tm_sec / 60.0;
		val /= 60.0;
		val *= 2.0 * PI;
		int radius = win_width * 3 / 7;
		int left = win_width / 2 + radius * sin(val);
		int top  = win_width / 2 - radius * cos(val);
		uint32_t color = rgb(0,0,0);
		draw_line_thick(w_ctx, decor_left_width + win_width / 2, decor_left_width + left, decor_top_height + win_width / 2, decor_top_height + top, color, 1);
	}
	{ /* Seconds */
		double val = timeinfo->tm_sec;
		val /= 60.0;
		val *= 2.0 * PI;
		int radius = win_width * 3 / 7;
		int left = win_width / 2 + radius * sin(val);
		int top  = win_width / 2 - radius * cos(val);
		uint32_t color = rgb(255,0,0);
		draw_line(w_ctx, decor_left_width + win_width / 2, decor_left_width + left, decor_top_height + win_width / 2, decor_top_height + top, color);
	}

	render_decorations(window, w_ctx->backbuffer, "Clock");
	flip(w_ctx);
}

void resize_callback(window_t * win) {
	win_width = win->width;
	win_height = win->height;
	reinit_graphics_window(w_ctx, window);
}

int main (int argc, char ** argv) {
	setup_windowing();

	int left   = 100;
	int top    = 100;
	int width  = 200;
	int height = 200;

	win_width = width;
	win_height = height;

	resize_window_callback = resize_callback;

	/* Do something with a window */
	window = window_create(left, top, width + decor_width(), height + decor_height());
	w_ctx = init_graphics_window_double_buffer(window);
	init_decorations();

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
			kbd = poll_keyboard();
			if (kbd != NULL) {
				ch = kbd->key;
				free(kbd);
			}
		} while (kbd != NULL);
		if (ch == 'q') {
			goto done;
			break;
		}
		syscall_yield();
	}
done:

	teardown_windowing();

	return 0;
}
