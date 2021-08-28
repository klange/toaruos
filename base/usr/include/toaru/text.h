#pragma once

#include <stdint.h>

extern struct TT_Font * tt_font_from_file(const char * fileName);
extern struct TT_Font * tt_font_from_shm(const char * identifier);
extern int tt_glyph_for_codepoint(struct TT_Font * font, unsigned int codepoint);
extern void tt_draw_glyph(gfx_context_t * ctx, struct TT_Font * font, int x_offset, int y_offset, unsigned int glyph, uint32_t color);
extern void tt_set_size(struct TT_Font * font, float sizeInEm);
extern void tt_set_size_px(struct TT_Font * font, float sizeInPx);
extern int tt_xadvance_for_glyph(struct TT_Font * font, unsigned int ind);
extern int tt_string_width(struct TT_Font * font, const char * s);
extern int tt_draw_string(gfx_context_t * ctx, struct TT_Font * font, int x, int y, const char * s, uint32_t color);
extern void tt_draw_string_shadow(gfx_context_t * ctx, struct TT_Font * font, char * string, int font_size, int left, int top, uint32_t text_color, uint32_t shadow_color, int blur);
