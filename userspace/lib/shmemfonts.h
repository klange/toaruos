#ifndef SHMEMFONTS_H
#define SHMEMFONTS_H

#include "graphics.h"
#include "window.h"

void init_shmemfonts();
void draw_string(gfx_context_t * ctx, int x, int y, uint32_t fg, char * string);
uint32_t draw_string_width(char * string);
void draw_string_shadow(gfx_context_t * ctx, int x, int y, uint32_t fg, char * string, uint32_t shadow_color, int darkness, int offset_x, int offset_y, double radius);
void set_font_size(int size);
void set_font_face(int face_num);
char * shmem_font_name(int i);

#define FONT_SANS_SERIF             0
#define FONT_SANS_SERIF_BOLD        1
#define FONT_SANS_SERIF_ITALIC      2
#define FONT_SANS_SERIF_BOLD_ITALIC 3
#define FONT_MONOSPACE              4
#define FONT_MONOSPACE_BOLD         5
#define FONT_MONOSPACE_ITALIC       6
#define FONT_MONOSPACE_BOLD_ITALIC  7
#define FONT_JAPANESE               8

#define FONTS_TOTAL 9

#endif
