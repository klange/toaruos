/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Decoration Library Headers
 *
 */

#pragma once

#include <toaru/graphics.h>
#include <toaru/yutani.h>

extern uint32_t decor_top_height;
extern uint32_t decor_bottom_height;
extern uint32_t decor_left_width;
extern uint32_t decor_right_width;

/*
 * Render decorations to a window. A buffer pointer is
 * provided so that you may render in double-buffered mode.
 *
 * Run me at least once for each window, and any time you may need to
 * redraw them.
 */
extern void render_decorations(yutani_window_t * window, gfx_context_t * ctx, char * title);
extern void render_decorations_inactive(yutani_window_t * window, gfx_context_t * ctx, char * title);

/*
 * Used by decoration libraries to set callbacks
 */
extern void (*decor_render_decorations)(yutani_window_t *, gfx_context_t *, char *, int);
extern int  (*decor_check_button_press)(yutani_window_t *, int x, int y);

/*
 * Run me once to set things up
 */
extern void init_decorations();

extern uint32_t decor_width();
extern uint32_t decor_height();

extern int decor_handle_event(yutani_t * yctx, yutani_msg_t * m);

/* Callbacks for handle_event */
extern void decor_set_close_callback(void (*callback)(yutani_window_t *));
extern void decor_set_resize_callback(void (*callback)(yutani_window_t *));
extern void decor_set_maximize_callback(void (*callback)(yutani_window_t *));
extern yutani_window_t * decor_show_default_menu(yutani_window_t * window, int y, int x);

/* Responses from handle_event */
#define DECOR_OTHER     1 /* Clicked on title bar but otherwise unimportant */
#define DECOR_CLOSE     2 /* Clicked on close button */
#define DECOR_RESIZE    3 /* Resize button */
#define DECOR_MAXIMIZE  4
#define DECOR_RIGHT     5

#define DECOR_ACTIVE   0
#define DECOR_INACTIVE 1

