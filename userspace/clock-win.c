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

/* Bilinear filtering from Wikipedia */
uint32_t getBilinearFilteredPixelColor(sprite_t * tex, double u, double v) {
	u *= tex->width;
	v *= tex->height;
	int x = floor(u);
	int y = floor(v);
	if (x >= tex->width)  return 0;
	if (y >= tex->height) return 0;
	double u_ratio = u - x;
	double v_ratio = v - y;
	double u_o = 1 - u_ratio;
	double v_o = 1 - v_ratio;
	double r_ALP = 256;
	if (tex->alpha) {
		if (x == tex->width - 1 || y == tex->height - 1) return (SPRITE(tex,x,y) | 0xFF000000) & (0xFFFFFF + _RED(SMASKS(tex,x,y)) * 0x1000000);
		r_ALP = (_RED(SMASKS(tex,x,y)) * u_o + _RED(SMASKS(tex,x+1,y)) * u_ratio) * v_o + (_RED(SMASKS(tex,x,y+1)) * u_o  + _RED(SMASKS(tex,x+1,y+1)) * u_ratio) * v_ratio;
	}
	if (x == tex->width - 1 || y == tex->height - 1) return SPRITE(tex,x,y);
	double r_RED = (_RED(SPRITE(tex,x,y)) * u_o + _RED(SPRITE(tex,x+1,y)) * u_ratio) * v_o + (_RED(SPRITE(tex,x,y+1)) * u_o  + _RED(SPRITE(tex,x+1,y+1)) * u_ratio) * v_ratio;
	double r_BLU = (_BLU(SPRITE(tex,x,y)) * u_o + _BLU(SPRITE(tex,x+1,y)) * u_ratio) * v_o + (_BLU(SPRITE(tex,x,y+1)) * u_o  + _BLU(SPRITE(tex,x+1,y+1)) * u_ratio) * v_ratio;
	double r_GRE = (_GRE(SPRITE(tex,x,y)) * u_o + _GRE(SPRITE(tex,x+1,y)) * u_ratio) * v_o + (_GRE(SPRITE(tex,x,y+1)) * u_o  + _GRE(SPRITE(tex,x+1,y+1)) * u_ratio) * v_ratio;

	return rgb(r_RED,r_GRE,r_BLU) & (0xFFFFFF + (int)r_ALP * 0x1000000);
}

void draw_sprite_scaled(sprite_t * sprite, uint16_t x, uint16_t y, uint16_t width, uint16_t height) {
	int32_t _left   = max(x, 0);
	int32_t _top    = max(y, 0);
	int32_t _right  = min(x + width,  graphics_width - 1);
	int32_t _bottom = min(y + height, graphics_height - 1);
	for (uint16_t _y = 0; _y < height; ++_y) {
		for (uint16_t _x = 0; _x < width; ++_x) {
			if (x + _x < _left || x + _x > _right || y + _y < _top || y + _y > _bottom)
				continue;
			if (sprite->alpha) {
				uint32_t n_color = getBilinearFilteredPixelColor(sprite, (double)_x / (double)width, (double)_y/(double)height);
				uint32_t f_color = rgb(_ALP(n_color), 0, 0);
				GFX(x + _x, y + _y) = alpha_blend(GFX(x + _x, y + _y), n_color, f_color);
			} else {
				GFX(x + _x, y + _y) = getBilinearFilteredPixelColor(sprite, (double)_x / (double)width, (double)_y/(double)height);
			}
		}
	}
}

void draw_line_thick(uint16_t x0, uint16_t x1, uint16_t y0, uint16_t y1, uint32_t color, char thickness) {
	int deltax = abs(x1 - x0);
	int deltay = abs(y1 - y0);
	int sx = (x0 < x1) ? 1 : -1;
	int sy = (y0 < y1) ? 1 : -1;
	int error = deltax - deltay;
	while (1) {
		for (char j = -thickness; j <= thickness; ++j) {
			for (char i = -thickness; i <= thickness; ++i) {
				GFX(x0 + i, y0 + j) = color;
			}
		}
		if (x0 == x1 && y0 == y1) break;
		int e2 = 2 * error;
		if (e2 > -deltay) {
			error -= deltay;
			x0 += sx;
		}
		if (e2 < deltax) {
			error += deltax;
			y0 += sy;
		}
	}
}


void draw(int secs) {
	struct tm * timeinfo = localtime((time_t *)&secs);
#if 0
	printf("Hour: %d\n", timeinfo->tm_hour);
	printf("Min:  %d\n", timeinfo->tm_min);
	printf("Sec:  %d\n", timeinfo->tm_sec);
#endif
	draw_fill(rgb(255,255,255));

	{ /* Hours */
		double val = timeinfo->tm_hour;
		if (val > 12.0)
			val -= 12.0;
		val /= 12.0;
		val *= 2.0 * PI;
		int radius = win_width * 1 / 4;
		int left = win_width / 2 + radius * sin(val);
		int top  = win_width / 2 - radius * cos(val);
		uint32_t color = rgb(0,0,0);
		draw_line_thick(decor_left_width + win_width / 2, decor_left_width + left, decor_top_height + win_width / 2, decor_top_height + top, color, 2);
	}
	{ /* Minutes */
		double val = timeinfo->tm_min;
		val /= 60.0;
		val *= 2.0 * PI;
		int radius = win_width * 3 / 7;
		int left = win_width / 2 + radius * sin(val);
		int top  = win_width / 2 - radius * cos(val);
		uint32_t color = rgb(0,0,0);
		draw_line_thick(decor_left_width + win_width / 2, decor_left_width + left, decor_top_height + win_width / 2, decor_top_height + top, color, 1);
	}
	{ /* Seconds */
		double val = timeinfo->tm_sec;
		val /= 60.0;
		val *= 2.0 * PI;
		int radius = win_width * 3 / 7;
		int left = win_width / 2 + radius * sin(val);
		int top  = win_width / 2 - radius * cos(val);
		uint32_t color = rgb(255,0,0);
		draw_line(decor_left_width + win_width / 2, decor_left_width + left, decor_top_height + win_width / 2, decor_top_height + top, color);
	}

	render_decorations(window, frame_mem, "Clock");
	flip();
}

int main (int argc, char ** argv) {
	setup_windowing();

	int left   = 100;
	int top    = 100;
	int width  = 200;
	int height = 200;

	win_width = width;
	win_height = height;

	/* Do something with a window */
	window = window_create(left, top, width + decor_width(), height + decor_height());
	init_graphics_window_double_buffer(window);
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
