/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Client-side Window Decoration library
 */

#include <stdint.h>
#include "graphics.h"
#include "window.h"
#include "decorations.h"
#include "shmemfonts.h"

uint32_t decor_top_height     = 33;
uint32_t decor_bottom_height  = 6;
uint32_t decor_left_width     = 6;
uint32_t decor_right_width    = 6;

#define TEXT_OFFSET_X 10
#define TEXT_OFFSET_Y 16

#define BORDERCOLOR rgb(60,60,60)
#define BACKCOLOR rgb(20,20,20)
#define TEXTCOLOR rgb(255,255,255)

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

static sprite_t * sprites[8];

#define TEXT_OFFSET 24

static void init_sprite_png(int id, char * path) {
	sprites[id] = malloc(sizeof(sprite_t));
	load_sprite_png(sprites[id], path);
}

void init_decorations() {
	init_shmemfonts();

	init_sprite_png(0, "/usr/share/ttk/ul.png");
	init_sprite_png(1, "/usr/share/ttk/um.png");
	init_sprite_png(2, "/usr/share/ttk/ur.png");
	init_sprite_png(3, "/usr/share/ttk/ml.png");
	init_sprite_png(4, "/usr/share/ttk/mr.png");
	init_sprite_png(5, "/usr/share/ttk/ll.png");
	init_sprite_png(6, "/usr/share/ttk/lm.png");
	init_sprite_png(7, "/usr/share/ttk/lr.png");

}

void render_decorations(window_t * window, gfx_context_t * ctx, char * title) {
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

uint32_t decor_width() {
	return decor_left_width + decor_right_width;
}

uint32_t decor_height() {
	return decor_top_height + decor_bottom_height;
}


