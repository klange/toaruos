#ifndef _YUTANI_H
#define _YUTANI_H

#include <stdio.h>
#include <stdint.h>

#include "hashmap.h"
#include "graphics.h"
#include "kbd.h"
#include "mouse.h"

#define YUTANI_SERVER_IDENTIFIER "sys.compositor"
#define YUTANI_SHMKEY(buf,sz,win) snprintf(buf, sz, "%s.%d", YUTANI_SERVER_IDENTIFIER, win->bufid);

typedef unsigned int yutani_wid_t;

typedef struct yutani_context {
	FILE * sock;

	/* XXX list of displays? */
	/* XXX display struct with more information? */
	size_t display_width;
	size_t display_height;

	hashmap_t * windows;
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

struct yutani_msg_mouse_event {
	yutani_wid_t wid;
	mouse_device_packet_t event;
};

typedef struct yutani_window {
	yutani_wid_t wid;

	uint32_t width;
	uint32_t height;

	uint8_t * buffer;
	uint32_t bufid;/* We occasionally replace the buffer; each is uniquely-indexed */

	uint8_t focused;
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
#define YUTANI_MSG_GOODBYE             0x000000F0

/* Server responses */
#define YUTANI_MSG_WELCOME             0x00010001
#define YUTANI_MSG_WINDOW_INIT         0x00010002

#define YUTANI_ZORDER_MAX    0xFFFF
#define YUTANI_ZORDER_TOP    0xFFFF
#define YUTANI_ZORDER_BOTTOM 0x0000

#define YUTANI_MOUSE_BUTTON_LEFT   0x01
#define YUTANI_MOUSE_BUTTON_RIGHT  0x02
#define YUTANI_MOUSE_BUTTON_MIDDLE 0x04

#define YUTANI_MOUSE_STATE_NORMAL     0
#define YUTANI_MOUSE_STATE_MOVING     1
#define YUTANI_MOUSE_STATE_DRAGGING   2
#define YUTANI_MOUSE_STATE_RESIZING   3

typedef struct {
	int x;
	int y;
	unsigned int width;
	unsigned int height;
} yutani_damage_rect_t;

yutani_msg_t * yutani_wait_for(yutani_t * y, uint32_t type);
yutani_msg_t * yutani_poll(yutani_t * y);

yutani_msg_t * yutani_msg_build_hello(void);
yutani_msg_t * yutani_msg_build_welcome(uint32_t width, uint32_t height);
yutani_msg_t * yutani_msg_build_window_new(uint32_t width, uint32_t height);
yutani_msg_t * yutani_msg_build_window_init(yutani_wid_t wid, uint32_t width, uint32_t height, uint32_t bufid);
yutani_msg_t * yutani_msg_build_flip(yutani_wid_t);
yutani_msg_t * yutani_msg_build_key_event(yutani_wid_t wid, key_event_t * event, key_event_state_t * state);
yutani_msg_t * yutani_msg_build_mouse_event(yutani_wid_t wid, mouse_device_packet_t * event);
yutani_msg_t * yutani_msg_build_window_close(yutani_wid_t wid);
yutani_msg_t * yutani_msg_build_window_stack(yutani_wid_t wid, int z);
yutani_msg_t * yutani_msg_build_window_focus_change(yutani_wid_t wid, int focused);

int yutani_msg_send(yutani_t * y, yutani_msg_t * msg);
yutani_t * yutani_context_create(FILE * socket);
yutani_t * yutani_init(void);
yutani_window_t * yutani_window_create(yutani_t * y, int width, int height);
void yutani_flip(yutani_t * y, yutani_window_t * win);
void yutani_window_move(yutani_t * yctx, yutani_window_t * window, int x, int y);
void yutani_close(yutani_t * y, yutani_window_t * win);
void yutani_set_stack(yutani_t *, yutani_window_t *, int);

gfx_context_t * init_graphics_yutani(yutani_window_t * window);
gfx_context_t *  init_graphics_yutani_double_buffer(yutani_window_t * window);
void reinit_graphics_yutani(gfx_context_t * out, yutani_window_t * window);

#endif /* _YUTANI_H */
