/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014-2018 K. Lange
 *
 * Yutani Client Library
 *
 * Client library for the compositing window system.
 */
#pragma once

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include <toaru/hashmap.h>
#include <toaru/graphics.h>
#include <toaru/kbd.h>
#include <toaru/mouse.h>
#include <toaru/list.h>

typedef unsigned int yutani_wid_t;

/*
 * Server connection context.
 */
typedef struct yutani_context {
	FILE * sock;

	/* server display size */
	size_t display_width;
	size_t display_height;

	/* Hash of window IDs to window objects */
	hashmap_t * windows;

	/* queued events */
	list_t * queued;

	/* server identifier string */
	char * server_ident;
} yutani_t;

typedef struct yutani_window {
	/* Server window identifier, unique to each window */
	yutani_wid_t wid;

	/* Window size */
	uint32_t width;
	uint32_t height;

	/* Window backing buffer */
	char * buffer;
	/*
	 * Because the buffer can change during resizing,
	 * buffers are indexed to ensure we are using
	 * the one the server expects.
	 */
	uint32_t bufid;

	/* Window focused flag */
	uint8_t focused;

	/* Old buffer ID */
	uint32_t oldbufid;

	/* Generic pointer for client use */
	void * user_data;

	/* Window position in the server; automatically updated */
	int32_t x;
	int32_t y;

	/* Flags for the decorator library to use */
	uint32_t decorator_flags;

	/* Server context that owns this window */
	yutani_t * ctx;
} yutani_window_t;

typedef struct yutani_message {
	uint32_t magic;
	uint32_t type;
	uint32_t size;
	char data[];
} yutani_msg_t;

struct yutani_msg_welcome {
	uint32_t display_width;
	uint32_t display_height;
};

struct yutani_msg_flip {
	yutani_wid_t wid;
};

struct yutani_msg_window_close {
	yutani_wid_t wid;
};

struct yutani_msg_window_new {
	uint32_t width;
	uint32_t height;
};

struct yutani_msg_window_new_flags {
	uint32_t width;
	uint32_t height;
	uint32_t flags;
};

struct yutani_msg_window_init {
	yutani_wid_t wid;
	uint32_t width;
	uint32_t height;
	uint32_t bufid;
};

struct yutani_msg_window_move {
	yutani_wid_t wid;
	int32_t x;
	int32_t y;
};

struct yutani_msg_key_event {
	yutani_wid_t wid;
	key_event_t event;
	key_event_state_t state;
};

struct yutani_msg_window_stack {
	yutani_wid_t wid;
	int z;
};

struct yutani_msg_window_focus_change {
	yutani_wid_t wid;
	int focused;
};

struct yutani_msg_window_mouse_event {
	yutani_wid_t wid;
	int32_t new_x;
	int32_t new_y;
	int32_t old_x;
	int32_t old_y;
	uint8_t buttons;
	uint8_t command;
};

struct yutani_msg_mouse_event {
	yutani_wid_t wid;
	mouse_device_packet_t event;
	int32_t type;
};

struct yutani_msg_flip_region {
	yutani_wid_t wid;
	int32_t x;
	int32_t y;
	int32_t width;
	int32_t height;
};

struct yutani_msg_window_resize {
	yutani_wid_t wid;
	uint32_t width;
	uint32_t height;
	uint32_t bufid;
	uint32_t flags;
};

struct yutani_msg_window_advertise {
	yutani_wid_t wid;
	uint32_t flags; /* Types, focused, etc. */
	uint16_t offsets[5]; /* Name, Icon, and three reserved slots */
	uint32_t size;
	char strings[];
};

struct yutani_msg_window_focus {
	yutani_wid_t wid;
};

struct yutani_msg_key_bind {
	kbd_key_t key;
	kbd_mod_t modifiers;
	int response;
};

struct yutani_msg_window_drag_start {
	yutani_wid_t wid;
};

struct yutani_msg_window_update_shape {
	yutani_wid_t wid;
	int set_shape;
};

struct yutani_msg_window_warp_mouse {
	yutani_wid_t wid;
	int32_t x;
	int32_t y;
};

struct yutani_msg_window_show_mouse {
	yutani_wid_t wid;
	int32_t show_mouse;
};

typedef enum {
	SCALE_AUTO,

	SCALE_UP,
	SCALE_DOWN,
	SCALE_LEFT,
	SCALE_RIGHT,

	SCALE_UP_LEFT,
	SCALE_UP_RIGHT,
	SCALE_DOWN_LEFT,
	SCALE_DOWN_RIGHT,

	SCALE_NONE,
} yutani_scale_direction_t;

struct yutani_msg_window_resize_start {
	yutani_wid_t wid;
	yutani_scale_direction_t direction;
};

