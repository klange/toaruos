/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Client-side Window Decoration library
 */

#include <stdint.h>
#include "graphics.h"
#include "window.h"
#include "decorations.h"

uint32_t decor_top_height     = 24;
uint32_t decor_bottom_height  = 1;
uint32_t decor_left_width     = 1;
uint32_t decor_right_width    = 1;

#define TEXT_OFFSET_X 10
#define TEXT_OFFSET_Y 16

#define BORDERCOLOR rgb(60,60,60)
#define BACKCOLOR rgb(20,20,20)
#define TEXTCOLOR rgb(255,255,255)
#define SGFX(CTX,x,y,WIDTH) *((uint32_t *)&CTX[((WIDTH) * (y) + (x)) * 4])
#define FONT_SIZE 12

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_CACHE_H

static FT_Library   library;
static FT_Face      face;
static FT_GlyphSlot slot;
static FT_UInt      glyph_index;

static void _loadDejavu() {
	char * font;
	size_t s = 0;
	int error;
	font = (char *)syscall_shm_obtain(WINS_SERVER_IDENTIFIER ".fonts.sans-serif.bold", &s);
	error = FT_New_Memory_Face(library, font, s, 0, &face);
	error = FT_Set_Pixel_Sizes(face, FONT_SIZE, FONT_SIZE);
}

static void draw_char(FT_Bitmap * bitmap, int x, int y, uint32_t fg, window_t * window, char * ctx) {
	int i, j, p, q;
	int x_max = x + bitmap->width;
	int y_max = y + bitmap->rows;
	for (j = y, q = 0; j < y_max; j++, q++) {
		for ( i = x, p = 0; i < x_max; i++, p++) {
			SGFX(ctx,i,j,window->width) = alpha_blend(SGFX(ctx,i,j,window->width),fg,rgb(bitmap->buffer[q * bitmap->width + p],0,0));
		}
	}
}

static void draw_string(int x, int y, uint32_t fg, char * string, window_t * window, char * ctx) {
	slot = face->glyph;
	int pen_x = x, pen_y = y, i = 0;
	int len = strlen(string);
	int error;

	for (i = 0; i < len; ++i) {
		FT_UInt glyph_index;

		glyph_index = FT_Get_Char_Index( face, string[i]);
		error = FT_Load_Glyph(face, glyph_index, FT_LOAD_DEFAULT);
		if (error) {
			printf("Error loading glyph for '%c'\n", string[i]);
			continue;
		}
		slot = (face)->glyph;
		if (slot->format == FT_GLYPH_FORMAT_OUTLINE) {
			error = FT_Render_Glyph((face)->glyph, FT_RENDER_MODE_NORMAL);
			if (error) {
				printf("Error rendering glyph for '%c'\n", string[i]);
				continue;
			}
		}

		draw_char(&slot->bitmap, pen_x + slot->bitmap_left, pen_y - slot->bitmap_top, fg, window, ctx);
		pen_x += slot->advance.x >> 6;
		pen_y += slot->advance.y >> 6;
	}
}

void init_decorations() {
	FT_Init_FreeType(&library);
	_loadDejavu();
}

void render_decorations(window_t * window, uint8_t * ctx, char * title) {
	for (uint32_t i = 0; i < window->height; ++i) {
		SGFX(ctx,0,i,window->width) = BORDERCOLOR;
		SGFX(ctx,window->width-1,i,window->width) = BORDERCOLOR;
	}
	for (uint32_t i = 1; i < decor_top_height; ++i) {
		for (uint32_t j = 1; j < window->width - 1; ++j) {
			SGFX(ctx,j,i,window->width) = BACKCOLOR;
		}
	}
	draw_string(TEXT_OFFSET_X,TEXT_OFFSET_Y,TEXTCOLOR,title,window,ctx);
	for (uint32_t i = 0; i < window->width; ++i) {
		SGFX(ctx,i,0,window->width) = BORDERCOLOR;
		SGFX(ctx,i,decor_top_height,window->width) = BORDERCOLOR;
		SGFX(ctx,i,window->height-1,window->width) = BORDERCOLOR;
	}
}

uint32_t decor_width() {
	return decor_left_width + decor_right_width;
}

uint32_t decor_height() {
	return decor_top_height + decor_bottom_height;
}


