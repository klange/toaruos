/*
 * test-gfx
 *
 * Windowed graphical test application.
 */
#include <stdlib.h>
#include <assert.h>
#include <math.h>

#include "lib/window.h"
#include "lib/graphics.h"
#include "lib/decorations.h"

#define dist(a,b,c,d) sqrt((double)(((a) - (c)) * ((a) - (c)) + ((b) - (d)) * ((b) - (d))))

window_t * wina;

uint16_t win_width;
uint16_t win_height;

uint16_t off_x;
uint16_t off_y;

gfx_context_t * ctx;

void redraw_borders() {
	render_decorations(wina, ctx, "Graphics Test");
}

void resize_callback(window_t * window) {
	win_width  = window->width  - decor_width();
	win_height = window->height - decor_height();
	reinit_graphics_window(ctx, wina);
	draw_fill(ctx, rgb(0,0,0));
	redraw_borders();
	flip(ctx);
}

uint32_t hsv_to_rgb(int h, float s, float v) {
	float c  = v * s;
	float hp = (float)h / 42.6666666f;
	float x = c * (1.0 - fabs(fmod(hp, 2) - 1.0));
	float m = v - c;
	float rp, gp, bp;
	if (hp < 1.0)      { rp = c; gp = x; bp = 0; }
	else if (hp < 2.0) { rp = x; gp = c; bp = 0; }
	else if (hp < 3.0) { rp = 0; gp = c; bp = x; }
	else if (hp < 4.0) { rp = 0; gp = x; bp = c; }
	else if (hp < 5.0) { rp = x; gp = 0; bp = c; }
	else if (hp < 6.0) { rp = c; gp = 0; bp = x; }
	else               { rp = 0; gp = 0; bp = 0; }
	return rgb((rp + m) * 255, (gp + m) * 255, (bp + m) * 255);
}

int main (int argc, char ** argv) {
	setup_windowing();
	resize_window_callback = resize_callback;

	win_width  = 500;
	win_height = 500;

	init_decorations();

	off_x = decor_left_width;
	off_y = decor_top_height;

	/* Do something with a window */
	wina = window_create(300, 300, win_width + decor_width(), win_height + decor_height());
	assert(wina);
	ctx = init_graphics_window_double_buffer(wina);

	draw_fill(ctx, rgb(0,0,0));
	redraw_borders();
	flip(ctx);

	double time;

	/* Generate a palette */
	uint32_t palette[256];
	for (int x = 0; x < 256; ++x) {
		palette[x] = hsv_to_rgb(x,1.0,1.0);
	}

	while (1) {
		w_keyboard_t * kbd = poll_keyboard_async();
		if (kbd) {
			char ch = kbd->key;
			free(kbd);
			if (ch == 'q') {
				goto done;
				break;
			}
		}

		time += 1.0;

		int w = win_width;
		int h = win_height;

		for (int x = 0; x < win_width; ++x) {
			for (int y = 0; y < win_height; ++y) {
				double value = sin(dist(x + time, y, 128.0, 128.0) / 8.0)
					+ sin(dist(x, y, 64.0, 64.0) / 8.0)
					+ sin(dist(x, y + time / 7, 192.0, 64) / 7.0)
					+ sin(dist(x, y, 192.0, 100.0) / 8.0);
				GFX(ctx, x + off_x, y + off_y) = palette[(int)((value + 4) * 32)];
			}
		}
		redraw_borders();
		flip(ctx);
	}
done:

	teardown_windowing();
	return 0;
}
