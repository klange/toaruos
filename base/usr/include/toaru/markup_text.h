#pragma once
#include <_cheader.h>
#include <toaru/graphics.h>

_Begin_C_Header

int markup_string_width(const char * str);
int markup_string_height(const char * str);
int markup_draw_string(gfx_context_t * ctx, int x, int y, const char * str, uint32_t color);
void markup_text_init(void);
struct MarkupState * markup_setup_renderer(gfx_context_t * ctx, int x, int y, uint32_t color, int dryrun);
void markup_set_base_font_size(struct MarkupState * state, int size);
void markup_set_base_state(struct MarkupState * state, int mode);
int markup_push_string(struct MarkupState * state, const char * str);
int markup_push_raw_string(struct MarkupState * state, const char * str);
int markup_finish_renderer(struct MarkupState * state);

#define MARKUP_TEXT_STATE_BOLD     (1 << 0)
#define MARKUP_TEXT_STATE_OBLIQUE  (1 << 1)
#define MARKUP_TEXT_STATE_HEADING  (1 << 2)
#define MARKUP_TEXT_STATE_SMALL    (1 << 3)
#define MARKUP_TEXT_STATE_MONO     (1 << 4)


_End_C_Header
