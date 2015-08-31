/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2012-2014 Kevin Lange
 */
/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Client-side Window Decoration library
 */

#include <stdint.h>
#include "graphics.h"
#include "yutani.h"
#include "decorations.h"
#include "shmemfonts.h"

uint32_t decor_top_height     = 33;
uint32_t decor_bottom_height  = 6;
uint32_t decor_left_width     = 6;
uint32_t decor_right_width    = 6;

#define TEXT_OFFSET_X 10
#define TEXT_OFFSET_Y 16

#define INACTIVE 9

#define BORDERCOLOR rgb(60,60,60)
#define BORDERCOLOR_INACTIVE rgb(30,30,30)
#define BACKCOLOR rgb(20,20,20)
#define TEXTCOLOR rgb(230,230,230)
#define TEXTCOLOR_INACTIVE rgb(140,140,140)

#define TTK_FANCY_PATH "/usr/share/ttk/"

static int u_height = 33;
static int ul_width = 10;
static int ur_width = 10;
static int ml_width = 6;
static int mr_width = 6;
static int l_height = 9;
static int ll_width = 9;
static int lr_width = 9;
static int llx_offset = 3;
static int lly_offset = 3;
static int lrx_offset = 3;
static int lry_offset = 3;

static sprite_t * sprites[20];

#define TEXT_OFFSET 24

static void init_sprite_png(int id, char * path) {
	sprites[id] = malloc(sizeof(sprite_t));
	load_sprite_png(sprites[id], path);
}

static void (*render_decorations_)(yutani_window_t *, gfx_context_t *, char *, int) = NULL;
static int  (*check_button_press)(yutani_window_t *, int x, int y) = NULL;

static void (*callback_close)(yutani_window_t *) = NULL;
static void (*callback_resize)(yutani_window_t *) = NULL;


static void render_decorations_simple(yutani_window_t * window, gfx_context_t * ctx, char * title, int decors_active) {

	uint32_t color = BORDERCOLOR;
	if (decors_active == INACTIVE) {
		color = BORDERCOLOR_INACTIVE;
	}

	for (uint32_t i = 0; i < window->height; ++i) {
		GFX(ctx, 0, i) = color;
		GFX(ctx, window->width - 1, i) = color;
	}

	for (uint32_t i = 1; i < decor_top_height; ++i) {
		for (uint32_t j = 1; j < window->width - 1; ++j) {
			GFX(ctx, j, i) = BACKCOLOR;
		}
	}

	if (decors_active == INACTIVE) {
		draw_string(ctx, TEXT_OFFSET_X, TEXT_OFFSET_Y, TEXTCOLOR_INACTIVE, title);
	} else {
		draw_string(ctx, TEXT_OFFSET_X, TEXT_OFFSET_Y, TEXTCOLOR, title);
	}

	for (uint32_t i = 0; i < window->width; ++i) {
		GFX(ctx, i, 0) = color;
		GFX(ctx, i, decor_top_height - 1) = color;
		GFX(ctx, i, window->height - 1) = color;
	}
}

static int check_button_press_simple(yutani_window_t * window, int x, int y) {
	return 0; /* no buttons in simple mode */
}

static void initialize_simple() {
	decor_top_height     = 24;
	decor_bottom_height  = 1;
	decor_left_width     = 1;
	decor_right_width    = 1;

	render_decorations_ = render_decorations_simple;
	check_button_press  = check_button_press_simple;
}

static void render_decorations_fancy(yutani_window_t * window, gfx_context_t * ctx, char * title, int decors_active) {
	int width = window->width;
	int height = window->height;

	for (int j = 0; j < decor_top_height; ++j) {
		for (int i = 0; i < width; ++i) {
			GFX(ctx,i,j) = 0;
		}
	}

	for (int j = decor_top_height; j < height - decor_bottom_height; ++j) {
		for (int i = 0; i < decor_left_width; ++i) {
			GFX(ctx,i,j) = 0;
		}
		for (int i = width - decor_right_width; i < width; ++i) {
			GFX(ctx,i,j) = 0;
		}
	}

	for (int j = height - decor_bottom_height; j < height; ++j) {
		for (int i = 0; i < width; ++i) {
			GFX(ctx,i,j) = 0;
		}
	}

	draw_sprite(ctx, sprites[decors_active + 0], 0, 0);
	for (int i = 0; i < width - (ul_width + ur_width); ++i) {
		draw_sprite(ctx, sprites[decors_active + 1], i + ul_width, 0);
	}
	draw_sprite(ctx, sprites[decors_active + 2], width - ur_width, 0);
	for (int i = 0; i < height - (u_height + l_height); ++i) {
		draw_sprite(ctx, sprites[decors_active + 3], 0, i + u_height);
		draw_sprite(ctx, sprites[decors_active + 4], width - mr_width, i + u_height);
	}
	draw_sprite(ctx, sprites[decors_active + 5], 0, height - l_height);
	for (int i = 0; i < width - (ll_width + lr_width); ++i) {
		draw_sprite(ctx, sprites[decors_active + 6], i + ll_width, height - l_height);
	}
	draw_sprite(ctx, sprites[decors_active + 7], width - lr_width, height - l_height);

	set_font_face(FONT_SANS_SERIF_BOLD);
	set_font_size(12);

	int title_offset = (width / 2) - (draw_string_width(title) / 2);
	if (decors_active == 0) {
		draw_string(ctx, title_offset, TEXT_OFFSET, rgb(226,226,226), title);
	} else {
		draw_string(ctx, title_offset, TEXT_OFFSET, rgb(147,147,147), title);
	}

	/* Buttons */
	draw_sprite(ctx, sprites[decors_active + 8], width - 28, 16);
}

