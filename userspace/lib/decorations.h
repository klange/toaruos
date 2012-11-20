/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Decoration Library Headers
 *
 */

#ifndef DECORATIONS_H
#define DECORATIONS_H

#include "graphics.h"
#include "window.h"

uint32_t decor_top_height;
uint32_t decor_bottom_height;
uint32_t decor_left_width;
uint32_t decor_right_width;

/*
 * Render decorations to a window. A buffer pointer is
 * provided so that you may render in double-buffered mode.
 *
 * Run me at least once for each window, and any time you may need to
 * redraw them.
 */
void render_decorations(window_t * window, gfx_context_t * ctx, char * title);
void render_decorations_inactive(window_t * window, gfx_context_t * ctx, char * title);

/*
 * Run me once to set things up
 */
void init_decorations();

uint32_t decor_width();
uint32_t decor_height();

#endif /* DECORATION_H */