struct yutani_msg_special_request {
	yutani_wid_t wid;
	uint32_t request;
};

struct yutani_msg_clipboard {
	uint32_t size;
	char content[];
};

/* Magic value */
#define YUTANI_MSG__MAGIC 0xABAD1DEA

/* Client messages */
#define YUTANI_MSG_HELLO               0x00000001
#define YUTANI_MSG_WINDOW_NEW          0x00000002
#define YUTANI_MSG_FLIP                0x00000003
#define YUTANI_MSG_KEY_EVENT           0x00000004
#define YUTANI_MSG_MOUSE_EVENT         0x00000005
#define YUTANI_MSG_WINDOW_MOVE         0x00000006
#define YUTANI_MSG_WINDOW_CLOSE        0x00000007
#define YUTANI_MSG_WINDOW_SHOW         0x00000008
#define YUTANI_MSG_WINDOW_HIDE         0x00000009
#define YUTANI_MSG_WINDOW_STACK        0x0000000A
#define YUTANI_MSG_WINDOW_FOCUS_CHANGE 0x0000000B
#define YUTANI_MSG_WINDOW_MOUSE_EVENT  0x0000000C
#define YUTANI_MSG_FLIP_REGION         0x0000000D
#define YUTANI_MSG_WINDOW_NEW_FLAGS    0x0000000E

#define YUTANI_MSG_RESIZE_REQUEST      0x00000010
#define YUTANI_MSG_RESIZE_OFFER        0x00000011
#define YUTANI_MSG_RESIZE_ACCEPT       0x00000012
#define YUTANI_MSG_RESIZE_BUFID        0x00000013
#define YUTANI_MSG_RESIZE_DONE         0x00000014

/* Some session management / de stuff */
#define YUTANI_MSG_WINDOW_ADVERTISE    0x00000020
#define YUTANI_MSG_SUBSCRIBE           0x00000021
#define YUTANI_MSG_UNSUBSCRIBE         0x00000022
#define YUTANI_MSG_NOTIFY              0x00000023
#define YUTANI_MSG_QUERY_WINDOWS       0x00000024
#define YUTANI_MSG_WINDOW_FOCUS        0x00000025
#define YUTANI_MSG_WINDOW_DRAG_START   0x00000026
#define YUTANI_MSG_WINDOW_WARP_MOUSE   0x00000027
#define YUTANI_MSG_WINDOW_SHOW_MOUSE   0x00000028
#define YUTANI_MSG_WINDOW_RESIZE_START 0x00000029

#define YUTANI_MSG_SESSION_END         0x00000030

#define YUTANI_MSG_KEY_BIND            0x00000040

#define YUTANI_MSG_WINDOW_UPDATE_SHAPE 0x00000050

#define YUTANI_MSG_CLIPBOARD           0x00000060

#define YUTANI_MSG_GOODBYE             0x000000F0

/* Special request (eg. one-off single-shot requests like "please maximize me" */
#define YUTANI_MSG_SPECIAL_REQUEST     0x00000100

/* Server responses */
#define YUTANI_MSG_WELCOME             0x00010001
#define YUTANI_MSG_WINDOW_INIT         0x00010002

/*
 * YUTANI_ZORDER
 *
 * Specifies which stack set a window should appear in.
 */
#define YUTANI_ZORDER_MAX    0xFFFF
#define YUTANI_ZORDER_TOP    0xFFFF
#define YUTANI_ZORDER_BOTTOM 0x0000

/*
 * YUTANI_MOUSE_BUTTON
 *
 * Button specifiers. Multiple specifiers may be set.
 */
#define YUTANI_MOUSE_BUTTON_LEFT   0x01
#define YUTANI_MOUSE_BUTTON_RIGHT  0x02
#define YUTANI_MOUSE_BUTTON_MIDDLE 0x04
#define YUTANI_MOUSE_SCROLL_UP     0x10
#define YUTANI_MOUSE_SCROLL_DOWN   0x20

/*
 * YUTANI_MOUSE_STATE
 *
 * The mouse has for effective states internally:
 *
 * NORMAL: The mouse is performing normally.
 * MOVING: The mouse is engaged in moving a window.
 * DRAGGING: The mouse is down and sending drag events.
 * RESIZING: The mouse is engaged in resizing a window.
 */
#define YUTANI_MOUSE_STATE_NORMAL     0
#define YUTANI_MOUSE_STATE_MOVING     1
#define YUTANI_MOUSE_STATE_DRAGGING   2
#define YUTANI_MOUSE_STATE_RESIZING   3
#define YUTANI_MOUSE_STATE_ROTATING   4

