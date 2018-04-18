#include <stdint.h>

#include <toaru/yutani.h>
#include <toaru/graphics.h>
#include <toaru/decorations.h>
#include <toaru/sdf.h>

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

#define TEXT_OFFSET 10

static void init_sprite(int id, char * path) {
	sprites[id] = malloc(sizeof(sprite_t));
	load_sprite(sprites[id], path);
	sprites[id]->alpha = ALPHA_EMBEDDED;
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

	char * tmp_title = strdup(title);
	int t_l = strlen(tmp_title);

#define EXTRA_SPACE 40

	if (draw_sdf_string_width(tmp_title, 18, SDF_FONT_BOLD) + EXTRA_SPACE > width) {
		while (t_l >= 0 && (draw_sdf_string_width(tmp_title, 18, SDF_FONT_BOLD) + EXTRA_SPACE > width)) {
			tmp_title[t_l] = '\0';
			t_l--;
		}
	}

	if (strlen(tmp_title)) {
		int title_offset = (width / 2) - (draw_sdf_string_width(tmp_title, 18, SDF_FONT_BOLD) / 2);
		if (decors_active == 0) {
			draw_sdf_string(ctx, title_offset, TEXT_OFFSET, tmp_title, 18, rgb(226,226,226), SDF_FONT_BOLD);
		} else {
			draw_sdf_string(ctx, title_offset, TEXT_OFFSET, tmp_title, 18, rgb(147,147,147), SDF_FONT_BOLD);
		}
	}

	free(tmp_title);

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
	init_sprite(0, TTK_FANCY_PATH "active/ul.bmp");
	init_sprite(1, TTK_FANCY_PATH "active/um.bmp");
	init_sprite(2, TTK_FANCY_PATH "active/ur.bmp");
	init_sprite(3, TTK_FANCY_PATH "active/ml.bmp");
	init_sprite(4, TTK_FANCY_PATH "active/mr.bmp");
	init_sprite(5, TTK_FANCY_PATH "active/ll.bmp");
	init_sprite(6, TTK_FANCY_PATH "active/lm.bmp");
	init_sprite(7, TTK_FANCY_PATH "active/lr.bmp");
	init_sprite(8, TTK_FANCY_PATH "active/button-close.bmp");

	init_sprite(INACTIVE + 0, TTK_FANCY_PATH "inactive/ul.bmp");
	init_sprite(INACTIVE + 1, TTK_FANCY_PATH "inactive/um.bmp");
	init_sprite(INACTIVE + 2, TTK_FANCY_PATH "inactive/ur.bmp");
	init_sprite(INACTIVE + 3, TTK_FANCY_PATH "inactive/ml.bmp");
	init_sprite(INACTIVE + 4, TTK_FANCY_PATH "inactive/mr.bmp");
	init_sprite(INACTIVE + 5, TTK_FANCY_PATH "inactive/ll.bmp");
	init_sprite(INACTIVE + 6, TTK_FANCY_PATH "inactive/lm.bmp");
	init_sprite(INACTIVE + 7, TTK_FANCY_PATH "inactive/lr.bmp");
	init_sprite(INACTIVE + 8, TTK_FANCY_PATH "inactive/button-close.bmp");

	decor_top_height     = 33;
	decor_bottom_height  = 6;
	decor_left_width     = 6;
	decor_right_width    = 6;

	decor_render_decorations = render_decorations_fancy;
	decor_check_button_press = check_button_press_fancy;
}

