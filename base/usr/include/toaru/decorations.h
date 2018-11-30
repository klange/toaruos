/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2012-2018 K. Lange
 *
 * Client-side Window Decoration library
 */
#pragma once

#include <_cheader.h>
#include <toaru/graphics.h>
#include <toaru/yutani.h>

_Begin_C_Header

/*
 * Render decorations to a window. A buffer pointer is
 * provided so that you may render in double-buffered mode.
 *
 * Run me at least once for each window, and any time you may need to
 * redraw them.
 */
extern void render_decorations(yutani_window_t * window, gfx_context_t * ctx, char * title);

/** DEPRECATED */
extern void render_decorations_inactive(yutani_window_t * window, gfx_context_t * ctx, char * title);

/**
 * Decoration boundaries
 */
struct decor_bounds {
	int top_height;
	int bottom_height;
	int left_width;
	int right_width;

	/* Convenience */
	int width;
	int height;
};

/*
 * Used by decoration libraries to set callbacks
 */
extern void (*decor_render_decorations)(yutani_window_t *, gfx_context_t *, char *, int);
extern int  (*decor_check_button_press)(yutani_window_t *, int x, int y);
extern int  (*decor_get_bounds)(yutani_window_t *, struct decor_bounds *);

/*
 * Run me once to set things up
 */
extern void init_decorations();

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

#define DECOR_FLAG_DECORATED   (1 << 0)
#define DECOR_FLAG_NO_MAXIMIZE (1 << 1)
#define DECOR_FLAG_TILED       (0xF << 2)
#define DECOR_FLAG_TILE_LEFT   (0x1 << 2)
#define DECOR_FLAG_TILE_RIGHT  (0x2 << 2)
#define DECOR_FLAG_TILE_UP     (0x4 << 2)
#define DECOR_FLAG_TILE_DOWN   (0x8 << 2)

_End_C_Header
