/* vim: ts=4 sw=4 noexpandtab
 *
 * Signed Distance Field Text Library
 */

#include <stdio.h>
#include <stdlib.h>

#include <toaru/graphics.h>
#include <toaru/hashmap.h>
#include <toaru/sdf.h>
#include <toaru/spinlock.h>

#define BASE_WIDTH 50
#define BASE_HEIGHT 50
#define GAMMA 1.7

static sprite_t _font_data_thin;
static sprite_t _font_data_bold;

static hashmap_t * _font_cache;

static volatile int _sdf_lock = 0;

struct {
	char code;
	size_t width_bold;
} _char_data[] = {
	{'!',  20},
	{'"',  35},
	{'#',  40},
	{'$',  35},
	{'%',  35},
	{'&',  35},
	{'\'', 35},
	{'(',  22},
	{')',  22},
	{'*',  35},
	{'+',  35},
	{',',  35},
	{'-',  30},
	{'.',  18},
	{'/',  24},
	{'0',  32},
	{'1',  32},
	{'2',  32},
	{'3',  32},
	{'4',  32},
	{'5',  32},
	{'6',  32},
	{'7',  32},
	{'8',  32},
	{'9',  32},
	{':',  22},
	{';',  22},
	{'<',  35},
	{'=',  35},
	{'>',  35},
	{'?',  35},
	{'@',  50},
	{'A',  35},
	{'B',  35},
	{'C',  34},
	{'D',  36},
	{'E',  34},
	{'F',  34},
	{'G',  35},
	{'H',  35},
	{'I',  22},
	{'J',  24},
	{'K',  35},
	{'L',  32},
	{'M',  45},
	{'N',  36},
	{'O',  38},
	{'P',  35},
	{'Q',  38},
	{'R',  36},
	{'S',  35},
	{'T',  35},
	{'U',  35},
	{'V',  37},
	{'W',  50},
	{'X',  35},
	{'Y',  32},
	{'Z',  35},
	{'[',  35},
	{'\\', 35},
	{']',  35},
	{'^',  35},
	{'_',  35},
	{'`',  35},
	{'a',  32},
	{'b',  32},
	{'c',  29},
	{'d',  32},
	{'e',  32},
	{'f',  25},
	{'g',  32},
	{'h',  32},
	{'i',  16},
	{'j',  16},
	{'k',  30},
	{'l',  16},
	{'m',  47},
	{'n',  33},
	{'o',  32},
	{'p',  32},
	{'q',  32},
	{'r',  25},
	{'s',  31},
	{'t',  26},
	{'u',  32},
	{'v',  32},
	{'w',  42},
	{'x',  32},
	{'y',  32},
	{'z',  32},
	{'{',  32},
	{'|',  32},
	{'}',  32},
	{'~',  32},
	{' ',  20},
	{0,0},
};

static int loaded = 0;

__attribute__((constructor))
static void _init_sdf(void) {
	/* Load the font. */
	_font_cache = hashmap_create_int(10);
	load_sprite(&_font_data_thin, "/usr/share/sdf_thin.bmp");
	load_sprite(&_font_data_bold, "/usr/share/sdf_bold.bmp");
	loaded = 1;
}

static sprite_t * _select_font(int font) {
	switch (font) {
		case SDF_FONT_BOLD:
			return &_font_data_bold;
		case SDF_FONT_THIN:
		default:
			return &_font_data_thin;
	}
}

static int _select_width(char ch, int font) {
	switch (font) {
		case SDF_FONT_BOLD:
			return _char_data[(int)ch].width_bold;
		case SDF_FONT_THIN:
		default:
			return _char_data[(int)ch].width_bold * 0.8;
	}
}

static int draw_sdf_character(gfx_context_t * ctx, int32_t x, int32_t y, int ch, int size, uint32_t color, sprite_t * tmp, int font, sprite_t * _font_data) {
	if (ch != ' ' && (ch < '!' || ch > '~')) {
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
	int width = _select_width(ch, font) * scale;
	int fx = ((BASE_WIDTH * ch) % _font_data->width) * scale;
	int fy = (((BASE_WIDTH * ch) / _font_data->width) * BASE_HEIGHT) * scale;

	int height = BASE_HEIGHT * ((double)size / 50.0);


	/* ignore size */
	for (int j = 0; j < height; ++j) {
		for (int i = 0; i < size; ++i) {
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

int draw_sdf_string(gfx_context_t * ctx, int32_t x, int32_t y, const char * str, int size, uint32_t color, int font) {

	sprite_t * _font_data = _select_font(font);

	if (!loaded) return 0;

	double scale = (double)size / 50.0;
	int scale_height = scale * _font_data->height;

	sprite_t * tmp;
	spin_lock(&_sdf_lock);
	if (!hashmap_has(_font_cache, (void *)(scale_height | (font << 16)))) {
		tmp = create_sprite(scale * _font_data->width, scale * _font_data->height, ALPHA_OPAQUE);
		gfx_context_t * t = init_graphics_sprite(tmp);
		draw_sprite_scaled(t, _font_data, 0, 0, tmp->width, tmp->height);
		free(t);
		hashmap_set(_font_cache, (void *)(scale_height | (font << 16)), tmp);
	} else {
		tmp = hashmap_get(_font_cache, (void *)(scale_height | (font << 16)));
	}
	spin_unlock(&_sdf_lock);

	int32_t out_width = 0;
	while (*str) {
		int w = draw_sdf_character(ctx,x,y,*str,size,color,tmp,font,_font_data);
		out_width += w;
		x += w;
		str++;
	}

	return out_width;
}

static int char_width(char ch, int font) {
	if (ch != ' ' && (ch < '!' || ch > '~')) {
		/* TODO: Draw missing symbol? */
		return 0;
	}

	/* Calculate offset into table above */
	if (ch == ' ') {
		ch = '~' + 1 - '!';
	} else {
		ch -= '!';
	}

	return _select_width(ch, font);
}


int draw_sdf_string_width(const char * str, int size, int font) {
	double scale = (double)size / 50.0;

	int32_t out_width = 0;
	while (*str) {
		int w = char_width(*str,font) * scale;
		out_width += w;
		str++;
	}

	return out_width;
}
