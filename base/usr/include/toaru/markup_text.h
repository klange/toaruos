#pragma once
#include <_cheader.h>
#include <toaru/graphics.h>

_Begin_C_Header

int markup_string_width(const char * str);
int markup_string_height(const char * str);
int markup_draw_string(gfx_context_t * ctx, int x, int y, const char * str, uint32_t color);
void markup_text_init(void);

_End_C_Header
