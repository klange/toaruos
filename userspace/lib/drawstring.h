#pragma once
#include "lib/graphics.h"

void draw_string(gfx_context_t * ctx, int x, int y, uint32_t _fg, char * str);
int draw_string_width(char * str);
