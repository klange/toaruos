/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2018 K. Lange
 *
 * Internal definitions used by the Yutani compositor
 * and extension plugins.
 */
#pragma once

#include <_cheader.h>
#include <stdint.h>
#include <sys/time.h>
#include <toaru/yutani.h>

_Begin_C_Header

/* Mouse resolution scaling */
#define MOUSE_SCALE 3
#define YUTANI_INCOMING_MOUSE_SCALE * 3

/* Mouse cursor hotspot */
#define MOUSE_OFFSET_X 26
#define MOUSE_OFFSET_Y 26

/* Mouse cursor size */
#define MOUSE_WIDTH 64
#define MOUSE_HEIGHT 64

/* How much the mouse needs to move to break off a tiled window */
#define UNTILE_SENSITIVITY (MOUSE_SCALE * 5)

/* Screenshot modes */
#define YUTANI_SCREENSHOT_FULL 1
#define YUTANI_SCREENSHOT_WINDOW 2

/*
 * Animation effect types.
 * XXX: Should this be in the client library?
 */
typedef enum {
	YUTANI_EFFECT_NONE,

	/* Basic animations */
	YUTANI_EFFECT_FADE_IN,
	YUTANI_EFFECT_FADE_OUT,

	/* XXX: Are these used? */
	YUTANI_EFFECT_MINIMIZE,
	YUTANI_EFFECT_UNMINIMIZE,
} yutani_effect;

/* Animation lengths */
static int yutani_animation_lengths[] = {
	0,   /* None */
	200, /* Fade In */
	200, /* Fade Out */
	0,   /* Minimize */
	0,   /* Unminimized */
};

/* Debug Options */
#define YUTANI_DEBUG_WINDOW_BOUNDS 1
#define YUTANI_DEBUG_WINDOW_SHAPES 1

/* Command line flag values */
struct {
	int nested;
	int nest_width;
	int nest_height;
} yutani_options = {
	.nested = 0,
	.nest_width = 640,
	.nest_height = 480,
};

/*
 * Server window definitions
 */
typedef struct YutaniServerWindow {
	/* Window identifier number */
	yutani_wid_t wid;

	/* Window location */
	signed long x;
	signed long y;

	/* Stack order */
	unsigned short z;

	/* Window size */
	int32_t width;
	int32_t height;

	/* Canvas buffer */
	uint8_t * buffer;
	uint32_t bufid;
	uint32_t newbufid;
	uint8_t * newbuffer;

	/* Connection that owns this window */
	uint32_t owner;

	/* Rotation of windows XXX */
	int16_t  rotation;

	/* Client advertisements */
	uint32_t client_flags;
	uint16_t client_offsets[5];
	uint32_t client_length;
	char *   client_strings;

	/* Window animations */
	int anim_mode;
	uint32_t anim_start;

	/* Alpha shaping threshold */
	int alpha_threshold;

	/*
	 * Mouse cursor selection
	 * Originally, this specified whether the mouse was
	 * hidden, but it plays double duty since client
	 * control over mouse cursors was added.
	 */
	int show_mouse;
	int default_mouse;

	/* Tiling / untiling information */
	int tiled;
	int32_t untiled_width;
	int32_t untiled_height;
	int32_t untiled_left;
	int32_t untiled_top;

	/* Client-configurable server behavior flags */
	uint32_t server_flags;

	/* Window opacity */
	int opacity;
} yutani_server_window_t;