static int check_button_press_fancy(yutani_window_t * window, int x, int y) {
	if (x >= window->width - 28 && x <= window->width - 18 &&
		y >= 16 && y <= 26) {
		return DECOR_CLOSE;
	}

	return 0;
}

static void initialize_fancy() {
	init_sprite_png(0, TTK_FANCY_PATH "active/ul.png");
	init_sprite_png(1, TTK_FANCY_PATH "active/um.png");
	init_sprite_png(2, TTK_FANCY_PATH "active/ur.png");
	init_sprite_png(3, TTK_FANCY_PATH "active/ml.png");
	init_sprite_png(4, TTK_FANCY_PATH "active/mr.png");
	init_sprite_png(5, TTK_FANCY_PATH "active/ll.png");
	init_sprite_png(6, TTK_FANCY_PATH "active/lm.png");
	init_sprite_png(7, TTK_FANCY_PATH "active/lr.png");
	init_sprite_png(8, TTK_FANCY_PATH "active/button-close.png");

	init_sprite_png(INACTIVE + 0, TTK_FANCY_PATH "inactive/ul.png");
	init_sprite_png(INACTIVE + 1, TTK_FANCY_PATH "inactive/um.png");
	init_sprite_png(INACTIVE + 2, TTK_FANCY_PATH "inactive/ur.png");
	init_sprite_png(INACTIVE + 3, TTK_FANCY_PATH "inactive/ml.png");
	init_sprite_png(INACTIVE + 4, TTK_FANCY_PATH "inactive/mr.png");
	init_sprite_png(INACTIVE + 5, TTK_FANCY_PATH "inactive/ll.png");
	init_sprite_png(INACTIVE + 6, TTK_FANCY_PATH "inactive/lm.png");
	init_sprite_png(INACTIVE + 7, TTK_FANCY_PATH "inactive/lr.png");
	init_sprite_png(INACTIVE + 8, TTK_FANCY_PATH "inactive/button-close.png");

	decor_top_height     = 33;
	decor_bottom_height  = 6;
	decor_left_width     = 6;
	decor_right_width    = 6;

	render_decorations_ = render_decorations_fancy;
	check_button_press  = check_button_press_fancy;
}

void render_decorations(yutani_window_t * window, gfx_context_t * ctx, char * title) {
	if (!window) return;
	if (!window->focused) {
		render_decorations_(window, ctx, title, INACTIVE);
	} else {
		render_decorations_(window, ctx, title, 0);
	}
}

void render_decorations_inactive(yutani_window_t * window, gfx_context_t * ctx, char * title) {
	if (!window) return;
	render_decorations_(window, ctx, title, INACTIVE);
}

void init_decorations() {
	init_shmemfonts();

	char * theme = getenv("WM_THEME");
	if (theme && !strcmp(theme, "simple")) {
		initialize_simple();
	} else {
		initialize_fancy();
	}
}

uint32_t decor_width() {
	return decor_left_width + decor_right_width;
}

uint32_t decor_height() {
	return decor_top_height + decor_bottom_height;
}

void decor_set_close_callback(void (*callback)(yutani_window_t *)) {
	callback_close = callback;
}

void decor_set_resize_callback(void (*callback)(yutani_window_t *)) {
	callback_resize = callback;
}

static int within_decors(yutani_window_t * window, int x, int y) {
	if ((x <= decor_left_width || x >= window->width - decor_right_width) && (x > 0 && x < window->width)) return 1;
	if ((y <= decor_top_height || y >= window->height - decor_bottom_height) && (y > 0 && y < window->width)) return 1;
	return 0;
}

