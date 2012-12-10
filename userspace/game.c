/*
 * The ToAru Sample Game
 *
 * This is the updated, windowed version of the sample RPG
 */

#include <stdio.h>
#include <stdint.h>
#include <syscall.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "lib/window.h"
#include "lib/graphics.h"
#include "lib/decorations.h"

sprite_t * sprites[128];
window_t * window;

gfx_context_t * ctx;

#define WINDOW_SIZE 224
int out_of_bounds(int x, int y) {
	if (x < ctx->width / 2 - WINDOW_SIZE)
		return 1;
	if (x >= ctx->width / 2 + WINDOW_SIZE)
		return 1;
	if (y < ctx->height / 2 - WINDOW_SIZE)
		return 1;
	if (y >= ctx->height / 2 + WINDOW_SIZE)
		return 1;
	return 0;
}

/* RPG Mapping Bits */
struct {
	int width;
	int height;
	char * buffer;
	int size;
} map;

void load_map(char * filename) {
	FILE * f = fopen(filename, "r");
	char tmp[256];
	fgets(tmp, 255, f);
	map.width = atoi(tmp);
	fgets(tmp, 256, f);
	map.height = atoi(tmp);
	map.size   = map.height * map.width;
	map.buffer = malloc(map.size);
	fread(map.buffer, map.size, 1, f);
	fclose(f);
}

char cell(int x, int y) {
	if (x < 0 || y < 0 || x >= map.width || y >= map.height) {
		return 'A'; /* The abyss is trees! */
	}
	return (map.buffer[y * map.width + x]);
}

#define VIEW_SIZE 4
#define CELL_SIZE 64

int my_x = 2;
int my_y = 2;
int direction = 0;
int offset_x = 0;
int offset_y = 0;
int offset_iter = 0;
int map_x;
int map_y;

int raw_x_offset = 0;
int raw_y_offset = 0;

void render_map(int x, int y) {
	int i = 0;
	for (int _y = y - VIEW_SIZE; _y <= y + VIEW_SIZE; ++_y) {
		int j = 0;
		for (int _x = x - VIEW_SIZE; _x <= x + VIEW_SIZE; ++_x) {
			char c = cell(_x,_y);
			int sprite;
			switch (c) {
				case '\n':
				case 'A':
					sprite = 1;
					break;
				case '.':
					sprite = 2;
					break;
				case 'W':
					sprite = 3;
					break;
				default:
					sprite = 0;
					break;
			}
			draw_sprite(ctx, sprites[sprite],
					decor_left_width + raw_x_offset + map_x + offset_x * offset_iter + j * CELL_SIZE,
					decor_top_height + raw_y_offset + map_y + offset_y * offset_iter + i * CELL_SIZE);
			++j;
		}
		++i;
	}
}


void display() {
	render_map(my_x,my_y);
	draw_sprite(ctx, sprites[124 + direction], decor_left_width + raw_x_offset + map_x + CELL_SIZE * 4, decor_top_height + raw_y_offset + map_y + CELL_SIZE * 4);
	render_decorations(window, ctx, "RPG Demo");
	flip(ctx);
}

void transition(int nx, int ny) {
	if (nx < my_x) {
		offset_x = 1;
		offset_y = 0;
	} else if (ny < my_y) {
		offset_x = 0;
		offset_y = 1;
	} else if (nx > my_x) {
		offset_x = -1;
		offset_y = 0;
	} else if (ny > my_y) {
		offset_x = 0;
		offset_y = -1;
	}
	for (int i = 0; i < 64; i += 2) {
		offset_iter = i;
		display();
	}
	offset_iter = 0;
	offset_x = 0;
	offset_y = 0;
	my_x = nx;
	my_y = ny;
	display();
}

void move(int cx, int cy) {
	int nx = my_x + cx;
	int ny = my_y + cy;

	if (cx == 1) {
		if (direction != 1) {
			direction = 1;
			display();
			return;
		}
	} else if (cx == -1) {
		if (direction != 2) {
			direction = 2;
			display();
			return;
		}
	} else if (cy == 1) {
		if (direction != 0) {
			direction = 0;
			display();
			return;
		}
	} else if (cy == -1) {
		if (direction != 3) {
			direction = 3;
			display();
			return;
		}
	}

	switch (cell(nx,ny)) {
		case '_':
		case '.':
			transition(nx,ny);
			break;
		default:
			break;
	}
	display();
}

/* woah */
char font_buffer[400000];
sprite_t alpha_tmp;

void init_sprite(int i, char * filename, char * alpha) {
	sprites[i] = malloc(sizeof(sprite_t));
	load_sprite(sprites[i], filename);
	if (alpha) {
		sprites[i]->alpha = ALPHA_MASK;
		load_sprite(&alpha_tmp, alpha);
		sprites[i]->masks = alpha_tmp.bitmap;
	} else {
		sprites[i]->alpha = ALPHA_INDEXED;
	}
	sprites[i]->blank = 0x0;
}

void resize_callback(window_t * win) {
	int _width  = win->width  - decor_left_width - decor_right_width;
	int _height = win->height - decor_top_height - decor_bottom_height;

	raw_x_offset = _width / 2  - WINDOW_SIZE;
	raw_y_offset = _height / 2 - WINDOW_SIZE;

	reinit_graphics_window(ctx, window);

	draw_fill(ctx, rgb(0,0,0));
	display();
}

void focus_callback() {
	display();
}

int main(int argc, char ** argv) {
	setup_windowing();

	resize_window_callback = resize_callback;
	window = window_create(10,10, 2 * WINDOW_SIZE, 2 * WINDOW_SIZE);
	ctx = init_graphics_window_double_buffer(window);
	draw_fill(ctx,rgb(0,0,0));
	flip(ctx);

	init_decorations();
	focus_changed_callback = focus_callback;

	map_x = WINDOW_SIZE - (64 * 9) / 2;
	map_y = WINDOW_SIZE - (64 * 9) / 2;

	printf("Loading sprites...\n");
	init_sprite(0, "/etc/game/0.bmp", NULL);
	init_sprite(1, "/etc/game/1.bmp", NULL);
	init_sprite(2, "/etc/game/2.bmp", NULL);
	init_sprite(3, "/etc/game/3.bmp", NULL);
	init_sprite(4, "/etc/game/4.bmp", NULL);
	init_sprite(5, "/etc/game/5.bmp", NULL);
	init_sprite(6, "/etc/game/6.bmp", NULL);
	init_sprite(7, "/etc/game/7.bmp", NULL);
	init_sprite(124, "/etc/game/remilia.bmp", NULL);
	init_sprite(125, "/etc/game/remilia_r.bmp", NULL);
	init_sprite(126, "/etc/game/remilia_l.bmp", NULL);
	init_sprite(127, "/etc/game/remilia_f.bmp", NULL);
	load_map("/etc/game/map");
	printf("%d x %d\n", map.width, map.height);

	display();


	int playing = 1;
	while (playing) {

		char ch = 0;
		w_keyboard_t * kbd = poll_keyboard();
		ch = kbd->key;
		free(kbd);

		switch (ch) {
			case 'q':
				playing = 0;
				break;
			case 'a':
				move(-1,0);
				/* left */
				break;
			case 'd':
				move(1,0);
				/* right */
				break;
			case 's':
				move(0,1);
				/* Down */
				break;
			case 'w':
				move(0,-1);
				/* Up */
				break;
			default:
				break;
		}
	}

	teardown_windowing();

	return 0;
}
