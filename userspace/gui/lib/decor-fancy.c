#include <stdint.h>

#include "lib/yutani.h"
#include "lib/graphics.h"
#include "lib/shmemfonts.h"
#include "lib/decorations.h"

#define INACTIVE 9

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

	if (decors_active == DECOR_INACTIVE) decors_active = INACTIVE;

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

void decor_init() {
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

	decor_render_decorations = render_decorations_fancy;
	decor_check_button_press = check_button_press_fancy;
}