#define LEFT_SIDE (me->new_x <= decor_left_width)
#define RIGHT_SIDE (me->new_x >= window->width - decor_right_width)
#define TOP_SIDE (me->new_y <= decor_top_height)
#define BOTTOM_SIDE (me->new_y >= window->height - decor_bottom_height)

static yutani_scale_direction_t check_resize_direction(struct yutani_msg_window_mouse_event * me, yutani_window_t * window) {
	yutani_scale_direction_t resize_direction = SCALE_NONE;
	if (LEFT_SIDE && !TOP_SIDE && !BOTTOM_SIDE) {
		resize_direction = SCALE_LEFT;
	} else if (RIGHT_SIDE && !TOP_SIDE && !BOTTOM_SIDE) {
		resize_direction = SCALE_RIGHT;
	} else if (BOTTOM_SIDE && !LEFT_SIDE && !RIGHT_SIDE) {
		resize_direction = SCALE_DOWN;
	} else if (BOTTOM_SIDE && LEFT_SIDE) {
		resize_direction = SCALE_DOWN_LEFT;
	} else if (BOTTOM_SIDE && RIGHT_SIDE) {
		resize_direction = SCALE_DOWN_RIGHT;
	} else if (TOP_SIDE && LEFT_SIDE) {
		resize_direction = SCALE_UP_LEFT;
	} else if (TOP_SIDE && RIGHT_SIDE) {
		resize_direction = SCALE_UP_RIGHT;
	} else if (TOP_SIDE && (me->new_y < 5)) {
		resize_direction = SCALE_UP;
	}
	return resize_direction;
}

static int old_resize_direction = SCALE_NONE;

int decor_handle_event(yutani_t * yctx, yutani_msg_t * m) {
	if (m) {
		switch (m->type) {
			case YUTANI_MSG_WINDOW_MOUSE_EVENT:
				{
					struct yutani_msg_window_mouse_event * me = (void*)m->data;
					yutani_window_t * window = hashmap_get(yctx->windows, (void*)me->wid);
					if (!window) return 0;
					if (within_decors(window, me->new_x, me->new_y)) {
						int button = check_button_press(window, me->new_x, me->new_y);
						if (me->command == YUTANI_MOUSE_EVENT_DOWN && me->buttons & YUTANI_MOUSE_BUTTON_LEFT) {
							if (!button) {
								/* Resize edges */
								yutani_scale_direction_t resize_direction = check_resize_direction(me, window);

								if (resize_direction != SCALE_NONE) {
									yutani_window_resize_start(yctx, window, resize_direction);
								}

								if (me->new_y < decor_top_height && resize_direction == SCALE_NONE) {
									yutani_window_drag_start(yctx, window);
								}
								return DECOR_OTHER;
							}
						}
						if (me->command == YUTANI_MOUSE_EVENT_MOVE) {
							if (!button) {
								/* Resize edges */
								yutani_scale_direction_t resize_direction = check_resize_direction(me, window);
								if (resize_direction != old_resize_direction) {
									if (resize_direction == SCALE_NONE) {
										yutani_window_show_mouse(yctx, window, YUTANI_CURSOR_TYPE_RESET);
									} else {
										switch (resize_direction) {
											case SCALE_UP:
											case SCALE_DOWN:
												yutani_window_show_mouse(yctx, window, YUTANI_CURSOR_TYPE_RESIZE_VERTICAL);
												break;
											case SCALE_LEFT:
											case SCALE_RIGHT:
												yutani_window_show_mouse(yctx, window, YUTANI_CURSOR_TYPE_RESIZE_HORIZONTAL);
												break;
											case SCALE_DOWN_RIGHT:
											case SCALE_UP_LEFT:
												yutani_window_show_mouse(yctx, window, YUTANI_CURSOR_TYPE_RESIZE_UP_DOWN);
												break;
											case SCALE_DOWN_LEFT:
											case SCALE_UP_RIGHT:
												yutani_window_show_mouse(yctx, window, YUTANI_CURSOR_TYPE_RESIZE_DOWN_UP);
												break;
										}
									}
									old_resize_direction = resize_direction;
								}
							}
						}
						if (me->command == YUTANI_MOUSE_EVENT_CLICK) {
							/* Determine if we clicked on a button */
							switch (button) {
								case DECOR_CLOSE:
									if (callback_close) callback_close(window);
									break;
								case DECOR_RESIZE:
									if (callback_resize) callback_resize(window);
									break;
								default:
									break;
							}
							return button;
						}
					} else {
						if (old_resize_direction != SCALE_NONE) {
							yutani_window_show_mouse(yctx, window, YUTANI_CURSOR_TYPE_RESET);
							old_resize_direction = 0;
						}
					}
				}
				break;
		}
	}
	return 0;
}