/*
 * YUTANI_MOUSE_EVENT
 *
 * Mouse events have different types.
 *
 * Most of these should be self-explanatory.
 *
 * CLICK: A down-up click has occured.
 * DRAG: The mouse is down and moving.
 * RAISE: A mouse button was released.
 * DOWN: A mouse button has been pressed.
 * MOVE: The mouse has moved without a mouse button pressed.
 * LEAVE: The mouse has left the given window.
 * ENTER: The mouse has entered the given window.
 */
#define YUTANI_MOUSE_EVENT_CLICK 0
#define YUTANI_MOUSE_EVENT_DRAG  1
#define YUTANI_MOUSE_EVENT_RAISE 2
#define YUTANI_MOUSE_EVENT_DOWN  3
#define YUTANI_MOUSE_EVENT_MOVE  4
#define YUTANI_MOUSE_EVENT_LEAVE 5
#define YUTANI_MOUSE_EVENT_ENTER 6

/*
 * YUTANI_MOUSE_EVENT_TYPE
 *
 * (For mouse drivers)
 *
 * RELATIVE: Mouse positions are relative to the previous reported location.
 * ABSOLUTE: Mouse positions are in absolute coordinates.
 */
#define YUTANI_MOUSE_EVENT_TYPE_RELATIVE 0
#define YUTANI_MOUSE_EVENT_TYPE_ABSOLUTE 1

/*
 * YUTANI_BIND
 *
 * Used to control keyboard binding modes.
 *
 * PASSTHROUGH: The key event will continue to the window that would have normally received.
 * STEAL: The key event will not be passed to the next window and is stolen by the bound window.
 */
#define YUTANI_BIND_PASSTHROUGH 0
#define YUTANI_BIND_STEAL       1

/*
 * YUTANI_SHAPE_THRESHOLD
 *
 * Used with yutani_window_update_shape to set the alpha threshold for window shaping.
 * All windows are shaped based on their transparency (alpha channel). The default
 * mode is NONE - meaning the alpha channel is ignored.
 *
 * NONE:  The window is always clickable, regardless of alpha transparency.
 * CLEAR: Only completely clear (alpha = 0) regions will pass through.
 * HALF:  Threshold of 50% - alpha values below 127 will pass through. Good for most cases.
 * ANY:   Any amount of alpha transparency will pass through - only fully opaque regions are kept.
 * PASSTHROUGH: All clicks pass through. Useful for tooltips / overlays.
 */
#define YUTANI_SHAPE_THRESHOLD_NONE        0
#define YUTANI_SHAPE_THRESHOLD_CLEAR       1
#define YUTANI_SHAPE_THRESHOLD_HALF        127
#define YUTANI_SHAPE_THRESHOLD_ANY         255
#define YUTANI_SHAPE_THRESHOLD_PASSTHROUGH 256

/*
 * YUTANI_CURSOR_TYPE
 *
 * Used with SHOW_MOUSE to set the cursor type for this window.
 * Note that modifications made to the cursor will only display
 * while it the current window is active and that cursor settings
 * are per-window, not per-application.
 *
 * HIDE:              Disable the mouse cursor. Useful for games.
 * NORMAL:            The normal arrow cursor.
 * DRAG:              A 4-directional arrow.
 * RESIZE_VERTICAL:   An up-down arrow / resize indicator.
 * RESIZE_HORIZONTAL: A left-right arrow / resize indicator.
 * RESIZE_UP_DOWN:    A diagonal ＼-shaped arrow.
 * RESIZE_DOWN_UP:    A diagonal ／-shaped arrow.
 *
 * RESET: If the cursor was previously hidden, hide it again.
 *        Otherwise, show the normal cursor. Allows for decorator
 *        to set resize cursors without having to know if a window
 *        had set the default mode to HIDE.
 */
#define YUTANI_CURSOR_TYPE_RESET            -1
#define YUTANI_CURSOR_TYPE_HIDE              0
#define YUTANI_CURSOR_TYPE_NORMAL            1
#define YUTANI_CURSOR_TYPE_DRAG              2
#define YUTANI_CURSOR_TYPE_RESIZE_VERTICAL   3
#define YUTANI_CURSOR_TYPE_RESIZE_HORIZONTAL 4
#define YUTANI_CURSOR_TYPE_RESIZE_UP_DOWN    5
#define YUTANI_CURSOR_TYPE_RESIZE_DOWN_UP    6

/*
 * YUTANI_WINDOW_FLAG
 *
 * Flags for new windows describing how the window
 * should be created.
 */
#define YUTANI_WINDOW_FLAG_NO_STEAL_FOCUS   (1 << 0)
#define YUTANI_WINDOW_FLAG_DISALLOW_DRAG    (1 << 1)
#define YUTANI_WINDOW_FLAG_DISALLOW_RESIZE  (1 << 2)
#define YUTANI_WINDOW_FLAG_ALT_ANIMATION    (1 << 3)

