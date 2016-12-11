#include <stdint.h>

#include "lib/yutani.h"
#include "lib/graphics.h"
#include "lib/shmemfonts.h"
#include "lib/decorations.h"
#include "lib/confreader.h"

static int u_height = 3;
static int ul_width = 3;
static int ur_width = 3;

static int m_height = 3;
static int ml_width = 3;
static int mr_width = 3;

static int l_height = 3;
static int ll_width = 3;
static int lr_width = 3;

static int close_top = 0;
static int close_right = 0;
static int close_left = -1;
static int close_bottom = -1;

static sprite_t * sprites[4];

static void init_sprite_png(int id, char * path) {
	sprites[id] = malloc(sizeof(sprite_t));
	load_sprite_png(sprites[id], path);
}

extern uint32_t getBilinearFilteredPixelColor(sprite_t * tex, double u, double v);

static void render_decorations_fancy(yutani_window_t * window, gfx_context_t * ctx, char * title, int decors_active) {
	int width = window->width;
	int height = window->height;

	sprite_t * texture = sprites[decors_active == DECOR_ACTIVE ? 0 : 1];
	sprite_t * close_button = sprites[decors_active == DECOR_ACTIVE ? 2 : 3];

	for (int j = 0; j < u_height; ++j) {
		for (int i = 0; i < ul_width; ++i) {
			GFX(ctx,i,j) = SPRITE(texture,i,j);
		}
	}

	for (int j = 0; j < u_height; ++j) {
		for (int i = 0; i < ur_width; ++i) {
			GFX(ctx,(width-ur_width+i),j) = SPRITE(texture,(texture->width - ur_width+i),j);
		}
	}

	for (int j = 0; j < l_height; ++j) {
		for (int i = 0; i < ll_width; ++i) {
			GFX(ctx,i,height-l_height+j) = SPRITE(texture,i,texture->width-l_height+j);
		}
	}

	for (int j = 0; j < u_height; ++j) {
		for (int i = 0; i < ur_width; ++i) {
			GFX(ctx,(width-lr_width+i),height-l_height+j) = SPRITE(texture,(texture->width - lr_width+i),texture->width-l_height+j);
		}
	}

	for (int j = u_height; j < height-l_height; ++j) {
		double v = (double)(j - u_height)/(double)(height-l_height-u_height);
		v = ((double)u_height + v * (texture->height - u_height - l_height))/(double)(texture->height);
		for (int i = 0; i < ml_width; ++i) {
			double u = (double)i/(double)texture->width;
			GFX(ctx,i,j)=getBilinearFilteredPixelColor(texture,u,v);
		}
	}

	for (int j = u_height; j < height-l_height; ++j) {
		double v = (double)(j - u_height)/(double)(height-l_height-u_height);
		v = ((double)u_height + v * (texture->height - u_height - l_height))/(double)(texture->height);
		for (int i = 0; i < mr_width; ++i) {
			double u = (double)(texture->width - mr_width + i)/(double)texture->width;
			GFX(ctx,width-mr_width+i,j)=getBilinearFilteredPixelColor(texture,u,v);
		}
	}

	for (int j = ul_width; j < width - ur_width; ++j) {
		double u = (double)(j - ul_width)/(double)(width-ur_width-ul_width);
		u = ((double)ul_width + u * (texture->width - ul_width - ur_width))/(double)(texture->width);
		for (int i = 0; i < u_height; ++i) {
			double v = (double)i/(double)texture->height;
			GFX(ctx,j,i)=getBilinearFilteredPixelColor(texture,u,v);
		}
	}

	for (int j = ll_width; j < width - lr_width; ++j) {
		double u = (double)(j - ll_width)/(double)(width-lr_width-ll_width);
		u = ((double)ll_width + u * (texture->width - ll_width - lr_width))/(double)(texture->width);
		for (int i = 0; i < u_height; ++i) {
			double v = (double)(texture->height - l_height + i)/(double)texture->height;
			GFX(ctx,j,height-l_height+i)=getBilinearFilteredPixelColor(texture,u,v);
		}
	}

	/* Draw the close button */
	int c_top = close_top == -1 ? height - close_bottom - sprites[2]->height : close_top;
	int c_left = close_left == -1 ? width - close_right - sprites[2]->width : close_left;

	draw_sprite(ctx, close_button, c_left, c_top);



}

static int check_button_press_fancy(yutani_window_t * window, int x, int y) {
	int c_top = close_top == -1 ? window->height - close_bottom - sprites[2]->height : close_top;
	int c_left = close_left == -1 ? window->width - close_right - sprites[2]->width : close_left;

	if (x >= c_left && x < c_left + sprites[2]->width && y >= c_top && y < c_top + sprites[2]->height) {
		return DECOR_CLOSE;
	}

	return 0;
}

void decor_init(char * theme_name) {
	char tmp[256];

	fprintf(stderr, "Theme name is %s\n", theme_name);

	sprintf(tmp, "/usr/share/decors/%s/%s", theme_name, "decor.conf");
	confreader_t * conf = confreader_load(tmp);

	u_height = confreader_intd(conf, "upper", "height", 1);
	ul_width = confreader_intd(conf, "upper", "left", 1);
	ur_width = confreader_intd(conf, "upper", "right", 1);

	m_height = confreader_intd(conf, "middle", "height", 1);
	ml_width = confreader_intd(conf, "middle", "left", 1);
	mr_width = confreader_intd(conf, "middle", "right", 1);

	l_height = confreader_intd(conf, "lower", "height", 1);
	ll_width = confreader_intd(conf, "lower", "left", 1);
	lr_width = confreader_intd(conf, "lower", "right", 1);

	close_left = confreader_intd(conf, "close", "left", -1);
	close_right = confreader_intd(conf, "close", "right", -1);
	close_top = confreader_intd(conf, "close", "top", -1);
	close_bottom = confreader_intd(conf, "close", "bottom", -1);

	confreader_free(conf);

	/* Load sprites */
	sprintf(tmp, "/usr/share/decors/%s/%s", theme_name, "active.png");
	init_sprite_png(0, tmp);
	sprintf(tmp, "/usr/share/decors/%s/%s", theme_name, "inactive.png");
	init_sprite_png(1, tmp);
	sprintf(tmp, "/usr/share/decors/%s/%s", theme_name, "close-active.png");
	init_sprite_png(2, tmp);
	sprintf(tmp, "/usr/share/decors/%s/%s", theme_name, "close-inactive.png");
	init_sprite_png(3, tmp);

	decor_top_height     = u_height;
	decor_bottom_height  = l_height;
	decor_left_width     = ml_width;
	decor_right_width    = mr_width;

	decor_render_decorations = render_decorations_fancy;
	decor_check_button_press = check_button_press_fancy;
}

