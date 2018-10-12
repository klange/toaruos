#pragma once

#include <_cheader.h>
#include <toaru/graphics.h>

_Begin_C_Header

void draw_string(gfx_context_t * ctx, int x, int y, uint32_t _fg, char * str);
int draw_string_width(char * str);

_End_C_Header
