/**
 * @file lib/decor-fancy.c
 * @brief "Fancy" decoration theme; the default.
 *
 * This is based on an old GTK theme I used to use back in ~2010.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2016-2021 K. Lange
 */
#include <stdint.h>
#include <dlfcn.h>

#include <toaru/yutani.h>
#include <toaru/graphics.h>
#include <toaru/decorations.h>
#include <toaru/text.h>

#define TTK_FANCY_PATH "/usr/share/ttk/fancy/"

#define TITLEBAR_HEIGHT 33
#define BASE_SIZE 10
#define TOTAL_SCALE 1
#define OUTER_SIZE 6

/* Color for the extra border lines drawn in tiled mode. */
#define BORDER_COLOR   rgb(62,62,62)

/* Button and title colors */
#define ACTIVE_COLOR   rgb(226,226,226)
#define INACTIVE_COLOR rgb(147,147,147)

static int u_height = TITLEBAR_HEIGHT * TOTAL_SCALE;
static int ul_width = BASE_SIZE * TOTAL_SCALE;
static int ur_width = BASE_SIZE * TOTAL_SCALE;
static int ml_width = BASE_SIZE * TOTAL_SCALE;
static int mr_width = BASE_SIZE * TOTAL_SCALE;
static int l_height = BASE_SIZE * TOTAL_SCALE;
static int ll_width = BASE_SIZE * TOTAL_SCALE;
static int lr_width = BASE_SIZE * TOTAL_SCALE;

static struct TT_Font * _tt_font = NULL;

#define BUTTON_CLOSE 0
#define BUTTON_MAXIMIZE 1
#define BUTTON_MINIMIZE 2
#define BUTTON_UNMAXIMIZE 3
#define ACTIVE   4
#define INACTIVE 13
static sprite_t * sprites[22];

#define TEXT_OFFSET ((window->decorator_flags & DECOR_FLAG_TILED) ? 5 : 10)
#define BUTTON_OFFSET ((window->decorator_flags & DECOR_FLAG_TILED) ? 5 : 0)

/**
 * Replaces an old graphics API function from the
 * very early days of ToaruOS...
 */
static void init_sprite(int id, char * path) {
	sprites[id] = malloc(sizeof(sprite_t));
	load_sprite(sprites[id], path);
}

/**
 * Make a new sprite by cropping down @p from.
 */
static sprite_t * sprite_crop(sprite_t * from, int x, int y, int w, int h) {
	sprite_t * dest = create_sprite(w,h,ALPHA_EMBEDDED);
	gfx_context_t * sctx = init_graphics_sprite(dest);
	draw_fill(sctx, rgba(0,0,0,0));
	draw_sprite(sctx, from, -x, -y);
	free(sctx);
	return dest;
}

/**
 * Chop up a spritesheet into edge/corner pieces.
 */
static void create_borders_from_spritesheet(int spriteIndex, const char * path) {
	sprite_t tmp;
	load_sprite(&tmp, path);

	int um_width = 1; /* These need to always be 1... tmp.width - ul_width - ur_width; */
	int m_height = 1; /* These need to always be 1... tmp.height - u_height - l_height; */
	int lm_width = 1; /* These need to always be 1... tmp.width - ll_width - lr_width; */

	int c = ul_width;
	int r = tmp.width - ur_width;
	int m = u_height;
	int l = tmp.height - l_height;

	sprites[spriteIndex + 0] = sprite_crop(&tmp, 0, 0, ul_width, u_height);
	sprites[spriteIndex + 1] = sprite_crop(&tmp, c, 0, um_width, u_height);
	sprites[spriteIndex + 2] = sprite_crop(&tmp, r, 0, ur_width, u_height);
	sprites[spriteIndex + 3] = sprite_crop(&tmp, 0, m, ml_width, m_height);
	sprites[spriteIndex + 4] = sprite_crop(&tmp, r, m, mr_width, m_height);
	sprites[spriteIndex + 5] = sprite_crop(&tmp, 0, l, ll_width, l_height);
	sprites[spriteIndex + 6] = sprite_crop(&tmp, c, l, lm_width, l_height);
	sprites[spriteIndex + 7] = sprite_crop(&tmp, r, l, lr_width, l_height);

	free(tmp.bitmap);
}

