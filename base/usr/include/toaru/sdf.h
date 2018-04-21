#pragma once

enum sdf_font {
    SDF_FONT_THIN,
    SDF_FONT_BOLD,
};

extern int draw_sdf_string(gfx_context_t * ctx, int32_t x, int32_t y, const char * str, int size, uint32_t color, int font);
extern int draw_sdf_string_width(const char * str, int size, int font);
