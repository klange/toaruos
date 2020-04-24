/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2016-2018 K. Lange
 *
 * The default "fancy" decorations theme.
 *
 * Based on an old gtk-window-decorator theme I used to use many,
 * many years ago.
 */
#include <stdint.h>
#include <dlfcn.h>

#include <toaru/yutani.h>
#include <toaru/graphics.h>
#include <toaru/decorations.h>
#include <toaru/sdf.h>

#define INACTIVE 10

#define TTK_FANCY_PATH "/usr/share/ttk/"

static int u_height = 33;
static int ul_width = 10;
static int ur_width = 10;
static int mr_width = 6;
static int l_height = 9;
static int ll_width = 9;
static int lr_width = 9;

static sprite_t * sprites[20];

#define TEXT_OFFSET ((window->decorator_flags & DECOR_FLAG_TILED) ? 5 : 10)
#define BUTTON_OFFSET ((window->decorator_flags & DECOR_FLAG_TILED) ? 5 : 0)

static int _have_freetype = 0;
static void (*freetype_set_font_face)(int face) = NULL;
static void (*freetype_set_font_size)(int size) = NULL;
static int (*freetype_draw_string)(gfx_context_t * ctx, int x, int y, uint32_t fg, const char * s) = NULL;
static int (*freetype_draw_string_width)(char * s) = NULL;

static void init_sprite(int id, char * path) {
	sprites[id] = malloc(sizeof(sprite_t));
	load_sprite(sprites[id], path);
}

static int get_bounds_fancy(yutani_window_t * window, struct decor_bounds * bounds) {
	if (window == NULL || !(window->decorator_flags & DECOR_FLAG_TILED)) {
		bounds->top_height = 33;
		bounds->bottom_height = 6;
		bounds->left_width = 6;
		bounds->right_width = 6;
	} else {
		/* Any "exposed" edge gets an extra pixel. */
		bounds->top_height = 27 + !(window->decorator_flags & DECOR_FLAG_TILE_UP);
		bounds->bottom_height   = !(window->decorator_flags & DECOR_FLAG_TILE_DOWN);
		bounds->left_width      = !(window->decorator_flags & DECOR_FLAG_TILE_LEFT);
		bounds->right_width     = !(window->decorator_flags & DECOR_FLAG_TILE_RIGHT);
	}

	bounds->width = bounds->left_width + bounds->right_width;
	bounds->height = bounds->top_height + bounds->bottom_height;
	return 0;
}

