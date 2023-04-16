/**
 * @brief Internal definitions used by the Yutani compositor.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2021 K. Lange
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

	/* Dialog animations, faster than the fades */
	YUTANI_EFFECT_SQUEEZE_IN,
	YUTANI_EFFECT_SQUEEZE_OUT,

	YUTANI_EFFECT_DISAPPEAR,
} yutani_effect;

/* Animation lengths */
static int yutani_animation_lengths[] = {
	0,   /* None */
	200, /* Fade In */
	200, /* Fade Out */
	200,   /* Minimize */
	200,   /* Unminimized */
	100, /* Squeeze in */
	100, /* Squeeze out */
	10,  /* Disappear */
};

static int yutani_is_closing_animation[] = {
	0,
	0,
	1,
	0,
	0,
	0,
	1,
	1,
};

static int yutani_is_minimizing_animation[] = {
	0,
	0,
	0,
	1,
	0,
	0,
	0,
	0
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
	uintptr_t owner;

	/* Rotation of windows XXX */
	int16_t  rotation;

	/* Client advertisements */
	uint32_t client_flags;
	uint32_t client_icon;
	uint32_t client_length;
	char *   client_strings;

	/* Window animations */
	uint64_t anim_mode;
	uint64_t anim_start;

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

	/* Window is hidden? */
	int hidden;
	int minimized;

	int32_t icon_x, icon_y, icon_w, icon_h;
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
	list_t * menu_zs;
	list_t * overlay_zs;
	yutani_server_window_t * top_z;

	/* Damage region list */
	list_t * update_list;

	/* Mouse cursors */
	sprite_t mouse_sprite;
	sprite_t mouse_sprite_drag;
	sprite_t mouse_sprite_resize_v;
	sprite_t mouse_sprite_resize_h;
	sprite_t mouse_sprite_resize_da;
	sprite_t mouse_sprite_resize_db;
	sprite_t mouse_sprite_point;
	sprite_t mouse_sprite_ibeam;
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

	/* Pointer to last hovered window to allow exit events */
	yutani_server_window_t * old_hover_window;

	/* Key bindings */
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
	uint8_t active_modifiers;

	uint64_t resize_release_time;
	int32_t resizing_init_w;
	int32_t resizing_init_h;

	list_t * windows_to_minimize;
	list_t * minimized_zs;
} yutani_globals_t;

struct key_bind {
	uintptr_t owner;
	int response;
};

/* Exported functions for plugins */
extern int yutani_window_is_top(yutani_globals_t * yg, yutani_server_window_t * window);
extern int yutani_window_is_bottom(yutani_globals_t * yg, yutani_server_window_t * window);
extern uint64_t yutani_time_since(yutani_globals_t * yg, uint64_t start_time);
extern void yutani_window_to_device(yutani_server_window_t * window, int32_t x, int32_t y, int32_t * out_x, int32_t * out_y);
extern void yutani_device_to_window(yutani_server_window_t * window, int32_t x, int32_t y, int32_t * out_x, int32_t * out_y);
extern uint32_t yutani_color_for_wid(yutani_wid_t wid);

_End_C_Header