typedef struct YutaniGlobals {
	/* Display resolution */
	unsigned int width;
	unsigned int height;
	uint32_t stride;

	/* TODO: What about multiple screens?
	 *
	 * Obviously this is the whole canvas size,
	 * but we need to be able to track different
	 * monitors if/when we ever get support for that.
	 */

	/* Core graphics context */
	void * backend_framebuffer;
	gfx_context_t * backend_ctx;

	/* Mouse location */
	signed int mouse_x;
	signed int mouse_y;

	/*
	 * Previous mouse location, so that events can have
	 * both the new and old mouse location together
	 */
	signed int last_mouse_x;
	signed int last_mouse_y;

	/* List of all windows */
	list_t * windows;

	/* Hash of window IDs to their objects */
	hashmap_t * wids_to_windows;

	/*
	 * Window stacking information
	 * TODO: Support multiple top and bottom windows.
	 */
	yutani_server_window_t * bottom_z;
	list_t * mid_zs;
	yutani_server_window_t * top_z;

	/* Damage region list */
	list_t * update_list;
	volatile int update_list_lock;

	/* Mouse cursors */
	sprite_t mouse_sprite;
	sprite_t mouse_sprite_drag;
	sprite_t mouse_sprite_resize_v;
	sprite_t mouse_sprite_resize_h;
	sprite_t mouse_sprite_resize_da;
	sprite_t mouse_sprite_resize_db;
	int current_cursor;

	/* Server backend communication identifier */
	char * server_ident;
	FILE * server;

	/* Pointer to focused window */
	yutani_server_window_t * focused_window;

	/* Mouse movement state */
	int mouse_state;

	/* Pointer to window being manipulated by mouse actions */
	yutani_server_window_t * mouse_window;

	/* Buffered information on mouse-moved window */
	int mouse_win_x;
	int mouse_win_y;
	int mouse_init_x;
	int mouse_init_y;
	int mouse_init_r;

	int32_t mouse_click_x_orig;
	int32_t mouse_click_y_orig;

	int mouse_drag_button;
	int mouse_moved;

	int32_t mouse_click_x;
	int32_t mouse_click_y;

	/* Keyboard library state machine state */
	key_event_state_t kbd_state;

	/* Pointer to window being resized */
	yutani_server_window_t * resizing_window;
	int32_t resizing_w;
	int32_t resizing_h;
	yutani_scale_direction_t resizing_direction;
	int32_t resizing_offset_x;
	int32_t resizing_offset_y;
	int resizing_button;

	/* List of clients subscribing to window information events */
	list_t * window_subscribers;

	/* When the server started, used for timing functions */
	time_t start_time;
	suseconds_t start_subtime;

	/* Basic lock to prevent redraw thread and communication thread interference */
	volatile int redraw_lock;

	/* Pointer to last hovered window to allow exit events */
	yutani_server_window_t * old_hover_window;

	/* Key bindigns */
	hashmap_t * key_binds;

	/* Windows to remove after the end of the rendering pass */
	list_t * windows_to_remove;

	/* For nested mode, the host Yutani context and window */
	yutani_t * host_context;
	yutani_window_t * host_window;

	/* Map of clients to their windows */
	hashmap_t * clients_to_windows;

	/* Toggles for debugging window locations */
	int debug_bounds;
	int debug_shapes;

	/* If the next rendered frame should be saved as a screenshot */
	int screenshot_frame;

	/* Next frame should resize host context */
	int resize_on_next;

	/* Last mouse buttons - used for some specialized mouse drivers */
	uint32_t last_mouse_buttons;

	/* Clipboard buffer */
	char clipboard[512];
	int clipboard_size;

	/* VirtualBox Seamless mode support information */
	int vbox_rects;
	int vbox_pointer;

	/* Renderer plugin context */
	void * renderer_ctx;

	int reload_renderer;
} yutani_globals_t;

struct key_bind {
	unsigned int owner;
	int response;
};

/* Exported functions for plugins */
extern int yutani_window_is_top(yutani_globals_t * yg, yutani_server_window_t * window);
extern int yutani_window_is_bottom(yutani_globals_t * yg, yutani_server_window_t * window);
extern uint32_t yutani_time_since(yutani_globals_t * yg, uint32_t start_time);
extern void yutani_window_to_device(yutani_server_window_t * window, int32_t x, int32_t y, int32_t * out_x, int32_t * out_y);
extern void yutani_device_to_window(yutani_server_window_t * window, int32_t x, int32_t y, int32_t * out_x, int32_t * out_y);
extern uint32_t yutani_color_for_wid(yutani_wid_t wid);

_End_C_Header
