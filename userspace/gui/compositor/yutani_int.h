#ifndef _YUTANI_INTERNAL_H
#define _YUTANI_INTERNAL_H

#include <cairo.h>
#include "lib/yutani.h"
#include "lib/list.h"
#include "lib/hashmap.h"
#include "lib/graphics.h"
#include "lib/kbd.h"

#define MOUSE_SCALE 3
#define MOUSE_OFFSET_X 26
#define MOUSE_OFFSET_Y 26

#define YUTANI_BYTE_DEPTH 4

#define YUTANI_SCREENSHOT_FULL 1
#define YUTANI_SCREENSHOT_WINDOW 2

typedef enum {
	YUTANI_EFFECT_NONE,
	YUTANI_EFFECT_FADE_IN,
	YUTANI_EFFECT_FADE_OUT,
	YUTANI_EFFECT_MINIMIZE,
	YUTANI_EFFECT_UNMINIMIZE,
} yutani_effect;

static int yutani_animation_lengths[] = {
	0,
	200,
	200,
	0,
	0,
};

typedef struct {
	yutani_wid_t wid;

	signed long x;
	signed long y;
	unsigned short z;

	int32_t width;
	int32_t height;

	uint8_t * buffer;
	uint32_t bufid;

	uint32_t owner;

	int16_t  rotation;

	uint32_t newbufid;
	uint8_t * newbuffer;

	uint32_t client_flags;
	uint16_t client_offsets[5];
	uint32_t client_length;
	char *   client_strings;

	int anim_mode;
	uint32_t anim_start;

	int alpha_threshold;
	int show_mouse;

	int tiled;
	int32_t untiled_width;
	int32_t untiled_height;

	int default_mouse;
} yutani_server_window_t;

typedef struct {
	/* XXX multiple displays */
	unsigned int width;
	unsigned int height;

	cairo_surface_t * framebuffer_surface;
	cairo_surface_t * real_surface;
	cairo_t * framebuffer_ctx;
	cairo_t * real_ctx;

	void * backend_framebuffer;
	gfx_context_t * backend_ctx;

	signed int mouse_x;
	signed int mouse_y;

	signed int last_mouse_x;
	signed int last_mouse_y;

	list_t * windows;
	hashmap_t * wids_to_windows;

	yutani_server_window_t * bottom_z;
	list_t * mid_zs;
	yutani_server_window_t * top_z;

	list_t * update_list;
	volatile int update_list_lock;

	sprite_t mouse_sprite;

	char * server_ident;

	yutani_server_window_t * focused_window;
	FILE * server;

	int mouse_state;
	yutani_server_window_t * mouse_window;

	int mouse_win_x;
	int mouse_win_y;
	int mouse_init_x;
	int mouse_init_y;

	int mouse_drag_button;
	int mouse_moved;

	int32_t mouse_click_x;
	int32_t mouse_click_y;

	key_event_state_t kbd_state;

	yutani_server_window_t * resizing_window;
	int32_t resizing_w;
	int32_t resizing_h;

	list_t * window_subscribers;

	uint32_t start_time;

	volatile int redraw_lock;

	yutani_server_window_t * old_hover_window;

	hashmap_t * key_binds;

	list_t * windows_to_remove;

	yutani_t * host_context;
	yutani_window_t * host_window;

	hashmap_t * clients_to_windows;

	int debug_bounds;
	int debug_shapes;

	int screenshot_frame;

	uint32_t start_subtime;

	yutani_scale_direction_t resizing_direction;
	int32_t resizing_offset_x;
	int32_t resizing_offset_y;
	int resizing_button;

	sprite_t mouse_sprite_drag;
	sprite_t mouse_sprite_resize_v;
	sprite_t mouse_sprite_resize_h;
	sprite_t mouse_sprite_resize_da;
	sprite_t mouse_sprite_resize_db;

	int current_cursor;

} yutani_globals_t;

struct key_bind {
	unsigned int owner;
	int response;
};

static void mark_window(yutani_globals_t * yg, yutani_server_window_t * window);
static void window_actually_close(yutani_globals_t * yg, yutani_server_window_t * w);
static void notify_subscribers(yutani_globals_t * yg);

#endif /* _YUTANI_INTERNAL_H */
