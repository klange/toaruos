#ifndef _YUTANI_INTERNAL_H
#define _YUTANI_INTERNAL_H

#include <cairo.h>
#include "lib/yutani.h"
#include "lib/list.h"
#include "lib/hashmap.h"
#include "lib/graphics.h"

#define MOUSE_SCALE 2
#define MOUSE_OFFSET_X 26
#define MOUSE_OFFSET_Y 26

#define YUTANI_BYTE_DEPTH 4

typedef enum {
	YUTANI_EFFECT_NONE,
	YUTANI_EFFECT_FADE_IN,
	YUTANI_EFFECT_FADE_OUT,
	YUTANI_EFFECT_MINIMIZE,
	YUTANI_EFFECT_UNMINIMIZE,
} yutani_effect;

typedef struct {
	yutani_wid_t wid;

	signed long x;
	signed long y;
	unsigned short z;

	uint32_t width;
	uint32_t height;

	uint8_t * buffer;
	uint32_t bufid;

	uint32_t owner;
} yutani_server_window_t;

typedef struct {
	/* XXX multiple displays */
	unsigned int width;
	unsigned int height;

	cairo_surface_t * framebuffer_surface;
	cairo_surface_t * selectbuffer_surface;
	cairo_t * framebuffer_ctx;
	cairo_t * selectbuffer_ctx;

	void * select_framebuffer;
	void * backend_framebuffer;
	gfx_context_t * backend_ctx;

	signed int mouse_x;
	signed int mouse_y;

	signed int last_mouse_x;
	signed int last_mouse_y;

	list_t * windows;
	hashmap_t * wids_to_windows;
	yutani_server_window_t * zlist[0x10000];

	list_t * update_list;

	sprite_t mouse_sprite;
} yutani_globals_t;


#endif /* _YUTANI_INTERNAL_H */
