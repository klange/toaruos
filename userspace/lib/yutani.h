#ifndef _YUTANI_H
#define _YUTANI_H

#include <stdio.h>
#include <stdint.h>

#include "graphics.h"

#define YUTANI_SERVER_IDENTIFIER "sys.compositor"
#define YUTANI_SHMKEY(buf,sz,win) snprintf(buf, sz, "%s.%d", YUTANI_SERVER_IDENTIFIER, win->bufid);

typedef unsigned int yutani_wid_t;

typedef struct yutani_context {
	FILE * sock;

	/* XXX list of displays? */
	/* XXX display struct with more information? */
	size_t display_width;
	size_t display_height;
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

struct yutani_msg_window_new {
	uint32_t width;
	uint32_t height;
};

struct yutani_msg_window_init {
	uint32_t width;
	uint32_t height;
	uint32_t bufid;
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
#define YUTANI_MSG__MAGIC       0xABAD1DEA

/* Client messages */
#define YUTANI_MSG_HELLO        0x00000001
#define YUTANI_MSG_WINDOW_NEW   0x00000002
#define YUTANI_MSG_FLIP         0x00000003

/* Server responses */
#define YUTANI_MSG_WELCOME      0x00010001
#define YUTANI_MSG_WINDOW_INIT  0x00010002

/* Device messages */
#define YUTANI_MSG_KEY_IN       0x00020001
#define YUTANI_MSG_MOUSE_IN     0x00020002

yutani_msg_t * yutani_wait_for(yutani_t * y, uint32_t type);

yutani_msg_t * yutani_msg_build_hello(void);
yutani_msg_t * yutani_msg_build_welcome(uint32_t width, uint32_t height);
yutani_msg_t * yutani_msg_build_window_new(uint32_t width, uint32_t height);
yutani_msg_t * yutani_msg_build_window_init(uint32_t width, uint32_t height, uint32_t bufid);
yutani_msg_t * yutani_msg_build_flip(void);

int yutani_msg_send(yutani_t * y, yutani_msg_t * msg);
yutani_t * yutani_context_create(FILE * socket);
yutani_t * yutani_init(void);
yutani_window_t * yutani_window_create(yutani_t * y, int width, int height);
void yutani_flip(yutani_t * y);
 
gfx_context_t * init_graphics_yutani(yutani_window_t * window);
gfx_context_t *  init_graphics_yutani_double_buffer(yutani_window_t * window);

#endif /* _YUTANI_H */
