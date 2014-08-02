/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2014 Kevin Lange
 */
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

#include "lib/yutani.h"
#include "lib/graphics.h"
#include "lib/decorations.h"

static sprite_t * sprites[128];
static yutani_t * yctx;
static yutani_window_t * window;
static gfx_context_t * ctx;

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
	yutani_flip(yctx, window);
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

#if 0
void resize_callback(window_t * win) {
	int _width  = win->width  - decor_left_width - decor_right_width;
	int _height = win->height - decor_top_height - decor_bottom_height;

	raw_x_offset = _width / 2  - WINDOW_SIZE;
	raw_y_offset = _height / 2 - WINDOW_SIZE;

	reinit_graphics_window(ctx, window);

	draw_fill(ctx, rgb(0,0,0));
	display();
}
#endif

char handle_event(yutani_msg_t * m) {
	if (m) {
		switch (m->type) {
			case YUTANI_MSG_KEY_EVENT:
				{
					struct yutani_msg_key_event * ke = (void*)m->data;
					if (ke->event.action == KEY_ACTION_DOWN) {
						return ke->event.keycode;
					}
				}
				break;
			case YUTANI_MSG_WINDOW_FOCUS_CHANGE:
				{
					struct yutani_msg_window_focus_change * wf = (void*)m->data;
					yutani_window_t * win = hashmap_get(yctx->windows, (void*)wf->wid);
					if (win) {
						win->focused = wf->focused;
						display();
					}
				}
				break;
			case YUTANI_MSG_WINDOW_MOUSE_EVENT:
				if (decor_handle_event(yctx, m) == DECOR_CLOSE) {
					return 'q';
				}
				break;
			case YUTANI_MSG_SESSION_END:
				return 'q';
			default:
				break;
		}
		free(m);
	}
	return 0;
}

int main(int argc, char ** argv) {

	yctx = yutani_init();
	window = yutani_window_create(yctx, 2 * WINDOW_SIZE, 2 * WINDOW_SIZE);
	yutani_window_move(yctx, window, 10, 10);
	ctx = init_graphics_yutani_double_buffer(window);
	draw_fill(ctx,rgb(0,0,0));
	flip(ctx);
	yutani_flip(yctx, window);

	yutani_window_advertise_icon(yctx, window, "RPG Demo", "applications-simulation");

	init_decorations();

	map_x = WINDOW_SIZE - (64 * 9) / 2;
	map_y = WINDOW_SIZE - (64 * 9) / 2;

	printf("Loading sprites...\n");

#define GAME_PATH "/usr/share/game/"

	init_sprite(0, GAME_PATH "0.bmp", NULL);
	init_sprite(1, GAME_PATH "1.bmp", NULL);
	init_sprite(2, GAME_PATH "2.bmp", NULL);
	init_sprite(3, GAME_PATH "3.bmp", NULL);
	init_sprite(4, GAME_PATH "4.bmp", NULL);
	init_sprite(5, GAME_PATH "5.bmp", NULL);
	init_sprite(6, GAME_PATH "6.bmp", NULL);
	init_sprite(7, GAME_PATH "7.bmp", NULL);
	init_sprite(124, GAME_PATH "remilia.bmp", NULL);
	init_sprite(125, GAME_PATH "remilia_r.bmp", NULL);
	init_sprite(126, GAME_PATH "remilia_l.bmp", NULL);
	init_sprite(127, GAME_PATH "remilia_f.bmp", NULL);
	load_map(GAME_PATH "map");
	printf("%d x %d\n", map.width, map.height);

	display();

	int playing = 1;
	while (playing) {

		char ch = '\0';

		yutani_msg_t * m = NULL;
		do {
			m = yutani_poll_async(yctx);
			handle_event(m);
		} while (m);

		m = yutani_poll(yctx);
		ch = handle_event(m);

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

	yutani_close(yctx, window);

	return 0;
}