static int get_bounds_fancy(yutani_window_t * window, struct decor_bounds * bounds) {
	if (window == NULL || !(window->decorator_flags & DECOR_FLAG_TILED)) {
		bounds->top_height    = TITLEBAR_HEIGHT * TOTAL_SCALE;
		bounds->bottom_height = OUTER_SIZE * TOTAL_SCALE;
		bounds->left_width    = OUTER_SIZE * TOTAL_SCALE;
		bounds->right_width   = OUTER_SIZE * TOTAL_SCALE;
	} else {
		/* Any "exposed" edge gets an extra pixel. */
		bounds->top_height = 27 * TOTAL_SCALE + !(window->decorator_flags & DECOR_FLAG_TILE_UP);
		bounds->bottom_height   = !(window->decorator_flags & DECOR_FLAG_TILE_DOWN);
		bounds->left_width      = !(window->decorator_flags & DECOR_FLAG_TILE_LEFT);
		bounds->right_width     = !(window->decorator_flags & DECOR_FLAG_TILE_RIGHT);
	}

	bounds->width = bounds->left_width + bounds->right_width;
	bounds->height = bounds->top_height + bounds->bottom_height;
	return 0;
}

#define BUTTON_PAD 5

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

	decors_active = (decors_active == DECOR_INACTIVE) ? INACTIVE : ACTIVE;

	if ((window->decorator_flags & DECOR_FLAG_TILED)) {
		for (int i = 0; i < width; ++i) {
			draw_sprite(ctx, sprites[decors_active + 1], i, -6 * TOTAL_SCALE + !(window->decorator_flags & DECOR_FLAG_TILE_UP));
		}

		uint32_t clear_color = BORDER_COLOR;
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

#define EXTRA_SPACE 120

	uint32_t title_color = (decors_active == ACTIVE) ? ACTIVE_COLOR : INACTIVE_COLOR;

	int buttons_width = (!(window->decorator_flags & DECOR_FLAG_NO_MAXIMIZE)) ? 72 : 28;
	int usable_width = width - bounds.width - (2 * buttons_width + 10) * TOTAL_SCALE;

	tt_set_size(_tt_font, 12 * TOTAL_SCALE);
	int title_width = tt_string_width(_tt_font, title);
	if (title_width > usable_width) {
		usable_width += buttons_width * TOTAL_SCALE;
		if (usable_width > 0) {
			char * tmp_title = tt_ellipsify(title, 12 * TOTAL_SCALE, _tt_font, usable_width, &title_width);
			int title_offset = bounds.left_width + 10 * TOTAL_SCALE;
			tt_draw_string(ctx, _tt_font, title_offset, (TEXT_OFFSET + 14) * TOTAL_SCALE, tmp_title, title_color);
			free(tmp_title);
		}
	} else {
		int title_offset = buttons_width * TOTAL_SCALE + bounds.left_width + 10 * TOTAL_SCALE + (usable_width / 2) - (title_width / 2);
		tt_draw_string(ctx, _tt_font, title_offset, (TEXT_OFFSET + 14) * TOTAL_SCALE, title, title_color);
	}

	uint32_t h_color = rgb(100,100,100);
	uint32_t i_color = (decor_hover_window == window && decor_hover_button) ? ACTIVE_COLOR : title_color;

	if (width + (BUTTON_OFFSET - 28) * TOTAL_SCALE > bounds.left_width) {
		if (decor_hover_window == window && decor_hover_button == DECOR_CLOSE) {
			draw_rounded_rectangle(ctx,
				width + (BUTTON_OFFSET - 28 - BUTTON_PAD) * TOTAL_SCALE,
				(16 - BUTTON_OFFSET - BUTTON_PAD) * TOTAL_SCALE, 8 + BUTTON_PAD * 2, 8 + BUTTON_PAD * 2, 4, h_color);
		}
		draw_sprite_alpha_paint(ctx, sprites[BUTTON_CLOSE],
			width + (BUTTON_OFFSET - 28) * TOTAL_SCALE,
			(16 - BUTTON_OFFSET) * TOTAL_SCALE, 1.0, i_color);

		if (width + (BUTTON_OFFSET - 50) * TOTAL_SCALE > bounds.left_width) {
			if (!(window->decorator_flags & DECOR_FLAG_NO_MAXIMIZE)) {
				if (decor_hover_window == window && decor_hover_button == DECOR_MAXIMIZE) {
					draw_rounded_rectangle(ctx,
						width + (BUTTON_OFFSET - 50 - BUTTON_PAD) * TOTAL_SCALE,
						(16 - BUTTON_OFFSET - BUTTON_PAD) * TOTAL_SCALE, 8 + BUTTON_PAD * 2, 8 + BUTTON_PAD * 2, 4, h_color);
				}
				draw_sprite_alpha_paint(ctx, sprites[(window->decorator_flags & DECOR_FLAG_TILED) ? BUTTON_UNMAXIMIZE : BUTTON_MAXIMIZE],
					width + (BUTTON_OFFSET - 50) * TOTAL_SCALE,
					(16 - BUTTON_OFFSET) * TOTAL_SCALE, 1.0, i_color);

				if (width + (BUTTON_OFFSET - 72) * TOTAL_SCALE > bounds.left_width) {
					if (decor_hover_window == window && decor_hover_button == DECOR_MINIMIZE) {
						draw_rounded_rectangle(ctx,
							width + (BUTTON_OFFSET - 72 - BUTTON_PAD) * TOTAL_SCALE,
							(16 - BUTTON_OFFSET - BUTTON_PAD) * TOTAL_SCALE, 8 + BUTTON_PAD * 2, 8 + BUTTON_PAD * 2, 4, h_color);
					}
					draw_sprite_alpha_paint(ctx, sprites[BUTTON_MINIMIZE],
						width + (BUTTON_OFFSET - 72) * TOTAL_SCALE,
						(16 - BUTTON_OFFSET) * TOTAL_SCALE, 1.0, i_color);
				}
			}
		}
	}
}

