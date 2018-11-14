#pragma once
#include <_cheader.h>
#include <stdint.h>
#include <toaru/graphics.h>

_Begin_C_Header

enum sdf_font {
    SDF_FONT_THIN,
    SDF_FONT_BOLD,
    SDF_FONT_MONO,
    SDF_FONT_MONO_BOLD,
    SDF_FONT_MONO_OBLIQUE,
    SDF_FONT_MONO_BOLD_OBLIQUE,
    SDF_FONT_OBLIQUE,
    SDF_FONT_BOLD_OBLIQUE,
};

extern int draw_sdf_string(gfx_context_t * ctx, int32_t x, int32_t y, const char * str, int size, uint32_t color, int font);
extern int draw_sdf_string_width(const char * str, int size, int font);
extern int draw_sdf_string_gamma(gfx_context_t * ctx, int32_t x, int32_t y, const char * str, int size, uint32_t color, int font, double _gamma);

_End_C_Header
