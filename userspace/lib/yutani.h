#ifndef _YUTANI_H
#define _YUTANI_H

#include <stdio.h>
#include <stdint.h>

#include "hashmap.h"
#include "graphics.h"
#include "kbd.h"
#include "mouse.h"
#include "list.h"

#define YUTANI_SHMKEY(server_ident,buf,sz,win) snprintf(buf, sz, "sys.%s.%d", server_ident, win->bufid);
#define YUTANI_SHMKEY_EXP(server_ident,buf,sz,bufid) snprintf(buf, sz, "sys.%s.%d", server_ident, bufid);

typedef unsigned int yutani_wid_t;

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

typedef struct yutani_context {
	FILE * sock;

	/* XXX list of displays? */
	/* XXX display struct with more information? */
	size_t display_width;
	size_t display_height;

	hashmap_t * windows;
	list_t * queued;

	char * server_ident;
} yutani_t;

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

struct yutani_msg_window_resize_start {
	yutani_wid_t wid;
	yutani_scale_direction_t direction;
};


typedef struct yutani_window {
	yutani_wid_t wid;

	uint32_t width;
	uint32_t height;

	uint8_t * buffer;
	uint32_t bufid;/* We occasionally replace the buffer; each is uniquely-indexed */

	uint8_t focused;

	uint32_t oldbufid;
} yutani_window_t;

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

#define YUTANI_MSG_GOODBYE             0x000000F0

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

typedef struct {
	int x;
	int y;
	unsigned int width;
	unsigned int height;
} yutani_damage_rect_t;

yutani_msg_t * yutani_wait_for(yutani_t * y, uint32_t type);
yutani_msg_t * yutani_poll(yutani_t * y);
yutani_msg_t * yutani_poll_async(yutani_t * y);
size_t yutani_query(yutani_t * y);

yutani_msg_t * yutani_msg_build_hello(void);
yutani_msg_t * yutani_msg_build_welcome(uint32_t width, uint32_t height);
yutani_msg_t * yutani_msg_build_window_new(uint32_t width, uint32_t height);
yutani_msg_t * yutani_msg_build_window_init(yutani_wid_t wid, uint32_t width, uint32_t height, uint32_t bufid);
yutani_msg_t * yutani_msg_build_flip(yutani_wid_t);
yutani_msg_t * yutani_msg_build_key_event(yutani_wid_t wid, key_event_t * event, key_event_state_t * state);
yutani_msg_t * yutani_msg_build_mouse_event(yutani_wid_t wid, mouse_device_packet_t * event, int32_t type);
yutani_msg_t * yutani_msg_build_window_move(yutani_wid_t wid, int32_t x, int32_t y);
yutani_msg_t * yutani_msg_build_window_close(yutani_wid_t wid);
yutani_msg_t * yutani_msg_build_window_stack(yutani_wid_t wid, int z);
yutani_msg_t * yutani_msg_build_window_focus_change(yutani_wid_t wid, int focused);
yutani_msg_t * yutani_msg_build_window_mouse_event(yutani_wid_t wid, int32_t new_x, int32_t new_y, int32_t old_x, int32_t old_y, uint8_t buttons, uint8_t command);
yutani_msg_t * yutani_msg_build_window_resize(uint32_t type, yutani_wid_t wid, uint32_t width, uint32_t height, uint32_t bufid);
yutani_msg_t * yutani_msg_build_window_advertise(yutani_wid_t wid, uint32_t flags, uint16_t * offsets, size_t length, char * data);
yutani_msg_t * yutani_msg_build_subscribe(void);
yutani_msg_t * yutani_msg_build_unsubscribe(void);
yutani_msg_t * yutani_msg_build_query(void);
yutani_msg_t * yutani_msg_build_notify(void);
yutani_msg_t * yutani_msg_build_session_end(void);
yutani_msg_t * yutani_msg_build_window_focus(yutani_wid_t wid);
yutani_msg_t * yutani_msg_build_key_bind(kbd_key_t key, kbd_mod_t mod, int response);
yutani_msg_t * yutani_msg_build_window_drag_start(yutani_wid_t wid);
yutani_msg_t * yutani_msg_build_window_update_shape(yutani_wid_t wid, int set_shape);
yutani_msg_t * yutani_msg_build_window_warp_mouse(yutani_wid_t wid, int32_t x, int32_t y);
yutani_msg_t * yutani_msg_build_window_show_mouse(yutani_wid_t wid, int32_t show_mouse);
yutani_msg_t * yutani_msg_build_window_resize_start(yutani_wid_t wid, yutani_scale_direction_t direction);


int yutani_msg_send(yutani_t * y, yutani_msg_t * msg);
yutani_t * yutani_context_create(FILE * socket);
yutani_t * yutani_init(void);
yutani_window_t * yutani_window_create(yutani_t * y, int width, int height);
void yutani_flip(yutani_t * y, yutani_window_t * win);
void yutani_window_move(yutani_t * yctx, yutani_window_t * window, int x, int y);
void yutani_close(yutani_t * y, yutani_window_t * win);
void yutani_set_stack(yutani_t *, yutani_window_t *, int);
void yutani_flip_region(yutani_t *, yutani_window_t * win, int32_t x, int32_t y, int32_t width, int32_t height);
void yutani_window_resize(yutani_t * yctx, yutani_window_t * window, uint32_t width, uint32_t height);
void yutani_window_resize_offer(yutani_t * yctx, yutani_window_t * window, uint32_t width, uint32_t height);
void yutani_window_resize_accept(yutani_t * yctx, yutani_window_t * window, uint32_t width, uint32_t height);
void yutani_window_resize_done(yutani_t * yctx, yutani_window_t * window);
void yutani_window_advertise(yutani_t * yctx, yutani_window_t * window, char * name);
void yutani_window_advertise_icon(yutani_t * yctx, yutani_window_t * window, char * name, char * icon);
void yutani_subscribe_windows(yutani_t * y);
void yutani_unsubscribe_windows(yutani_t * y);
void yutani_query_windows(yutani_t * y);
void yutani_session_end(yutani_t * y);
void yutani_focus_window(yutani_t * y, yutani_wid_t wid);
void yutani_key_bind(yutani_t * yctx, kbd_key_t key, kbd_mod_t mod, int response);
void yutani_window_drag_start(yutani_t * yctx, yutani_window_t * window);
void yutani_window_update_shape(yutani_t * yctx, yutani_window_t * window, int set_shape);
void yutani_window_warp_mouse(yutani_t * yctx, yutani_window_t * window, int32_t x, int32_t y);
void yutani_window_show_mouse(yutani_t * yctx, yutani_window_t * window, int32_t show_mouse);
void yutani_window_resize_start(yutani_t * yctx, yutani_window_t * window, yutani_scale_direction_t direction);


gfx_context_t * init_graphics_yutani(yutani_window_t * window);
gfx_context_t *  init_graphics_yutani_double_buffer(yutani_window_t * window);
void reinit_graphics_yutani(gfx_context_t * out, yutani_window_t * window);

#endif /* _YUTANI_H */