static int check_button_press_fancy(yutani_window_t * window, int x, int y) {
	if (y >= (16 - BUTTON_OFFSET - BUTTON_PAD) * TOTAL_SCALE && y <= (16 - BUTTON_OFFSET + 8 + BUTTON_PAD) * TOTAL_SCALE ) {
		if (x >= (int)window->width + (BUTTON_OFFSET - 28 - BUTTON_PAD) * TOTAL_SCALE &&
			x <= (int)window->width + (BUTTON_OFFSET - 28 + 8 + BUTTON_PAD) * TOTAL_SCALE) {
			return DECOR_CLOSE;
		}

		if (!(window->decorator_flags & DECOR_FLAG_NO_MAXIMIZE)) {
			if (x >= (int)window->width + (BUTTON_OFFSET - 50 - BUTTON_PAD) * TOTAL_SCALE &&
				x <= (int)window->width + (BUTTON_OFFSET - 50 + 8 + BUTTON_PAD) * TOTAL_SCALE) {
				return DECOR_MAXIMIZE;
			}

			if (x >= (int)window->width + (BUTTON_OFFSET - 72 - BUTTON_PAD) * TOTAL_SCALE &&
				x <= (int)window->width + (BUTTON_OFFSET - 72 + 8 + BUTTON_PAD) * TOTAL_SCALE) {
				return DECOR_MINIMIZE;
			}
		}

		if (x >= (int)window->width + (BUTTON_OFFSET - 72 - BUTTON_PAD) * TOTAL_SCALE &&
			x <= (int)window->width + (BUTTON_OFFSET - 28 + 8 + BUTTON_PAD) * TOTAL_SCALE) {
			return DECOR_OTHER;
		}
	}

	return 0;
}

void decor_init() {
	init_sprite(BUTTON_CLOSE, TTK_FANCY_PATH "button-close.png");
	init_sprite(BUTTON_MAXIMIZE, TTK_FANCY_PATH "button-maximize.png");
	init_sprite(BUTTON_MINIMIZE, TTK_FANCY_PATH "button-minimize.png");
	init_sprite(BUTTON_UNMAXIMIZE, TTK_FANCY_PATH "button-unmaximize.png");

	create_borders_from_spritesheet(ACTIVE, TTK_FANCY_PATH "borders-active.png");
	create_borders_from_spritesheet(INACTIVE, TTK_FANCY_PATH "borders-inactive.png");

	decor_render_decorations = render_decorations_fancy;
	decor_check_button_press = check_button_press_fancy;
	decor_get_bounds = get_bounds_fancy;

	_tt_font = tt_font_from_shm("sans-serif.bold");
}

