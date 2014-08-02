/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Decoration Library Headers
 *
 */

#ifndef DECORATIONS_H
#define DECORATIONS_H

#include "graphics.h"
#include "yutani.h"

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
void render_decorations(yutani_window_t * window, gfx_context_t * ctx, char * title);
void render_decorations_inactive(yutani_window_t * window, gfx_context_t * ctx, char * title);

/*
 * Run me once to set things up
 */
void init_decorations();

uint32_t decor_width();
uint32_t decor_height();

int decor_handle_event(yutani_t * yctx, yutani_msg_t * m);

/* Callbacks for handle_event */
void decor_set_close_callback(void (*callback)(yutani_window_t *));
void decor_set_resize_callback(void (*callback)(yutani_window_t *));

/* Responses from handle_event */
#define DECOR_OTHER  1
#define DECOR_CLOSE  2
#define DECOR_RESIZE 3

#endif /* DECORATION_H */
