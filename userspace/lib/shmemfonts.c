/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 *
 * Shared-memory font management and access library.
 *
 */

#include <stdint.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_CACHE_H

#include "graphics.h"
#include "shmemfonts.h"

static FT_Library   library;
static FT_Face      face; /* perhaps make this an array ? */
static FT_GlyphSlot slot;
static FT_UInt      glyph_index;
static int initialized = 0;

#define SGFX(CTX,x,y,WIDTH) *((uint32_t *)&CTX[((WIDTH) * (y) + (x)) * 4])
#define FONT_SIZE 12

/*
 * XXX: take font name as an argument / allow multiple fonts
 */
static void _loadSansSerif() {
	char * font;
	size_t s = 0;
	int error;
	font = (char *)syscall_shm_obtain(WINS_SERVER_IDENTIFIER ".fonts.sans-serif", &s);
	error = FT_New_Memory_Face(library, font, s, 0, &face);
	error = FT_Set_Pixel_Sizes(face, FONT_SIZE, FONT_SIZE);
}

void init_shmemfonts() {
	if (!initialized) {
		FT_Init_FreeType(&library);
		_loadSansSerif();
		initialized = 1;
	}
}

/*
 * Draw a character to a context.
 */
static void draw_char(FT_Bitmap * bitmap, int x, int y, uint32_t fg, gfx_context_t * ctx) {
	int i, j, p, q;
	int x_max = x + bitmap->width;
	int y_max = y + bitmap->rows;
	for (j = y, q = 0; j < y_max; j++, q++) {
		for ( i = x, p = 0; i < x_max; i++, p++) {
			SGFX(ctx->backbuffer,i,j,ctx->width) = alpha_blend(SGFX(ctx->backbuffer,i,j,ctx->width),fg,rgb(bitmap->buffer[q * bitmap->width + p],0,0));
		}
	}
}

uint32_t draw_string_width(char * string) {
	slot = face->glyph;
	int pen_x = 0, i = 0;
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
		pen_x += slot->advance.x >> 6;
	}
	return pen_x;
}

void draw_string(gfx_context_t * ctx, int x, int y, uint32_t fg, char * string) {
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

		draw_char(&slot->bitmap, pen_x + slot->bitmap_left, pen_y - slot->bitmap_top, fg, ctx);
		pen_x += slot->advance.x >> 6;
		pen_y += slot->advance.y >> 6;
	}
}

