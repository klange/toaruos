/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 *
 * Extension library for freetype font rendering.
 */
#include <syscall.h>
#include <toaru/yutani.h>
#include <toaru/graphics.h>
#include <toaru/decodeutf8.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_CACHE_H

#define SERVER_NAME "fonts"

#define FONT_SANS_SERIF             0
#define FONT_SANS_SERIF_BOLD        1
#define FONT_SANS_SERIF_ITALIC      2
#define FONT_SANS_SERIF_BOLD_ITALIC 3
#define FONT_MONOSPACE              4
#define FONT_MONOSPACE_BOLD         5
#define FONT_MONOSPACE_ITALIC       6
#define FONT_MONOSPACE_BOLD_ITALIC  7
#define FONT_JAPANESE               8
#define FONT_SYMBOLA                9
#define FONTS_TOTAL 10

#define FONT_SIZE 12

static FT_Library   library;
static FT_Face      faces[FONTS_TOTAL]; /* perhaps make this an array ? */
static FT_GlyphSlot slot;
static int selected_face = 0;
static int fallbacks[] = {FONT_JAPANESE, FONT_SYMBOLA, -1};

#define SGFX(CTX,x,y,WIDTH) *((uint32_t *)&CTX[((WIDTH) * (y) + (x)) * 4])

static void _load_font(int i, char * name) {
	uint8_t * font;
	size_t s = 0;
	int error;
	char tmp[100];
	snprintf(tmp, 100, "sys.%s%s", SERVER_NAME, name);

	font = (void*)syscall_shm_obtain(tmp, &s);
	error = FT_New_Memory_Face(library, font, s, 0, &faces[i]);
	if (error) {
		fprintf(stderr, "[freetype backend] encountered error\n");
	}
	error = FT_Set_Pixel_Sizes(faces[i], FONT_SIZE, FONT_SIZE);
	if (error) {
		fprintf(stderr, "[freetype backend] encountered error\n");
	}
}

static void _load_font_f(int i, char * path) {
	int error;
	error = FT_New_Face(library, path, 0, &faces[i]);
	if (error) {
		fprintf(stderr, "[freetype backend] encountered error\n");
	}
	error = FT_Set_Pixel_Sizes(faces[i], FONT_SIZE, FONT_SIZE);
	if (error) {
		fprintf(stderr, "[freetype backend] encountered error\n");
	}
}

static void _load_fonts() {
	_load_font(FONT_SANS_SERIF,             ".fonts.sans-serif");
	_load_font(FONT_SANS_SERIF_BOLD,        ".fonts.sans-serif.bold");
	_load_font(FONT_SANS_SERIF_ITALIC,      ".fonts.sans-serif.italic");
	_load_font(FONT_SANS_SERIF_BOLD_ITALIC, ".fonts.sans-serif.bolditalic");
	_load_font(FONT_MONOSPACE,              ".fonts.monospace");
	_load_font(FONT_MONOSPACE_BOLD,         ".fonts.monospace.bold");
	_load_font(FONT_MONOSPACE_ITALIC,       ".fonts.monospace.italic");
	_load_font(FONT_MONOSPACE_BOLD_ITALIC,  ".fonts.monospace.bolditalic");
	_load_font_f(FONT_JAPANESE, "/usr/share/fonts/VLGothic.ttf");
	_load_font_f(FONT_SYMBOLA, "/usr/share/fonts/Symbola.ttf");
}

void set_font_face(int font) {
	selected_face = font;
}

void set_font_size(int size) {
	for (int i = 0; i < FONTS_TOTAL; ++i) {
		FT_Set_Pixel_Sizes(faces[i], size, size);
	}
}

static void draw_char(FT_Bitmap * bitmap, int x, int y, uint32_t fg, gfx_context_t * ctx) {
	int i, j, p, q;
	int x_max = x + bitmap->width;
	int y_max = y + bitmap->rows;
	for (j = y, q = 0; j < y_max; j++, q++) {
		if (j < 0 || j >= ctx->height) continue;
		for ( i = x, p = 0; i < x_max; i++, p++) {
			uint32_t a = _ALP(fg);
			a = (a * bitmap->buffer[q * bitmap->width + p]) / 255;
			uint32_t tmp = premultiply(rgba(_RED(fg),_GRE(fg),_BLU(fg),a));
			if (i < 0 || i >= ctx->width) continue;
			SGFX(ctx->backbuffer,i,j,ctx->width) = alpha_blend_rgba(SGFX(ctx->backbuffer,i,j,ctx->width),tmp);
		}
	}
}

void draw_string(gfx_context_t * ctx, int x, int y, uint32_t fg, char * string) {
	slot = faces[selected_face]->glyph;
	int pen_x = x, pen_y = y;
	int error;

	uint8_t * s = (uint8_t *)string;

	uint32_t codepoint;
	uint32_t state = 0;

	while (*s) {
		uint32_t o = 0;
		while (*s) {
			if (!decode(&state, &codepoint, (uint8_t)*s)) {
				o = (uint32_t)codepoint;
				s++;
				goto finished;
			} else if (state == UTF8_REJECT) {
				state = 0;
			}
			s++;
		}

finished:
		if (!o) continue;

		FT_UInt glyph_index;

		glyph_index = FT_Get_Char_Index( faces[selected_face], o);
		if (glyph_index) {
			error = FT_Load_Glyph(faces[selected_face], glyph_index, FT_LOAD_DEFAULT);
			if (error) {
				fprintf(stderr, "Error loading glyph for '%lu'\n", o);
				continue;
			}
			slot = (faces[selected_face])->glyph;
			if (slot->format == FT_GLYPH_FORMAT_OUTLINE) {
				error = FT_Render_Glyph((faces[selected_face])->glyph, FT_RENDER_MODE_NORMAL);
				if (error) {
					fprintf(stderr, "Error rendering glyph for '%lu'\n", o);
					continue;
				}
			}
		} else {
			int i = 0;
			while (!glyph_index && fallbacks[i] != -1) {
				int fallback = fallbacks[i++];
				glyph_index = FT_Get_Char_Index( faces[fallback], o);
				error = FT_Load_Glyph(faces[fallback], glyph_index, FT_LOAD_DEFAULT);
				if (error) {
					fprintf(stderr, "Error loading glyph for '%lu'\n", o);
					continue;
				}
				slot = (faces[fallback])->glyph;
				if (slot->format == FT_GLYPH_FORMAT_OUTLINE) {
					error = FT_Render_Glyph((faces[fallback])->glyph, FT_RENDER_MODE_NORMAL);
					if (error) {
						fprintf(stderr, "Error rendering glyph for '%lu'\n", o);
						continue;
					}
				}
			}

		}

		draw_char(&slot->bitmap, pen_x + slot->bitmap_left, pen_y - slot->bitmap_top, fg, ctx);
		pen_x += slot->advance.x >> 6;
		pen_y += slot->advance.y >> 6;
	}
}

__attribute__((constructor)) static void init_lib(void) {
	FT_Init_FreeType(&library);
	_load_fonts();
	selected_face = FONT_SANS_SERIF;
}