/* YUTANI_SPECIAL_REQUEST
 *
 * Special one-off single-shot request messages.
 */
#define YUTANI_SPECIAL_REQUEST_MAXIMIZE     1
#define YUTANI_SPECIAL_REQUEST_PLEASE_CLOSE 2

#define YUTANI_SPECIAL_REQUEST_CLIPBOARD    10

#define YUTANI_SPECIAL_REQUEST_RELOAD       20

/*
 * YUTANI_RESIZE
 *
 * Flags provided in resize offers describing the window state.
 */
#define YUTANI_RESIZE_NORMAL 0x00000000
#define YUTANI_RESIZE_TILED  0x0000000f

#define YUTANI_RESIZE_TILE_LEFT  0x00000001
#define YUTANI_RESIZE_TILE_RIGHT 0x00000002
#define YUTANI_RESIZE_TILE_UP    0x00000004
#define YUTANI_RESIZE_TILE_DOWN  0x00000008

typedef struct {
	int x;
	int y;
	unsigned int width;
	unsigned int height;
} yutani_damage_rect_t;

extern yutani_msg_t * yutani_wait_for(yutani_t * y, uint32_t type);
extern yutani_msg_t * yutani_poll(yutani_t * y);
extern yutani_msg_t * yutani_poll_async(yutani_t * y);
extern size_t yutani_query(yutani_t * y);

extern int yutani_msg_send(yutani_t * y, yutani_msg_t * msg);
extern yutani_t * yutani_context_create(FILE * socket);
extern yutani_t * yutani_init(void);
extern yutani_window_t * yutani_window_create(yutani_t * y, int width, int height);
extern yutani_window_t * yutani_window_create_flags(yutani_t * y, int width, int height, uint32_t flags);
extern void yutani_flip(yutani_t * y, yutani_window_t * win);
extern void yutani_window_move(yutani_t * yctx, yutani_window_t * window, int x, int y);
extern void yutani_close(yutani_t * y, yutani_window_t * win);
extern void yutani_set_stack(yutani_t *, yutani_window_t *, int);
extern void yutani_flip_region(yutani_t *, yutani_window_t * win, int32_t x, int32_t y, int32_t width, int32_t height);
extern void yutani_window_resize(yutani_t * yctx, yutani_window_t * window, uint32_t width, uint32_t height);
extern void yutani_window_resize_offer(yutani_t * yctx, yutani_window_t * window, uint32_t width, uint32_t height);
extern void yutani_window_resize_accept(yutani_t * yctx, yutani_window_t * window, uint32_t width, uint32_t height);
extern void yutani_window_resize_done(yutani_t * yctx, yutani_window_t * window);
extern void yutani_window_advertise(yutani_t * yctx, yutani_window_t * window, char * name);
extern void yutani_window_advertise_icon(yutani_t * yctx, yutani_window_t * window, char * name, char * icon);
extern void yutani_subscribe_windows(yutani_t * y);
extern void yutani_unsubscribe_windows(yutani_t * y);
extern void yutani_query_windows(yutani_t * y);
extern void yutani_session_end(yutani_t * y);
extern void yutani_focus_window(yutani_t * y, yutani_wid_t wid);
extern void yutani_key_bind(yutani_t * yctx, kbd_key_t key, kbd_mod_t mod, int response);
extern void yutani_window_drag_start(yutani_t * yctx, yutani_window_t * window);
extern void yutani_window_drag_start_wid(yutani_t * yctx, yutani_wid_t wid);
extern void yutani_window_update_shape(yutani_t * yctx, yutani_window_t * window, int set_shape);
extern void yutani_window_warp_mouse(yutani_t * yctx, yutani_window_t * window, int32_t x, int32_t y);
extern void yutani_window_show_mouse(yutani_t * yctx, yutani_window_t * window, int32_t show_mouse);
extern void yutani_window_resize_start(yutani_t * yctx, yutani_window_t * window, yutani_scale_direction_t direction);
extern void yutani_special_request(yutani_t * yctx, yutani_window_t * window, uint32_t request);
extern void yutani_special_request_wid(yutani_t * yctx, yutani_wid_t wid, uint32_t request);
extern void yutani_set_clipboard(yutani_t * yctx, char * content);
extern FILE * yutani_open_clipboard(yutani_t * yctx);

extern gfx_context_t * init_graphics_yutani(yutani_window_t * window);
extern gfx_context_t *  init_graphics_yutani_double_buffer(yutani_window_t * window);
extern void reinit_graphics_yutani(gfx_context_t * out, yutani_window_t * window);
extern void release_graphics_yutani(gfx_context_t * gfx);