static void render_decorations_fancy(yutani_window_t * window, gfx_context_t * ctx, char * title, int decors_active) {
	int width = window->width;
	int height = window->height;

	struct decor_bounds bounds;
	get_bounds_fancy(window, &bounds);

	for (int j = 0; j < (int)bounds.top_height; ++j) {
		for (int i = 0; i < width; ++i) {
			GFX(ctx,i,j) = 0;
		}
	}

	if (decors_active == DECOR_INACTIVE) decors_active = INACTIVE;

	if ((window->decorator_flags & DECOR_FLAG_TILED)) {
		for (int i = 0; i < width; ++i) {
			draw_sprite(ctx, sprites[decors_active + 1], i, -6 + !(window->decorator_flags & DECOR_FLAG_TILE_UP));
		}

		uint32_t clear_color = rgb(62,62,62);
		if (!(window->decorator_flags & DECOR_FLAG_TILE_DOWN)) {
			/* Draw bottom line */
			for (int i = 0; i < (int)window->width; ++i) {
				GFX(ctx,i,window->height-1) = clear_color;
			}
		}

		if (!(window->decorator_flags & DECOR_FLAG_TILE_LEFT)) {
			/* Draw left line */
			for (int i = 0; i < (int)window->height; ++i) {
				GFX(ctx,0,i) = clear_color;
			}
		}

		if (!(window->decorator_flags & DECOR_FLAG_TILE_RIGHT)) {
			/* Draw right line */
			for (int i = 0; i < (int)window->height; ++i) {
				GFX(ctx,window->width-1,i) = clear_color;
			}
		}

	} else {

		uint32_t clear_color = 0x000000;

		for (int j = (int)bounds.top_height; j < height - (int)bounds.bottom_height; ++j) {
			for (int i = 0; i < (int)bounds.left_width; ++i) {
				GFX(ctx,i,j) = clear_color;
			}
			for (int i = width - (int)bounds.right_width; i < width; ++i) {
				GFX(ctx,i,j) = clear_color;
			}
		}

		for (int j = height - (int)bounds.bottom_height; j < height; ++j) {
			for (int i = 0; i < width; ++i) {
				GFX(ctx,i,j) = clear_color;
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
	}

	char * tmp_title = strdup(title);
	int t_l = strlen(tmp_title);

#define EXTRA_SPACE 120

	uint32_t title_color = (decors_active == 0) ? rgb(226,226,226) : rgb(147,147,147);
	if (_have_freetype) {
		freetype_set_font_face(1); /* regular non-monospace */
		freetype_set_font_size(12);
		if (freetype_draw_string_width(tmp_title) + EXTRA_SPACE > width) {
			while (t_l >= 0 && (freetype_draw_string_width(tmp_title) + EXTRA_SPACE > width)) {
				tmp_title[t_l] = '\0';
				t_l--;
			}
		}
		if (*tmp_title) {
			int title_offset = (width / 2) - (freetype_draw_string_width(tmp_title) / 2);
			freetype_draw_string(ctx, title_offset, TEXT_OFFSET + 14, title_color, tmp_title);
		}
	} else {
#define TEXT_SIZE 15
		if (draw_sdf_string_width(tmp_title, TEXT_SIZE, SDF_FONT_BOLD) + EXTRA_SPACE > width) {
			while (t_l >= 0 && (draw_sdf_string_width(tmp_title, TEXT_SIZE, SDF_FONT_BOLD) + EXTRA_SPACE > width)) {
				tmp_title[t_l] = '\0';
				t_l--;
			}
		}

		if (*tmp_title) {
			int title_offset = (width / 2) - (draw_sdf_string_width(tmp_title, TEXT_SIZE, SDF_FONT_BOLD) / 2);
			draw_sdf_string(ctx, title_offset, TEXT_OFFSET+2, tmp_title, TEXT_SIZE, title_color, SDF_FONT_BOLD);
		}
	}

	free(tmp_title);

	/* Buttons */
	draw_sprite(ctx, sprites[decors_active + 8], width - 28 + BUTTON_OFFSET, 16 - BUTTON_OFFSET);
	if (!(window->decorator_flags & DECOR_FLAG_NO_MAXIMIZE)) {
		draw_sprite(ctx, sprites[decors_active + 9], width - 50 + BUTTON_OFFSET, 16 - BUTTON_OFFSET);
	}
}

static int check_button_press_fancy(yutani_window_t * window, int x, int y) {
	if (x >= (int)window->width - 28 + BUTTON_OFFSET && x <= (int)window->width - 18 + BUTTON_OFFSET &&
		y >= 16 && y <= 26) {
		return DECOR_CLOSE;
	}

	if (!(window->decorator_flags & DECOR_FLAG_NO_MAXIMIZE)) {
		if (x >= (int)window->width - 50 + BUTTON_OFFSET && x <= (int)window->width - 40 + BUTTON_OFFSET &&
			y >= 16 && y <= 26) {
			return DECOR_MAXIMIZE;
		}
	}

	return 0;
}

void decor_init() {
	init_sprite(0, TTK_FANCY_PATH "active/ul.png");
	init_sprite(1, TTK_FANCY_PATH "active/um.png");
	init_sprite(2, TTK_FANCY_PATH "active/ur.png");
	init_sprite(3, TTK_FANCY_PATH "active/ml.png");
	init_sprite(4, TTK_FANCY_PATH "active/mr.png");
	init_sprite(5, TTK_FANCY_PATH "active/ll.png");
	init_sprite(6, TTK_FANCY_PATH "active/lm.png");
	init_sprite(7, TTK_FANCY_PATH "active/lr.png");
	init_sprite(8, TTK_FANCY_PATH "active/button-close.png");
	init_sprite(9, TTK_FANCY_PATH "active/button-maximize.png");

	init_sprite(INACTIVE + 0, TTK_FANCY_PATH "inactive/ul.png");
	init_sprite(INACTIVE + 1, TTK_FANCY_PATH "inactive/um.png");
	init_sprite(INACTIVE + 2, TTK_FANCY_PATH "inactive/ur.png");
	init_sprite(INACTIVE + 3, TTK_FANCY_PATH "inactive/ml.png");
	init_sprite(INACTIVE + 4, TTK_FANCY_PATH "inactive/mr.png");
	init_sprite(INACTIVE + 5, TTK_FANCY_PATH "inactive/ll.png");
	init_sprite(INACTIVE + 6, TTK_FANCY_PATH "inactive/lm.png");
	init_sprite(INACTIVE + 7, TTK_FANCY_PATH "inactive/lr.png");
	init_sprite(INACTIVE + 8, TTK_FANCY_PATH "inactive/button-close.png");
	init_sprite(INACTIVE + 9, TTK_FANCY_PATH "inactive/button-maximize.png");

	decor_render_decorations = render_decorations_fancy;
	decor_check_button_press = check_button_press_fancy;
	decor_get_bounds = get_bounds_fancy;

	void * freetype = dlopen("libtoaru_ext_freetype_fonts.so", 0);
	if (freetype) {
		_have_freetype = 1;
		freetype_set_font_face = dlsym(freetype, "freetype_set_font_face");
		freetype_set_font_size = dlsym(freetype, "freetype_set_font_size");
		freetype_draw_string   = dlsym(freetype, "freetype_draw_string");
		freetype_draw_string_width = dlsym(freetype, "freetype_draw_string_width");
	}
}

