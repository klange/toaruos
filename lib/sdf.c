/* vim: ts=4 sw=4 noexpandtab
 *
 * Signed Distance Field Text Library
 */

#include <stdio.h>
#include <stdlib.h>

#include <toaru/graphics.h>

#define BASE_WIDTH 50
#define BASE_HEIGHT 50
#define GAMMA 1.7

static sprite_t _font_data;

struct {
	char code;
	size_t width;
} _char_data[] = {
	{'!', 20},
	{'"', 35},
	{'#', 40},
	{'$', 35},
	{'%', 35},
	{'&', 35},
	{'\'', 35},
	{'(', 35},
	{')', 35},
	{'*', 35},
	{'+', 35},
	{',', 35},
	{'-', 35},
	{'.', 35},
	{'/', 35},
	{'0', 35},
	{'1', 35},
	{'2', 35},
	{'3', 35},
	{'4', 35},
	{'5', 35},
	{'6', 35},
	{'7', 35},
	{'8', 35},
	{'9', 35},
	{':', 35},
	{';', 35},
	{'<', 35},
	{'=', 35},
	{'>', 35},
	{'?', 35},
	{'@', 50},
	{'A', 35},
	{'B', 35},
	{'C', 35},
	{'D', 35},
	{'E', 35},
	{'F', 35},
	{'G', 35},
	{'H', 35},
	{'I', 35},
	{'J', 35},
	{'K', 35},
	{'L', 35},
	{'M', 40},
	{'N', 35},
	{'O', 35},
	{'P', 35},
	{'Q', 35},
	{'R', 35},
	{'S', 35},
	{'T', 35},
	{'U', 35},
	{'V', 35},
	{'W', 35},
	{'X', 35},
	{'Y', 35},
	{'Z', 35},
	{'[', 35},
	{'\\', 35},
	{']', 35},
	{'^', 35},
	{'_', 35},
	{'`', 35},
	{'a', 35},
	{'b', 35},
	{'c', 35},
	{'d', 32},
	{'e', 32},
	{'f', 35},
	{'g', 35},
	{'h', 35},
	{'i', 16},
	{'j', 16},
	{'k', 30},
	{'l', 16},
	{'m', 50},
	{'n', 50},
	{'o', 32},
	{'p', 35},
	{'q', 35},
	{'r', 25},
	{'s', 35},
	{'t', 35},
	{'u', 35},
	{'v', 35},
	{'w', 42},
	{'x', 35},
	{'y', 35},
	{'z', 35},
	{'{', 35},
	{'|', 35},
	{'}', 35},
	{'~', 35},
	{' ', 20},
	{0,0},
};

__attribute__((constructor))
static void _init_sdf(void) {
	/* Load the font. */
	load_sprite(&_font_data, "/usr/share/sdf.bmp");
}

static int draw_sdf_character(gfx_context_t * ctx, int32_t x, int32_t y, int ch, int size, uint32_t color, sprite_t * tmp) {
	if (ch != ' ' && ch < '!' || ch > '~') {
		/* TODO: Draw missing symbol? */
		return 0;
	}

	/* Calculate offset into table above */
	if (ch == ' ') {
		ch = '~' + 1 - '!';
	} else {
		ch -= '!';
	}

	double scale = (double)size / 50.0;
	size_t width = _char_data[ch].width * scale;
	int fx = ((BASE_WIDTH * ch) % _font_data.width) * scale;
	int fy = (((BASE_WIDTH * ch) / _font_data.width) * BASE_HEIGHT) * scale;

	int height = BASE_HEIGHT * ((double)size / 50.0);

	/* ignore size */
	for (int j = 0; j < height; ++j) {
		for (int i = 0; i < width; ++i) {
			/* TODO needs to do bilinear filter */
			if (fx+i > tmp->width) continue;
			if (fy+j > tmp->height) continue;
			uint32_t c = SPRITE((tmp), fx+i, fy+j);
			double dist = (double)_RED(c) / 255.0;
			double edge0 = 0.75 - GAMMA * 1.4142 / (double)size;
			double edge1 = 0.75 + GAMMA * 1.4142 / (double)size;
			double a = (dist - edge0) / (edge1 - edge0);
			if (a < 0.0) a = 0.0;
			if (a > 1.0) a = 1.0;
			a = a * a * (3 - 2 * a);
			GFX(ctx,x+i,y+j) = alpha_blend(GFX(ctx,x+i,y+j), color, rgb(255*a,0,0));
		}
	}

	return width;

}

int draw_sdf_string(gfx_context_t * ctx, int32_t x, int32_t y, char * str, int size, uint32_t color) {

	double scale = (double)size / 50.0;
	sprite_t * tmp = create_sprite(scale * _font_data.width, scale * _font_data.height, ALPHA_OPAQUE);
	gfx_context_t * t = init_graphics_sprite(tmp);
	draw_sprite_scaled(t, &_font_data, 0, 0, tmp->width, tmp->height);

	int32_t out_width = 0;
	while (*str) {
		int w = draw_sdf_character(ctx,x,y,*str,size,color,tmp);
		out_width += w;
		x += w;
		str++;
	}

	sprite_free(tmp);
	free(t);

	return out_width;
}


static int char_width(char ch) {
	if (ch != ' ' && ch < '!' || ch > '~') {
		/* TODO: Draw missing symbol? */
		return 0;
	}

	/* Calculate offset into table above */
	if (ch == ' ') {
		ch = '~' + 1 - '!';
	} else {
		ch -= '!';
	}

	return _char_data[ch].width;
}


int draw_sdf_string_width(char * str, int size) {
	double scale = (double)size / 50.0;

	int32_t out_width = 0;
	while (*str) {
		int w = char_width(*str) * scale;
		out_width += w;
		str++;
	}

	return out_width;
}
