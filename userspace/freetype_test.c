#include <stdio.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include <stdint.h>
#include <syscall.h>
DEFN_SYSCALL0(getgraphicsaddress, 11);

DEFN_SYSCALL0(getgraphicswidth,  18);
DEFN_SYSCALL0(getgraphicsheight, 19);

#define FONT_SIZE 12

uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
	return (r * 0x10000) + (g * 0x100) + (b * 0x1);
}

uint16_t graphics_width  = 0;
uint16_t graphics_height = 0;

#define GFX_W  graphics_width
#define GFX_H  graphics_height
#define GFX_B  4
#define GFX(x,y) gfx_mem[GFX_W * (y) + (x)]

uint32_t * gfx_mem;

#define _RED(color) ((color & 0x00FF0000) / 0x10000)
#define _GRE(color) ((color & 0x0000FF00) / 0x100)
#define _BLU(color) ((color & 0x000000FF) / 0x1)

uint32_t alpha_blend(uint32_t bottom, uint32_t top, uint32_t mask) {
	float a = _RED(mask) / 256.0;
	uint8_t red = _RED(bottom) * (1.0 - a) + _RED(top) * a;
	uint8_t gre = _GRE(bottom) * (1.0 - a) + _GRE(top) * a;
	uint8_t blu = _BLU(bottom) * (1.0 - a) + _BLU(top) * a;
	return rgb(red,gre,blu);
}

FT_Library   library;
FT_Face      face;
FT_GlyphSlot slot;

void drawChar(FT_Bitmap * bitmap, int x, int y) {
	int i, j, p, q;
	int x_max = x + bitmap->width;
	int y_max = y + bitmap->rows;
	for (j = y, q = 0; j < y_max; j++, q++) {
		for ( i = x, p = 0; i < x_max; i++, p++) {
			GFX(i,j) = alpha_blend(GFX(i,j),rgb(0xff,0xff,0xff),rgb(bitmap->buffer[q * bitmap->width + p],0,0));
		}
	}
}

int main(int argc, char *argv[]) {
	graphics_width  = syscall_getgraphicswidth();
	graphics_height = syscall_getgraphicsheight();
	gfx_mem = (void *)syscall_getgraphicsaddress();
	printf("Display is %dx%d\n", graphics_width, graphics_height);
	unsigned long str[] = {
		'H',
		'e',
		'l',
		'l',
		'o',
		' ',
		'w',
		'o',
		'r',
		'l',
		'd',
		'!',
		' ',
		0x3053, // こ
		0x3093, // ん
		0x306B, // に
		0x3061, // ち
		0x306F, // は
		0x3001, // 、
		0x4E16, // 世
		0x754C, // 界
		' ',
		0x3068, // と
		0x3042, // あ
		0x308B, // る
		'O',
		'S',
		' ',
		'0',
		'.',
		'1',
		0
	};
	int pen_x = 400; //2;
	int pen_y = 400; //12;
	int error;
	FT_UInt glyph_index;
	error = FT_Init_FreeType(&library);
	if (error) return 1;
	error = FT_New_Face(library, "/font.ttf", 0, &face);
	if (error) return 2;
	error = FT_Set_Pixel_Sizes(face, 0, FONT_SIZE);
	if (error) return 3;
	for (int i = 0; str[i] != 0; ++i) {
		glyph_index = FT_Get_Char_Index(face, str[i]);
		error = FT_Load_Glyph(face, glyph_index, FT_LOAD_DEFAULT | FT_LOAD_FORCE_AUTOHINT );
		if (error) return 4;
		error = FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);
		if (error) return 5;
		slot = face->glyph;
		drawChar(&slot->bitmap, pen_x + slot->bitmap_left, pen_y - slot->bitmap_top);
		pen_x += slot->advance.x >> 6;
		pen_y += slot->advance.y >> 6;
	}
}
