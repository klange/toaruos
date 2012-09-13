#ifndef SHMEMFONTS_H
#define SHMEMFONTS_H

#include "graphics.h"
#include "window.h"

void init_shmemfonts();
void draw_string(gfx_context_t * ctx, int x, int y, uint32_t fg, char * string);
uint32_t draw_string_width(char * string);
void set_font_size(int size);
void set_text_opacity(float new_opacity);

#endif
