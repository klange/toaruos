#include <stdlib.h>
#include <assert.h>
#include <math.h>

#include "lib/window.h"
#include "lib/graphics.h"

sprite_t * sprites[128];
sprite_t alpha_tmp;

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
	if (x >= tex->width)  return rgb(0,0,0);
	if (y >= tex->height) return rgb(0,0,0);
	double u_ratio = u - x;
	double v_ratio = v - y;
	double u_o = 1 - u_ratio;
	double v_o = 1 - v_ratio;
	if (x == tex->width - 1 || y == tex->height - 1) return SPRITE(tex,x,y);
	double r_RED = (_RED(SPRITE(tex,x,y)) * u_o + _RED(SPRITE(tex,x+1,y)) * u_ratio) * v_o + (_RED(SPRITE(tex,x,y+1)) * u_o  + _RED(SPRITE(tex,x+1,y+1)) * u_ratio) * v_ratio;
	double r_BLU = (_BLU(SPRITE(tex,x,y)) * u_o + _BLU(SPRITE(tex,x+1,y)) * u_ratio) * v_o + (_BLU(SPRITE(tex,x,y+1)) * u_o  + _BLU(SPRITE(tex,x+1,y+1)) * u_ratio) * v_ratio;
	double r_GRE = (_GRE(SPRITE(tex,x,y)) * u_o + _GRE(SPRITE(tex,x+1,y)) * u_ratio) * v_o + (_GRE(SPRITE(tex,x,y+1)) * u_o  + _GRE(SPRITE(tex,x+1,y+1)) * u_ratio) * v_ratio;

	return rgb(r_RED,r_GRE,r_BLU);
}

void window_draw_sprite_scaled(window_t * window, sprite_t * sprite, uint16_t x, uint16_t y, uint16_t width, uint16_t height) {
	int x_hi = min(width, (window->width - x));
	int y_hi = min(height, (window->height - y));

	for (uint16_t _y = 0; _y < y_hi; ++_y) {
		for (uint16_t _x = 0; _x < x_hi; ++_x) {
			window_set_point(window, x + _x, y + _y, getBilinearFilteredPixelColor(sprite, (double)_x / (double)width, (double)_y/(double)height));
		}
	}
}

int main (int argc, char ** argv) {
	if (argc < 3) {
		printf("usage: %s width height\n", argv[0]);
		return -1;
	}

	int width = atoi(argv[1]);
	int height = atoi(argv[2]);

	setup_windowing();

	/* Do something with a window */
	window_t * wina = window_create(0,0, width, height);
	assert(wina);
	window_fill(wina, rgb(100,100,100));
#if 0
	window_redraw_full(wina);
#endif

	printf("Loading background...\n");
	init_sprite(0, "/usr/share/login-background.bmp", NULL);
	printf("Background loaded.\n");

	window_draw_sprite_scaled(wina, sprites[0], 0, 0, width, height);

	while (1) {
		w_keyboard_t * kbd = poll_keyboard();
		if (kbd != NULL) {
			printf("[glogin] got key '%c'\n", (char)kbd->key);
			free(kbd);
		}

		//window_draw_line(wina, rand() % width, rand() % width, rand() % height, rand() % height, rgb(rand() % 255,rand() % 255,rand() % 255));
		window_draw_sprite_scaled(wina, sprites[0], 0, 0, width, height);
		window_redraw_full(wina);
	}

	//window_destroy(window); // (will close on exit)
	teardown_windowing();

	return 0;
}
