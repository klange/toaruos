/*
 * view
 *
 * Displays bitmap images in windows
 */
#include <stdlib.h>

#include "lib/window.h"
#include "lib/graphics.h"
#include "lib/shmemfonts.h"

int u_height = 33;
int ul_width = 10;
int ur_width = 10;

int ml_width = 6;
int mr_width = 6;

int l_height = 9;
int ll_width = 9;
int lr_width = 9;

int llx_offset = 3;
int lly_offset = 3;
int lrx_offset = 3;
int lry_offset = 3;

int border_top     = 33;
int border_bottom  = 6;
int border_left    = 6;
int border_right   = 6;

#define TEXT_OFFSET 24

window_t * wina;
sprite_t * sprites[8];
gfx_context_t * ctx;

int width = 700;
int height = 500;

void init_sprite_png(int id, char * path) {
	sprites[id] = malloc(sizeof(sprite_t));
	load_sprite_png(sprites[id], path);
}

char * title = "テストアプリケーション Test Application ☃";

void redraw_borders() {
	draw_sprite(ctx, sprites[0], 0, 0);
	for (int i = 0; i < width - (ul_width + ur_width); ++i) {
		draw_sprite(ctx, sprites[1], i + ul_width, 0);
	}
	draw_sprite(ctx, sprites[2], width - ur_width, 0);
	for (int i = 0; i < height - (u_height + l_height); ++i) {
		draw_sprite(ctx, sprites[3], 0, i + u_height);
		draw_sprite(ctx, sprites[4], width - mr_width, i + u_height);
	}
	draw_sprite(ctx, sprites[5], 0, height - l_height);
	for (int i = 0; i < width - (ll_width + lr_width); ++i) {
		draw_sprite(ctx, sprites[6], i + ll_width, height - l_height);
	}
	draw_sprite(ctx, sprites[7], width - lr_width, height - l_height);

	set_font_face(FONT_SANS_SERIF_BOLD);
	set_font_size(12);

	int title_offset = (width / 2) - (draw_string_width(title) / 2);
	draw_string(ctx, title_offset, TEXT_OFFSET, rgb(226,226,226), title);

}

void redraw_interior() {
	for (int i = border_top; i < height - (border_bottom); ++i) {
		draw_line(ctx, border_left, width - border_right - 1, i, i, rgb(240,240,240));
	}
}

void resize_callback(window_t * window) {
	width  = window->width;
	height = window->height;
	reinit_graphics_window(ctx, wina);
	draw_fill(ctx, rgba(0,0,0,0));
	redraw_borders();
	redraw_interior();
}

int main (int argc, char ** argv) {

	int left = 30;
	int top  = 30;

	init_sprite_png(0, "/usr/share/ttk/ul.png");
	init_sprite_png(1, "/usr/share/ttk/um.png");
	init_sprite_png(2, "/usr/share/ttk/ur.png");
	init_sprite_png(3, "/usr/share/ttk/ml.png");
	init_sprite_png(4, "/usr/share/ttk/mr.png");
	init_sprite_png(5, "/usr/share/ttk/ll.png");
	init_sprite_png(6, "/usr/share/ttk/lm.png");
	init_sprite_png(7, "/usr/share/ttk/lr.png");

	setup_windowing();
	resize_window_callback = resize_callback;

	init_shmemfonts();

	/* Do something with a window */
	wina = window_create(left, top, width, height);
	ctx = init_graphics_window(wina);

	draw_fill(ctx, rgba(0,0,0,0));
	window_enable_alpha(wina);

	redraw_borders();
	redraw_interior();

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
