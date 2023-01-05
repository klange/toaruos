/**
 * @brief Yutani - The ToaruOS Window Compositor.
 *
 * Yutani is a canvas-based window compositor and manager.
 * It employs shared memory to provide clients access to
 * canvases in which they may render, while using a packet-based
 * socket interface to communicate actions between the server
 * and client such as keyboard activity, mouse movement, responses
 * to client events, etc., as well as to communicate requests from
 * the client to the server, such as creation of new windows,
 * movement, resizing, and display updates.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2018 K. Lange
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include <assert.h>
#include <getopt.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/fswait.h>
#include <sys/sysfunc.h>
#include <sys/shm.h>
#include <pthread.h>
#include <dlfcn.h>

#include <toaru/graphics.h>
#include <toaru/mouse.h>
#include <toaru/kbd.h>
#include <toaru/pex.h>
#include <toaru/yutani.h>
#include <toaru/yutani-internal.h>
#include <toaru/yutani-server.h>
#include <toaru/hashmap.h>
#include <toaru/list.h>

#define _DEBUG_YUTANI
#ifdef _DEBUG_YUTANI
#include <toaru/trace.h>
#define TRACE_APP_NAME "yutani"
#else
#define TRACE(msg,...)
#endif

/* Early definitions */
static void mark_window(yutani_globals_t * yg, yutani_server_window_t * window);
static void window_actually_close(yutani_globals_t * yg, yutani_server_window_t * w);
static void notify_subscribers(yutani_globals_t * yg);
static void mouse_stop_drag(yutani_globals_t * yg);
static void window_move(yutani_globals_t * yg, yutani_server_window_t * window, int x, int y);
static yutani_server_window_t * top_at(yutani_globals_t * yg, uint16_t x, uint16_t y);

#define ENABLE_BLUR_BEHIND
#ifdef ENABLE_BLUR_BEHIND
#define BLUR_CLIP_MAX 20
#define BLUR_KERNEL   10
static char * blur_texture = NULL;
static gfx_context_t * blur_ctx = NULL;
static gfx_context_t * clip_ctx = NULL;
#endif

/**
 * Print usage information.
 */
static int usage(char * argv[]) {
	fprintf(stderr,
			"Yutani - Window Compositor\n"
			"\n"
			"usage: %s [-n [-g WxH]] [-h]\n"
			"\n"
			" -n --nested     \033[3mRun in a window.\033[0m\n"
			" -h --help       \033[3mShow this help message.\033[0m\n"
			" -g --geometry   \033[3mSet the size of the server framebuffer.\033[0m\n"
			"\n"
			"  Yutani is the standard system compositor.\n"
			"\n",
			argv[0]);
	return 1;
}

/**
 * Parse arguments
 */
static int parse_args(int argc, char * argv[], int * out) {
	static struct option long_opts[] = {
		{"nested",     no_argument,       0, 'n'},
		{"geometry",   required_argument, 0, 'g'},
		{"help",       no_argument,       0, 'h'},
		{0,0,0,0}
	};

	int index, c;
	while ((c = getopt_long(argc, argv, "hg:n", long_opts, &index)) != -1) {
		if (!c) {
			if (long_opts[index].flag == 0) {
				c = long_opts[index].val;
			}
		}
		switch (c) {
			case 'h':
				return usage(argv);
			case 'n':
				yutani_options.nested = 1;
				break;
			case 'g':
				{
					char * c = strstr(optarg, "x");
					if (c) {
						*c = '\0';
						c++;
						yutani_options.nest_width  = atoi(optarg);
						yutani_options.nest_height = atoi(c);
					}
				}
				break;
			default:
				fprintf(stderr, "Unrecognized option: %c\n", c);
				break;
		}
	}
	*out = optind;
	return 0;
}

static int32_t min(int32_t a, int32_t b) {
	return (a < b) ? a : b;
}

static int32_t max(int32_t a, int32_t b) {
	return (a > b) ? a : b;
}

static int next_buf_id(void) {
	static int _next = 1;
	return _next++;
}

static int next_wid(void) {
	static int _next = 1;
	return _next++;
}

uint64_t yutani_current_time(yutani_globals_t * yg) {
	struct timeval t;
	gettimeofday(&t, NULL);

	time_t sec_diff = t.tv_sec - yg->start_time;
	suseconds_t usec_diff = t.tv_usec - yg->start_subtime;

	if (t.tv_usec < yg->start_subtime) {
		sec_diff -= 1;
		usec_diff = (1000000 + t.tv_usec) - yg->start_subtime;
	}

	return (uint64_t)(sec_diff * 1000 + usec_diff / 1000);
}

uint64_t yutani_time_since(yutani_globals_t * yg, uint64_t start_time) {

	uint64_t now = yutani_current_time(yg);
	uint64_t diff = now - start_time; /* Milliseconds */

	return diff;
}

/**
 * Translate and transform coordinate from screen-relative to window-relative.
 */
void yutani_device_to_window(yutani_server_window_t * window, int32_t x, int32_t y, int32_t * out_x, int32_t * out_y) {
	if (!window) {
		*out_x = 0;
		*out_y = 0;
		return;
	}
	*out_x = x - window->x;
	*out_y = y - window->y;

	if (!window->rotation) return;

	double t_x = (double)*out_x - ((double)window->width / 2);
	double t_y = (double)*out_y - ((double)window->height / 2);

	double s = sin(-M_PI * (window->rotation/ 180.0));
	double c = cos(-M_PI * (window->rotation/ 180.0));

	double n_x = t_x * c - t_y * s;
	double n_y = t_x * s + t_y * c;

	*out_x = (int32_t)(n_x + ((double)window->width / 2));
	*out_y = (int32_t)(n_y + ((double)window->height / 2));
}

/**
 * Translate and transform coordinate from window-relative to screen-relative.
 */
void yutani_window_to_device(yutani_server_window_t * window, int32_t x, int32_t y, int32_t * out_x, int32_t * out_y) {

	if (!window->rotation) {
		*out_x = window->x + x;
		*out_y = window->y + y;
		return;
	}

	double t_x = (double)x - ((double)window->width / 2);
	double t_y = (double)y - ((double)window->height / 2);

	double s = sin((double)window->rotation * M_PI / 180.0);
	double c = cos((double)window->rotation * M_PI / 180.0);

	double n_x = t_x * c - t_y * s;
	double n_y = t_x * s + t_y * c;

	*out_x = (int32_t)(n_x + ((double)window->width / 2)  + (double)window->x);
	*out_y = (int32_t)(n_y + ((double)window->height / 2) + (double)window->y);
}

static list_t * window_zorder_owner(yutani_globals_t * yg, unsigned short index) {
	switch (index) {
		case YUTANI_ZORDER_BOTTOM:
		case YUTANI_ZORDER_TOP:
			return NULL;
		case YUTANI_ZORDER_MENU:
			return yg->menu_zs;
		case YUTANI_ZORDER_OVERLAY:
			return yg->overlay_zs;
		default:
			return yg->mid_zs;
	}
}

/**
 * Remove a window from the z stack.
 */
static void unorder_window(yutani_globals_t * yg, yutani_server_window_t * w) {
	unsigned short index = w->z;
	w->z = -1;
	if (index == YUTANI_ZORDER_BOTTOM && yg->bottom_z == w) {
		yg->bottom_z = NULL;
		return;
	}
	if (index == YUTANI_ZORDER_TOP && yg->top_z == w) {
		yg->top_z = NULL;
		return;
	}

	list_t * zorder_owner = window_zorder_owner(yg, index);
	node_t * n = list_find(zorder_owner, w);
	if (!n) return;
	list_delete(zorder_owner, n);
	free(n);
}

/**
 * Move a window to a new stack order.
 */
static void reorder_window(yutani_globals_t * yg, yutani_server_window_t * window, uint16_t new_zed) {
	if (!window) {
		return;
	}

	unorder_window(yg, window);

	window->z = new_zed;

	list_t * zorder_owner = window_zorder_owner(yg, new_zed);
	if (zorder_owner) {
		list_insert(zorder_owner, window);
		return;
	}

	if (new_zed == YUTANI_ZORDER_TOP) {
		if (yg->top_z) {
			unorder_window(yg, yg->top_z);
		}
		yg->top_z = window;
		return;
	}

	if (new_zed == YUTANI_ZORDER_BOTTOM) {
		if (yg->bottom_z) {
			unorder_window(yg, yg->bottom_z);
		}
		yg->bottom_z = window;
		return;
	}
}

/**
 * Move a window to the top of if its z stack.
 */
static void make_top(yutani_globals_t * yg, yutani_server_window_t * w) {
	unsigned short index = w->z;
	list_t * zorder_owner = window_zorder_owner(yg, index);
	if (!zorder_owner) return;

	node_t * n = list_find(zorder_owner, w);
	if (!n) return; /* wat */

	list_delete(zorder_owner, n);
	list_append(zorder_owner, n);
}

/**
 * Set a window as the focused window.
 *
 * Currently, we only support one focused window.
 * In the future, we should support multiple windows as "focused" to account
 * for multiple "seats" on a single display.
 */
static void set_focused_window(yutani_globals_t * yg, yutani_server_window_t * w) {
	if (w == yg->focused_window) {
		return; /* Already focused */
	}

	if (yg->focused_window) {
		/* Send focus change to old focused window */
		yutani_msg_buildx_window_focus_change_alloc(response);
		yutani_msg_buildx_window_focus_change(response, yg->focused_window->wid, 0);
		pex_send(yg->server, yg->focused_window->owner, response->size, (char *)response);
	}
	yg->focused_window = w;
	if (w) {
		/* Send focus change to new focused window */
		yutani_msg_buildx_window_focus_change_alloc(response);
		yutani_msg_buildx_window_focus_change(response, w->wid, 1);
		pex_send(yg->server, w->owner, response->size, (char *)response);
		make_top(yg, w);
		mark_window(yg, w);
	} else {
		/*
		 * There is no window to focus (we're unsetting focus);
		 * default to the bottom window (background)
		 */
		yg->focused_window = yg->bottom_z;
	}

	/* Notify all subscribers of window changes */
	notify_subscribers(yg);
}

/**
 * Get the focused window.
 *
 * In case there is no focused window, we return the bottom window.
 */
static yutani_server_window_t * get_focused(yutani_globals_t * yg) {
	if (yg->focused_window) return yg->focused_window;
	return yg->bottom_z;
}

static int yutani_pick_animation(uint32_t flags, int direction) {
	if (flags & YUTANI_WINDOW_FLAG_DIALOG_ANIMATION) {
		return (direction == 0) ? YUTANI_EFFECT_SQUEEZE_IN : YUTANI_EFFECT_SQUEEZE_OUT;
	}

	if (flags & YUTANI_WINDOW_FLAG_NO_ANIMATION) {
		return (direction == 0) ? YUTANI_EFFECT_NONE : YUTANI_EFFECT_DISAPPEAR;
	}

	return (direction == 0) ? YUTANI_EFFECT_FADE_IN : YUTANI_EFFECT_FADE_OUT;
}


/**
 * Create a server window object.
 *
 * Initializes a window of the particular size for a given client.
 */
static yutani_server_window_t * server_window_create(yutani_globals_t * yg, int width, int height, uintptr_t owner, uint32_t flags) {
	yutani_server_window_t * win = malloc(sizeof(yutani_server_window_t));

	win->wid = next_wid();
	win->owner = owner;
	list_insert(yg->windows, win);
	hashmap_set(yg->wids_to_windows, (void*)(uintptr_t)win->wid, win);

	list_t * client_list = hashmap_get(yg->clients_to_windows, (void *)owner);
	list_insert(client_list, win);

	win->x = 0;
	win->y = 0;
	win->z = 1;
	win->width = width;
	win->height = height;
	win->bufid = next_buf_id();
	win->rotation = 0;
	win->newbufid = 0;
	win->client_flags   = 0;
	win->client_icon = 0;
	win->client_length  = 0;
	win->client_strings = NULL;
	win->anim_mode = 0;
	win->anim_start = 0;
	win->alpha_threshold = 0;
	win->show_mouse = 1;
	win->tiled = 0;
	win->untiled_width = 0;
	win->untiled_height = 0;
	win->default_mouse = 1;
	win->server_flags = flags;
	win->opacity = 255;
	win->hidden = 1;

	char key[1024];
	YUTANI_SHMKEY(yg->server_ident, key, 1024, win);

	size_t size = (width * height * 4);

	win->buffer = shm_obtain(key, &size);
	memset(win->buffer, 0, size);

	list_insert(yg->mid_zs, win);

	return win;
}

/**
 * Update the shape threshold for a window.
 *
 * A shaping threshold is a byte representing the minimum
 * required alpha for a window to be considered "solid".
 * Eg., a value of 0 says all windows are solid, while a value of
 * 1 requires a window to have at least some opacity to it,
 * and a value of 255 requires fully opaque pixels.
 *
 * Not actually stored as a byte, so a value over 255 can be used.
 * This results in a window that passes through all clicks.
 */
static void server_window_update_shape(yutani_globals_t * yg, yutani_server_window_t * window, int set) {
	window->alpha_threshold = set;
}

/**
 * Start resizing a window.
 *
 * Resizing a multi-stage process.
 * The client and server agree on a size and the server prepares a buffer.
 * The client then needs to accept the resize, fill the buffer, and then
 * inform the server that it is ready, at which point we'll swap the
 * buffer we are rendering from.
 */
static uint32_t server_window_resize(yutani_globals_t * yg, yutani_server_window_t * win, int width, int height) {
	/* A client has accepted our offer, let's make a buffer for them */
	if (win->newbufid) {
		/* Already in the middle of an accept/done, bail */
		return win->newbufid;
	}
	win->newbufid = next_buf_id();

	{
		char key[1024];
		YUTANI_SHMKEY_EXP(yg->server_ident, key, 1024, win->newbufid);

		size_t size = (width * height * 4);
		win->newbuffer = shm_obtain(key, &size);
	}

	return win->newbufid;
}

/**
 * Finish the resize process.
 *
 * We delete the unlink the old buffer and then swap the pointers
 * for the new buffer.
 */
static void server_window_resize_finish(yutani_globals_t * yg, yutani_server_window_t * win, int width, int height) {
	if (!win->newbufid) {
		return;
	}

	int oldbufid = win->bufid;

	mark_window(yg, win);

	if (yg->resizing_window == win) {
		yg->resize_release_time = 0;
		if (yg->mouse_state == YUTANI_MOUSE_STATE_NORMAL) {
			int32_t x, y;
			if (yg->resizing_window->rotation) {
				/* If the window is rotated, we need to move the center to be where the new center should be, but x/y are based on the unrotated upper left corner. */
				/* The center always moves by one-half the resize dimensions */
				int32_t center_x, center_y;
				yutani_server_window_t fake_window = {
					.width = yg->resizing_init_w,
					.height = yg->resizing_init_h,
					.x = yg->resizing_window->x,
					.y = yg->resizing_window->y,
					.rotation = yg->resizing_window->rotation,
				};
				yutani_window_to_device(&fake_window, yg->resizing_offset_x + yg->resizing_w / 2, yg->resizing_offset_y + yg->resizing_h / 2, &center_x, &center_y);
				x = center_x - yg->resizing_w / 2;
				y = center_y - yg->resizing_h / 2;
			} else {
				yutani_window_to_device(yg->resizing_window, yg->resizing_offset_x, yg->resizing_offset_y, &x, &y);
			}
			TRACE("resize complete, now %d x %d", yg->resizing_w, yg->resizing_h);
			window_move(yg, yg->resizing_window, x,y);
			yg->resizing_window = NULL;
			yg->mouse_window = NULL;
		}
	}

	win->width = width;
	win->height = height;

	win->bufid = win->newbufid;
	win->buffer = win->newbuffer;

	win->newbuffer = NULL;
	win->newbufid = 0;

	{
		char key[1024];
		YUTANI_SHMKEY_EXP(yg->server_ident, key, 1024, oldbufid);
		shm_release(key);
	}

	mark_window(yg, win);
}

/**
 * Mark a screen region as damaged.
 */
static void mark_screen(yutani_globals_t * yg, int32_t x, int32_t y, int32_t width, int32_t height) {
	yutani_damage_rect_t * rect = malloc(sizeof(yutani_damage_rect_t));

	rect->x = x;
	rect->y = y;
	rect->width = width;
	rect->height = height;

	list_insert(yg->update_list, rect);
}

/**
 * Draw the cursor sprite.
 */
static void draw_cursor(yutani_globals_t * yg, int x, int y, int cursor) {
	sprite_t * sprite = &yg->mouse_sprite;
	static sprite_t * previous = NULL;
	if (yg->resizing_window) {
		switch (yg->resizing_direction) {
			case SCALE_UP:
			case SCALE_DOWN:
				sprite = &yg->mouse_sprite_resize_v;
				break;
			case SCALE_LEFT:
			case SCALE_RIGHT:
				sprite = &yg->mouse_sprite_resize_h;
				break;
			case SCALE_DOWN_RIGHT:
			case SCALE_UP_LEFT:
				sprite = &yg->mouse_sprite_resize_da;
				break;
			case SCALE_DOWN_LEFT:
			case SCALE_UP_RIGHT:
				sprite = &yg->mouse_sprite_resize_db;
				break;
			default:
				break;
		}
	} else if (yg->mouse_state == YUTANI_MOUSE_STATE_MOVING) {
		sprite = &yg->mouse_sprite_drag;
	} else {
		switch (cursor) {
			case YUTANI_CURSOR_TYPE_DRAG:              sprite = &yg->mouse_sprite_drag; break;
			case YUTANI_CURSOR_TYPE_RESIZE_VERTICAL:   sprite = &yg->mouse_sprite_resize_v; break;
			case YUTANI_CURSOR_TYPE_RESIZE_HORIZONTAL: sprite = &yg->mouse_sprite_resize_h; break;
			case YUTANI_CURSOR_TYPE_RESIZE_UP_DOWN:    sprite = &yg->mouse_sprite_resize_da; break;
			case YUTANI_CURSOR_TYPE_RESIZE_DOWN_UP:    sprite = &yg->mouse_sprite_resize_db; break;
			case YUTANI_CURSOR_TYPE_POINT:             sprite = &yg->mouse_sprite_point; break;
			case YUTANI_CURSOR_TYPE_IBEAM:             sprite = &yg->mouse_sprite_ibeam; break;
		}
	}
	if (sprite != previous) {
		mark_screen(yg, x / MOUSE_SCALE - MOUSE_OFFSET_X, y / MOUSE_SCALE - MOUSE_OFFSET_Y, MOUSE_WIDTH, MOUSE_HEIGHT);
		previous = sprite;
	}

	if (yg->vbox_pointer > 0) {
		if (write(yg->vbox_pointer, sprite->bitmap, 48*48*4) > 0) {
			/* if that was successful, we don't need to draw the cursor */
			return;
		}
	}

	yutani_server_window_t * cursor_window = yg->resizing_window ? yg->resizing_window :
		top_at(yg, yg->mouse_x / MOUSE_SCALE, yg->mouse_y / MOUSE_SCALE);
	int16_t rotation = cursor_window ? cursor_window->rotation : 0;

	if (rotation) {
		draw_sprite_rotate(yg->backend_ctx, sprite, x / MOUSE_SCALE - MOUSE_OFFSET_X, y / MOUSE_SCALE - MOUSE_OFFSET_Y, (double)rotation * M_PI / 180.0, 1.0);
	} else {
		draw_sprite(yg->backend_ctx, sprite, x / MOUSE_SCALE - MOUSE_OFFSET_X, y / MOUSE_SCALE - MOUSE_OFFSET_Y);
	}
}

/**
 * Determine if a window has a solid pixel at a given screen-space coordinate.
 *
 * This is where we evaluate alpha thresholds. We only do this underneath
 * the cursor, and only when we move the cursor. It's reasonably fast
 * in those circumstances, but shouldn't be used for large regions. We
 * do have one debug method that indicates the top window in a box
 * around the cursor, but it is relatively slow.
 */
static yutani_server_window_t * check_top_at(yutani_globals_t * yg, yutani_server_window_t * w, uint16_t x, uint16_t y){
	if (!w) return NULL;
	if (w->hidden) return NULL;
	int32_t _x = -1, _y = -1;
	yutani_device_to_window(w, x, y, &_x, &_y);
	if (_x < 0 || _x >= w->width || _y < 0 || _y >= w->height) return NULL;
	uint32_t c = ((uint32_t *)w->buffer)[(w->width * _y + _x)];
	uint8_t a = _ALP(c);
	if (a >= w->alpha_threshold) {
		return w;
	}
	return NULL;
}

/**
 * Find the window that is at the top at a particular screen-space coordinate.
 *
 * This walks through each window from top to bottom (foreachr - reverse foreach)
 * until it finds one with a pixel at this coordinate. Again, we only call this
 * at the cursor coordinates, and it is not particularly fast, so don't use it
 * anywhere that needs to hit a lot of coordinates.
 */
static yutani_server_window_t * top_at(yutani_globals_t * yg, uint16_t x, uint16_t y) {
	if (check_top_at(yg, yg->top_z, x, y)) return yg->top_z;
	foreachr(node, yg->menu_zs) {
		yutani_server_window_t * w = node->value;
		if (check_top_at(yg, w, x, y)) return w;
	}
	foreachr(node, yg->overlay_zs) {
		yutani_server_window_t * w = node->value;
		if (check_top_at(yg, w, x, y)) return w;
	}
	foreachr(node, yg->mid_zs) {
		yutani_server_window_t * w = node->value;
		if (check_top_at(yg, w, x, y)) return w;
	}
	if (check_top_at(yg, yg->bottom_z, x, y)) return yg->bottom_z;
	return NULL;
}

/**
 * Get the window at a coordinate and focus it.
 *
 * See the docs for the proceeding functions, but added to this
 * focusing windows is also not particular fast as the reshuffle
 * is complicated.
 */
static void set_focused_at(yutani_globals_t * yg, int x, int y) {
	yutani_server_window_t * n_focused = top_at(yg, x, y);
	set_focused_window(yg, n_focused);
}

/*
 * Convenience functions for checking if a window is in the top/bottom stack.
 *
 * In the future, these single-item "stacks" will be replaced with dedicated stacks
 * so we can have multiple background windows and multiple panels / always-top windows.
 */
int yutani_window_is_top(yutani_globals_t * yg, yutani_server_window_t * window) {
	/* For now, just use simple z-order */
	return window->z == YUTANI_ZORDER_TOP;
}

int yutani_window_is_bottom(yutani_globals_t * yg, yutani_server_window_t * window) {
	/* For now, just use simple z-order */
	return window->z == YUTANI_ZORDER_BOTTOM;
}

/**
 * Get a color for a wid for debugging.
 *
 * Makes a pretty rainbow pattern.
 */
uint32_t yutani_color_for_wid(yutani_wid_t wid) {
	static uint32_t colors[] = {
		0xFF19aeff,
		0xFFff4141,
		0xFFffff3e,
		0xFFff6600,
		0xFF9ade00,
		0xFFd76cff,
		0xFF364e59,
		0xFF0084c8,
		0xFFdc0000,
		0xFFff9900,
		0xFF009100,
		0xFFba00ff,
		0xFFb88100,
		0xFF9eabb0
	};
	int i = wid % (sizeof(colors) / sizeof(uint32_t));
	return colors[i];
}

/**
 * Determine if a matrix has an identity transformation for its linear component.
 */
static inline int matrix_is_translation(gfx_matrix_t m) {
	return (m[0][0] == 1.0 && m[0][1] == 0.0 && m[1][0] == 0.0 && m[1][1] == 1.0);
}

/**
 * Blit a window to the framebuffer.
 *
 * Applies transformations (rotation, animations) and then renders
 * the window through alpha blitting.
 */
static int yutani_blit_window(yutani_globals_t * yg, yutani_server_window_t * window, int x, int y) {

	if (window->hidden) {
		return 0;
	}

	sprite_t _win_sprite;
	_win_sprite.width = window->width;
	_win_sprite.height = window->height;
	_win_sprite.bitmap = (uint32_t *)window->buffer;
	_win_sprite.masks = NULL;
	_win_sprite.blank = 0;
	_win_sprite.alpha = ALPHA_EMBEDDED;

	double opacity = (double)(window->opacity) / 255.0;

	if (window->rotation || window == yg->resizing_window || window->anim_mode || (window->server_flags & YUTANI_WINDOW_FLAG_BLUR_BEHIND)) {
		double m[2][3];

		gfx_matrix_identity(m);
		gfx_matrix_translate(m,x,y);

		if (window == yg->resizing_window) {
			if (window->rotation) {
				gfx_matrix_translate(m, yg->resizing_init_w / 2, yg->resizing_init_h / 2);
				gfx_matrix_rotate(m, (double)window->rotation * M_PI / 180.0);
				gfx_matrix_translate(m, -yg->resizing_init_w / 2, -yg->resizing_init_h / 2);
			}
			double x_scale = (double)yg->resizing_w / (double)yg->resizing_window->width;
			double y_scale = (double)yg->resizing_h / (double)yg->resizing_window->height;
			if (x_scale < 0.00001) {
				x_scale = 0.00001;
			}
			if (y_scale < 0.00001) {
				y_scale = 0.00001;
			}
			gfx_matrix_translate(m, (int)yg->resizing_offset_x, (int)yg->resizing_offset_y);
			gfx_matrix_scale(m, x_scale, y_scale);
		} else if (window->rotation) {
			gfx_matrix_translate(m, window->width / 2, window->height / 2);
			gfx_matrix_rotate(m, (double)window->rotation * M_PI / 180.0);
			gfx_matrix_translate(m, -window->width / 2, -window->height / 2);
		}


		if (window->anim_mode) {
			int frame = yutani_time_since(yg, window->anim_start);
			if (frame >= yutani_animation_lengths[window->anim_mode]) {
				/* XXX handle animation-end things like cleanup of closing windows */
				if (yutani_is_closing_animation[window->anim_mode]) {
					list_insert(yg->windows_to_remove, window);
					return 0;
				}
				window->anim_mode = 0;
				window->anim_start = 0;
			} else {
				switch (window->anim_mode) {
					case YUTANI_EFFECT_SQUEEZE_OUT:
					case YUTANI_EFFECT_FADE_OUT:
						{
							frame = yutani_animation_lengths[window->anim_mode] - frame;
						} /* fallthrough */
					case YUTANI_EFFECT_SQUEEZE_IN:
					case YUTANI_EFFECT_FADE_IN:
						{
							double time_diff = ((double)frame / (float)yutani_animation_lengths[window->anim_mode]);

							if (window->server_flags & YUTANI_WINDOW_FLAG_DIALOG_ANIMATION) {
								double x = time_diff;
								int t_y = (window->height * (1.0 -x)) / 2;
								gfx_matrix_translate(m, 0, t_y);
								gfx_matrix_scale(m, 1.0, x);
							} else {
								double x = 0.75 + time_diff * 0.25;
								opacity *= time_diff;
								if (!(window->server_flags & YUTANI_WINDOW_FLAG_ALT_ANIMATION)) {
									int t_x = (window->width * (1.0 - x)) / 2;
									int t_y = (window->height * (1.0 - x)) / 2;
									gfx_matrix_translate(m, t_x, t_y);
									gfx_matrix_scale(m, x, x);
								}
							}
						}
						break;
					default:
						break;
				}
			}
		}
#ifdef ENABLE_BLUR_BEHIND
		if (window->server_flags & YUTANI_WINDOW_FLAG_BLUR_BEHIND) {
			extern void draw_sprite_transform_blur(gfx_context_t * ctx, gfx_context_t * blur_ctx, const sprite_t * sprite, gfx_matrix_t matrix, float alpha, uint8_t threshold);
			draw_sprite_transform_blur(yg->backend_ctx, blur_ctx, &_win_sprite, m, opacity, window->alpha_threshold);
		} else
#endif
		if (matrix_is_translation(m)) {
			draw_sprite_alpha(yg->backend_ctx, &_win_sprite, m[0][2], m[1][2], opacity);
		} else {
			draw_sprite_transform(yg->backend_ctx, &_win_sprite, m, opacity);
		}
	} else if (window->opacity != 255) {
		draw_sprite_alpha(yg->backend_ctx, &_win_sprite, window->x, window->y, opacity);
	} else {
		draw_sprite(yg->backend_ctx, &_win_sprite, window->x, window->y);
	}

	return 0;
}

/**
 * VirtualBox Seamless desktop driver.
 *
 * Sends rectangles describing all the non-background windows
 * to the VirtualBox Guest Additions driver for use with the
 * seamless desktop mode.
 */
static void yutani_post_vbox_rects(yutani_globals_t * yg) {
	if (yg->vbox_rects <= 0) return;

	char tmp[4096];
	uint32_t * count = (uint32_t *)tmp;
	*count = 0;

	struct Rect {
		int32_t x;
		int32_t y;
		int32_t xe;
		int32_t ye;
	} __attribute__((packed));


	struct Rect * rects = (struct Rect *)(tmp+sizeof(int32_t));

#define DO_WINDOW(win) if (win && !win->hidden && *count < 255 ) { \
	rects->x = (win)->x; \
	rects->y = (win)->y; \
	rects->xe = (win)->x + (win)->width; \
	rects->ye = (win)->y + (win)->height; \
	rects++; \
	(*count)++; \
}

	/* Add top window if it exists */
	DO_WINDOW(yg->top_z);

	/* Add regular windows */
	foreach (node, yg->mid_zs) {
		yutani_server_window_t * w = node->value;
		DO_WINDOW(w);
	}

	/* Add overlay windows */
	foreach (node, yg->overlay_zs) {
		yutani_server_window_t * w = node->value;
		DO_WINDOW(w);
	}

	/* Add menu windows */
	foreach (node, yg->menu_zs) {
		yutani_server_window_t * w = node->value;
		DO_WINDOW(w);
	}

	/*
	 * If there were no windows, show the whole desktop
	 * so we can see, eg., the login screen.
	 */
	if (*count == 0) {
		*count = 1;
		rects->x = 0;
		rects->y = 0;
		rects->xe = yg->width;
		rects->ye = yg->height;
	}

	/* Post rectangle data to driver */
	write(yg->vbox_rects, tmp, sizeof(tmp));
}

/**
 * Blit all windows into the given context.
 *
 * This is called for rendering and for screenshots.
 */
static void yutani_blit_windows(yutani_globals_t * yg) {
	if (!yg->bottom_z || yg->bottom_z->anim_mode) {
		draw_fill(yg->backend_ctx, rgb(0,0,0));
	}
	if (yg->bottom_z) yutani_blit_window(yg, yg->bottom_z, yg->bottom_z->x, yg->bottom_z->y);
	foreach (node, yg->mid_zs) {
		yutani_server_window_t * w = node->value;
		if (w) yutani_blit_window(yg, w, w->x, w->y);
	}
	foreach (node, yg->overlay_zs) {
		yutani_server_window_t * w = node->value;
		if (w) yutani_blit_window(yg, w, w->x, w->y);
	}
	foreach (node, yg->menu_zs) {
		yutani_server_window_t * w = node->value;
		if (w) yutani_blit_window(yg, w, w->x, w->y);
	}
	if (yg->top_z) yutani_blit_window(yg, yg->top_z, yg->top_z->x, yg->top_z->y);
}

/**
 * Take a screenshot
 */
static void yutani_screenshot(yutani_globals_t * yg) {
	int task = yg->screenshot_frame;
	yg->screenshot_frame = 0;

	/* raw screenshots */

	char fname[1024];
	struct tm * timeinfo;
	struct timeval now;
	gettimeofday(&now, NULL);
	timeinfo = localtime((time_t *)&now.tv_sec);
	strftime(fname,1024,"/tmp/screenshot_%F_%H_%M_%S.tga",timeinfo);

	FILE * f = fopen(fname, "w");
	if (!f) {
		TRACE("Error opening output file for screenshot.");
		return;
	}

	uint32_t * buffer = NULL;
	int width, height;
	int alpha;

	if (task == YUTANI_SCREENSHOT_FULL) {
		buffer = (void *)yg->backend_ctx->backbuffer;
		width = yg->width;
		height = yg->height;
		alpha = 0;
	} else if (task == YUTANI_SCREENSHOT_WINDOW) {
		yutani_server_window_t * window = yg->focused_window;
		buffer = (void *)window->buffer;
		width = window->width;
		height = window->height;
		alpha = 1;
	}

	if (buffer) {

		struct {
			uint8_t id_length;
			uint8_t color_map_type;
			uint8_t image_type;

			uint16_t color_map_first_entry;
			uint16_t color_map_length;
			uint8_t color_map_entry_size;

			uint16_t x_origin;
			uint16_t y_origin;
			uint16_t width;
			uint16_t height;
			uint8_t  depth;
			uint8_t  descriptor;
		} __attribute__((packed)) header = {
			0, /* No image ID field */
			0, /* No color map */
			2, /* Uncompressed truecolor */
			0, 0, 0, /* No color map */
			0, 0, /* Don't care about origin */
			width, height, alpha ? 32 : 24,
			alpha ? 8 : 0,
		};
		fwrite(&header, 1, sizeof(header), f);

		for (int y = height-1; y>=0; y--) {
			for (int x = 0; x < width; ++x) {
				uint8_t buf[4] = {
					_BLU(buffer[y * width + x]),
					_GRE(buffer[y * width + x]),
					_RED(buffer[y * width + x]),
					_ALP(buffer[y * width + x]),
				};
				fwrite(buf, 1, alpha ? 4 : 3, f);
			}
		}
	}
	fclose(f);


	FILE * toast = fopen("/dev/pex/toast", "w");
	fprintf(toast, "{\"icon\": \"%s\", \"body\": \"Screenshot taken.\"}", fname);
	fclose(toast);

	/* Blorp */
	system("play /usr/share/ttk/blorp.wav &");
}

static gfx_context_t * init_graphics_with_store(gfx_context_t * base, char * store) {
	gfx_context_t * out = malloc(sizeof(gfx_context_t));
	out->clips = NULL;
	out->width = base->width;
	out->height = base->height;
	out->stride = base->stride;
	out->depth = base->depth;
	out->size = base->size;
	out->buffer = store;
	out->backbuffer = out->buffer;
	return out;
}

static void resize_display(yutani_globals_t * yg) {
	TRACE("Resizing display.");

	if (!yutani_options.nested) {
		reinit_graphics_fullscreen(yg->backend_ctx);
	} else {
		reinit_graphics_yutani(yg->backend_ctx, yg->host_window);
		yutani_window_resize_done(yg->host_context, yg->host_window);
	}

#ifdef ENABLE_BLUR_BEHIND
	free(blur_ctx);
	blur_texture = realloc(blur_texture, yg->backend_ctx->stride * yg->backend_ctx->height);
	blur_ctx = init_graphics_with_store(yg->backend_ctx, blur_texture);
	clip_ctx->width = yg->backend_ctx->width;
	clip_ctx->height = yg->backend_ctx->height;

	/* reinitialize extended clip context or we won't be drawing enough later... */
	if (clip_ctx->clips && clip_ctx->clips_size) {
		free(clip_ctx->clips);
		clip_ctx->clips_size = 0;
		clip_ctx->clips = NULL;
	}
#endif

	TRACE("graphics context resized...");
	yg->width = yg->backend_ctx->width;
	yg->height = yg->backend_ctx->height;
	yg->backend_framebuffer = yg->backend_ctx->backbuffer;

	TRACE("Marking...");
	yg->resize_on_next = 0;
	mark_screen(yg, 0, 0, yg->width, yg->height);

	TRACE("Sending welcome messages...");
	yutani_msg_buildx_welcome_alloc(response);
	yutani_msg_buildx_welcome(response, yg->width, yg->height);
	pex_broadcast(yg->server, response->size, (char *)response);
	TRACE("Done.");
}

/**
 * Redraw all windows, as well as the mouse cursor.
 *
 * This is the main redraw function.
 */
static void redraw_windows(yutani_globals_t * yg) {
	int has_updates = 0;

	/* We keep our own temporary mouse coordinates as they may change while we're drawing. */
	int tmp_mouse_x = yg->mouse_x;
	int tmp_mouse_y = yg->mouse_y;

	if (yg->resize_on_next) {
		resize_display(yg);
	}

	if (yg->resizing_window &&
		yg->mouse_state == YUTANI_MOUSE_STATE_NORMAL &&
		yg->resize_release_time &&
		yutani_time_since(yg, yg->resize_release_time) >= 500) {

		yutani_server_window_t * resizing = yg->resizing_window;
		mark_window(yg, resizing);
		yg->resize_release_time = 0;
		yg->resizing_window = NULL;
		yg->mouse_window = NULL;
		mark_window(yg, resizing);
	}

	gfx_clear_clip(yg->backend_ctx);
#ifdef ENABLE_BLUR_BEHIND
	gfx_clear_clip(clip_ctx);
#endif

	/* If the mouse has moved, that counts as two damage regions */
	if ((yg->last_mouse_x != tmp_mouse_x) || (yg->last_mouse_y != tmp_mouse_y)) {
		has_updates = 2;
		gfx_add_clip(yg->backend_ctx, yg->last_mouse_x / MOUSE_SCALE - MOUSE_OFFSET_X, yg->last_mouse_y / MOUSE_SCALE - MOUSE_OFFSET_Y, MOUSE_WIDTH, MOUSE_HEIGHT);
		gfx_add_clip(yg->backend_ctx, tmp_mouse_x / MOUSE_SCALE - MOUSE_OFFSET_X, tmp_mouse_y / MOUSE_SCALE - MOUSE_OFFSET_Y, MOUSE_WIDTH, MOUSE_HEIGHT);
#ifdef ENABLE_BLUR_BEHIND
		gfx_add_clip(clip_ctx, yg->last_mouse_x / MOUSE_SCALE - MOUSE_OFFSET_X - BLUR_CLIP_MAX, yg->last_mouse_y / MOUSE_SCALE - MOUSE_OFFSET_Y - BLUR_CLIP_MAX, MOUSE_WIDTH + BLUR_CLIP_MAX * 2, MOUSE_HEIGHT + BLUR_CLIP_MAX * 2);
		gfx_add_clip(clip_ctx, tmp_mouse_x / MOUSE_SCALE - MOUSE_OFFSET_X - BLUR_CLIP_MAX, tmp_mouse_y / MOUSE_SCALE - MOUSE_OFFSET_Y - BLUR_CLIP_MAX, MOUSE_WIDTH + BLUR_CLIP_MAX * 2, MOUSE_HEIGHT + BLUR_CLIP_MAX * 2);
#endif
	}

	yg->last_mouse_x = tmp_mouse_x;
	yg->last_mouse_y = tmp_mouse_y;

	if (yg->bottom_z && yg->bottom_z->anim_mode) mark_window(yg, yg->bottom_z);
	if (yg->top_z && yg->top_z->anim_mode) mark_window(yg, yg->top_z);
	foreach (node, yg->mid_zs) {
		yutani_server_window_t * w = node->value;
		if (w && w->anim_mode) mark_window(yg, w);
	}
	foreach (node, yg->overlay_zs) {
		yutani_server_window_t * w = node->value;
		if (w && w->anim_mode) mark_window(yg, w);
	}
	foreach (node, yg->menu_zs) {
		yutani_server_window_t * w = node->value;
		if (w && w->anim_mode) mark_window(yg, w);
	}

	/* Calculate damage regions from currently queued updates */
	while (yg->update_list->length) {
		node_t * win = list_dequeue(yg->update_list);
		yutani_damage_rect_t * rect = (void *)win->value;

		/* We add a clip region for each window in the update queue */
		has_updates = 1;
		gfx_add_clip(yg->backend_ctx, rect->x, rect->y, rect->width, rect->height);
#ifdef ENABLE_BLUR_BEHIND
		gfx_add_clip(clip_ctx, rect->x - BLUR_CLIP_MAX, rect->y - BLUR_CLIP_MAX, rect->width + BLUR_CLIP_MAX * 2, rect->height + BLUR_CLIP_MAX * 2);
#endif
		free(rect);
		free(win);
	}

	/* Render */
	if (has_updates) {

#ifdef ENABLE_BLUR_BEHIND
		/* Extend clips */
		char * oclip = yg->backend_ctx->clips;
		yg->backend_ctx->clips = clip_ctx->clips;
#endif

		/*
		 * In theory, we should restrict this to windows within the clip region,
		 * but calculating that may be more trouble than it's worth;
		 * we also need to render windows in stacking order...
		 */
		yutani_blit_windows(yg);

#ifdef ENABLE_BLUR_BEHIND
		/* Restore clip context */
		yg->backend_ctx->clips = oclip;
#endif

		/* Send VirtualBox rects */
		yutani_post_vbox_rects(yg);

#if YUTANI_DEBUG_WINDOW_SHAPES
#define WINDOW_SHAPE_VIEWER_SIZE 20
		/*
		 * Debugging window shapes: draw a box around the mouse cursor
		 * showing which window is at the top and will accept mouse events.
		 */
		if (yg->debug_shapes) {
			int _ly = max(0,tmp_mouse_y/MOUSE_SCALE - WINDOW_SHAPE_VIEWER_SIZE);
			int _hy = min(yg->height,tmp_mouse_y/MOUSE_SCALE + WINDOW_SHAPE_VIEWER_SIZE);
			int _lx = max(0,tmp_mouse_x/MOUSE_SCALE - 20);
			int _hx = min(yg->width,tmp_mouse_x/MOUSE_SCALE + WINDOW_SHAPE_VIEWER_SIZE);
			for (int y = _ly; y < _hy; ++y) {
				for (int x = _lx; x < _hx; ++x) {
					yutani_server_window_t * w = top_at(yg, x, y);
					if (w) { GFX(yg->backend_ctx, x, y) = yutani_color_for_wid(w->wid); }
				}
			}
		}
#endif

		if (yutani_options.nested) {
			flip(yg->backend_ctx);
			/*
			 * We should be able to flip only the places we need to flip, but
			 * instead we're going to flip the whole thing.
			 *
			 * TODO: Do a better job of this.
			 */
			yutani_flip(yg->host_context, yg->host_window);
			yutani_server_window_t * tmp_window = top_at(yg, yg->mouse_x / MOUSE_SCALE, yg->mouse_y / MOUSE_SCALE);
			if (yg->mouse_state == YUTANI_MOUSE_STATE_MOVING) {
				yutani_window_show_mouse(yg->host_context, yg->host_window, YUTANI_CURSOR_TYPE_DRAG);
			} else if (!tmp_window || tmp_window->show_mouse) {
				yutani_window_show_mouse(yg->host_context, yg->host_window, tmp_window ? tmp_window->show_mouse : 1);
			}
		} else {

			/*
			 * Draw the cursor.
			 * We may also want to draw other compositor elements, like effects, but those
			 * can also go in the stack order of the windows.
			 */
			yutani_server_window_t * tmp_window = top_at(yg, yg->mouse_x / MOUSE_SCALE, yg->mouse_y / MOUSE_SCALE);
			if (!tmp_window || tmp_window->show_mouse) {
				draw_cursor(yg, tmp_mouse_x, tmp_mouse_y, tmp_window ? tmp_window->show_mouse : 1);
			}

			/*
			 * Flip the updated areas. This minimizes writes to video memory,
			 * which is very important on real hardware where these writes are slow.
			 */
			if (yg->backend_ctx->size == 0) {
				extern void gfx_flip_24bit(gfx_context_t * ctx);
				gfx_flip_24bit(yg->backend_ctx);
			} else {
				flip(yg->backend_ctx);
			}
		}

		foreach (node, yg->windows) {
			yutani_server_window_t * w = node->value;
			if (w->z == YUTANI_ZORDER_MAX && w != yg->top_z) {
				if (yutani_is_closing_animation[w->anim_mode]) {
					list_insert(yg->windows_to_remove, w);
				}
			}
		}

		/*
		 * If any windows were marked for removal,
		 * then remove them.
		 */
		while (yg->windows_to_remove->tail) {
			node_t * node = list_pop(yg->windows_to_remove);
			window_actually_close(yg, node->value);
			free(node);
		}

	}

	if (yg->screenshot_frame) {
		yutani_screenshot(yg);
	}
}

/**
 * Initialize clipping regions.
 */
void yutani_clip_init(yutani_globals_t * yg) {
	yg->update_list = list_create();
}

/**
 * Mark a region within a window as damaged.
 *
 * If the window is rotated, we calculate the minimum rectangle that covers
 * the whole region specified and then mark that.
 */
static void mark_window_relative(yutani_globals_t * yg, yutani_server_window_t * window, int32_t x, int32_t y, int32_t width, int32_t height) {
	yutani_damage_rect_t * rect = malloc(sizeof(yutani_damage_rect_t));
	yutani_server_window_t fake_window;

	if (window == yg->resizing_window) {
		fake_window.width  = yg->resizing_init_w;
		fake_window.height = yg->resizing_init_h;
		fake_window.x = window->x;
		fake_window.y = window->y;
		fake_window.rotation = window->rotation;

		double x_scale = (double)yg->resizing_w / (double)yg->resizing_window->width;
		double y_scale = (double)yg->resizing_h / (double)yg->resizing_window->height;

		x *= x_scale;
		x += yg->resizing_offset_x - 1;

		y *= y_scale;
		y += yg->resizing_offset_y - 1;

		width *= x_scale;
		height *= y_scale;

		width += 2;
		height += 2;

		window = &fake_window;
	}

	if (window->rotation == 0) {
		rect->x = window->x + x;
		rect->y = window->y + y;
		rect->width = width;
		rect->height = height;
	} else {
		int32_t ul_x, ul_y;
		int32_t ll_x, ll_y;
		int32_t ur_x, ur_y;
		int32_t lr_x, lr_y;

		yutani_window_to_device(window, x, y, &ul_x, &ul_y);
		yutani_window_to_device(window, x, y + height, &ll_x, &ll_y);
		yutani_window_to_device(window, x + width, y, &ur_x, &ur_y);
		yutani_window_to_device(window, x + width, y + height, &lr_x, &lr_y);

		/* Calculate bounds */

		int32_t left_bound = min(min(ul_x, ll_x), min(ur_x, lr_x));
		int32_t top_bound  = min(min(ul_y, ll_y), min(ur_y, lr_y));

		int32_t right_bound  = max(max(ul_x+1, ll_x+1), max(ur_x+1, lr_x+1));
		int32_t bottom_bound = max(max(ul_y+1, ll_y+1), max(ur_y+1, lr_y+1));

		rect->x = left_bound;
		rect->y = top_bound;
		rect->width = right_bound - left_bound;
		rect->height = bottom_bound - top_bound;
	}

	list_insert(yg->update_list, rect);
}

/**
 * (Convenience function) Mark a whole a window as damaged.
 */
static void mark_window(yutani_globals_t * yg, yutani_server_window_t * window) {
	mark_window_relative(yg, window, 0, 0, window->width, window->height);
}

/**
 * Set a window as closed. It will be removed after rendering has completed.
 */
static void window_mark_for_close(yutani_globals_t * yg, yutani_server_window_t * w) {
	if (w->hidden) {
		window_actually_close(yg, w);
	} else {
		w->anim_mode = yutani_pick_animation(w->server_flags, 1);
		w->anim_start = yutani_current_time(yg);
	}
}

/**
 * Remove a window from its owner's child set.
 */
static void window_remove_from_client(yutani_globals_t * yg, yutani_server_window_t * w) {
	list_t * client_list = hashmap_get(yg->clients_to_windows, (void *)(uintptr_t)w->owner);
	if (client_list) {
		node_t * n = list_find(client_list, w);
		if (n) {
			list_delete(client_list, n);
			free(n);
		}
	}
}

/**
 * Actually remove a window and free the associated resources.
 */
static void window_actually_close(yutani_globals_t * yg, yutani_server_window_t * w) {
	/* Remove from the wid -> window mapping */
	hashmap_remove(yg->wids_to_windows, (void *)(uintptr_t)w->wid);

	/* Remove from the general list of windows. */
	list_remove(yg->windows, list_index_of(yg->windows, w));

	/* Unstack the window */
	unorder_window(yg, w);

	/* Mark the region where the window was */
	mark_window(yg, w);

	/* And if it was focused, unfocus it. */
	if (w == yg->focused_window) {
		/* find the top z-ordered window */
		yg->focused_window = NULL;
		if (yg->menu_zs->tail && yg->menu_zs->tail->value) {
			set_focused_window(yg, yg->menu_zs->tail->value);
		} else if (yg->mid_zs->tail && yg->mid_zs->tail->value) {
			set_focused_window(yg, yg->mid_zs->tail->value);
		}
	}

	{
		char key[1024];
		YUTANI_SHMKEY_EXP(yg->server_ident, key, 1024, w->bufid);

		/*
		 * Normally we would acquire a lock before doing this, but the render
		 * thread holds that lock already and we are only called from the
		 * render thread, so we don't bother.
		 */
		shm_release(key);
	}

	/* Notify subscribers that there are changes to windows */
	notify_subscribers(yg);
}

/**
 * Generate flags for client advertisements.
 *
 * Currently, we only have one flag (focused).
 */
static uint32_t ad_flags(yutani_globals_t * yg, yutani_server_window_t * win) {
	uint32_t flags = win->client_flags;
	if (win == yg->focused_window) {
		flags |= 1;
	}
	return flags;
}

/**
 * Send a result for a window query.
 */
static void yutani_query_result(yutani_globals_t * yg, uintptr_t dest, yutani_server_window_t * win) {
	if (win && win->client_length) {
		yutani_msg_buildx_window_advertise_alloc(response, win->client_length);
		yutani_msg_buildx_window_advertise(response, win->wid, ad_flags(yg, win), win->client_icon, win->bufid, win->width, win->height, win->client_length, win->client_strings);
		pex_send(yg->server, dest, response->size, (char *)response);
	}
}

/**
 * Send a notice to all subscribed clients that windows have updated.
 */
static void notify_subscribers(yutani_globals_t * yg) {
	yutani_msg_buildx_notify_alloc(response);
	yutani_msg_buildx_notify(response);
	list_t * remove = NULL;
	foreach(node, yg->window_subscribers) {
		uintptr_t subscriber = (uintptr_t)node->value;
		if (!hashmap_has(yg->clients_to_windows, (void *)subscriber)) {
			if (!remove) {
				remove = list_create();
			}
			list_insert(remove, node);
		} else {
			pex_send(yg->server, subscriber, response->size, (char *)response);
		}
	}
	if (remove) {
		while (remove->length) {
			node_t * n = list_pop(remove);
			list_delete(yg->window_subscribers, n->value);
			free(n);
		}
		free(remove);
	}
}

static void window_move(yutani_globals_t * yg, yutani_server_window_t * window, int x, int y) {
	mark_window(yg, window);
	window->x = x;
	window->y = y;
	mark_window(yg, window);

	yutani_msg_buildx_window_move_alloc(response);
	yutani_msg_buildx_window_move(response, window->wid, x, y);
	pex_send(yg->server, window->owner, response->size, (char *)response);
}

/**
 * Move and resize a window to fit a particular tiling pattern.
 *
 * x and y are 0-based
 * width_div and height_div are the number of cells in each dimension
 */
static void window_tile(yutani_globals_t * yg, yutani_server_window_t * window,  int width_div, int height_div, int x, int y) {
	int panel_h = 0;
	yutani_server_window_t * panel = yg->top_z;
	if (panel) {
		panel_h = panel->height;
		if (panel->y < 1) {
			panel_h += panel->y; /* We can move the panel up to "hide" it. */
		}
	}

	if (!window->tiled) {
		window->untiled_width = window->width;
		window->untiled_height = window->height;
		window->untiled_left = window->x;
		window->untiled_top = window->y;
		window->tiled = 1;
	}

	int w = yg->width / width_div;
	int h = (yg->height - panel_h) / height_div;
	int _x = w * x;
	int _y = panel_h + h * y;
	if (x == width_div - 1) {
		w = yg->width - w * x;
	}
	if (y == height_div - 1) {
		h = (yg->height - panel_h) - h * y;
	}

	int tile = YUTANI_RESIZE_TILED;

	/* If not left most */
	if (x > 0) {
		_x -= 1;
		w++;
		tile &= ~YUTANI_RESIZE_TILE_LEFT;
	}

	/* If not right most */
	if (x < width_div-1) {
		tile &= ~YUTANI_RESIZE_TILE_RIGHT;
	}

	/* If not top most */
	if (y > 0) {
		_y -= 1;
		h++;
		tile &= ~YUTANI_RESIZE_TILE_UP;
	}

	/* If not bottom most */
	if (y < height_div-1) {
		tile &= ~YUTANI_RESIZE_TILE_DOWN;
	}

	window_move(yg, window, _x, _y);
	yutani_msg_buildx_window_resize_alloc(response);
	yutani_msg_buildx_window_resize(response, YUTANI_MSG_RESIZE_OFFER, window->wid, w, h, 0, tile);
	pex_send(yg->server, window->owner, response->size, (char *)response);
}

/**
 * Take a previously tiled window and "untile" it, eg. restore its original size.
 */
static void window_untile(yutani_globals_t * yg, yutani_server_window_t * window) {
	window->tiled = 0;

	yutani_msg_buildx_window_resize_alloc(response);
	yutani_msg_buildx_window_resize(response,YUTANI_MSG_RESIZE_OFFER, window->wid, window->untiled_width, window->untiled_height, 0, 0);
	pex_send(yg->server, window->owner, response->size, (char *)response);
}

static void window_reveal(yutani_globals_t * yg, yutani_server_window_t * window) {
	if (!window->hidden) return;

	window->hidden = 0;
	window->anim_mode = yutani_pick_animation(window->server_flags, 0);
	window->anim_start = yutani_current_time(yg);
}

/**
 * Process a key event.
 *
 * These are mostly compositor shortcuts and bindings.
 * We also process key bindings for other applications.
 */
static void handle_key_event(yutani_globals_t * yg, struct yutani_msg_key_event * ke) {
	yg->active_modifiers = ke->event.modifiers;
	yutani_server_window_t * focused = get_focused(yg);
	if (focused) {
#if 1
		if ((ke->event.action == KEY_ACTION_DOWN) &&
			(ke->event.modifiers & KEY_MOD_LEFT_SUPER) &&
			(ke->event.modifiers & KEY_MOD_LEFT_SHIFT) &&
			(ke->event.keycode == 'z')) {
			mark_window(yg,focused);
			focused->rotation -= 5;
			mark_window(yg,focused);
			return;
		}
		if ((ke->event.action == KEY_ACTION_DOWN) &&
			(ke->event.modifiers & KEY_MOD_LEFT_SUPER) &&
			(ke->event.modifiers & KEY_MOD_LEFT_SHIFT) &&
			(ke->event.keycode == 'x')) {
			mark_window(yg,focused);
			focused->rotation += 5;
			mark_window(yg,focused);
			return;
		}
		if ((ke->event.action == KEY_ACTION_DOWN) &&
			(ke->event.modifiers & KEY_MOD_LEFT_SUPER) &&
			(ke->event.modifiers & KEY_MOD_LEFT_SHIFT) &&
			(ke->event.keycode == 'c')) {
			mark_window(yg,focused);
			focused->rotation = 0;
			mark_window(yg,focused);
			return;
		}
#endif
		if ((ke->event.action == KEY_ACTION_DOWN) &&
			(ke->event.modifiers & KEY_MOD_LEFT_ALT) &&
			(ke->event.keycode == KEY_F10)) {
			if (focused->z != YUTANI_ZORDER_BOTTOM && focused->z != YUTANI_ZORDER_TOP) {
				if (focused->tiled) {
					window_untile(yg, focused);
					window_move(yg, focused, focused->untiled_left, focused->untiled_top);
				} else {
					window_tile(yg, focused, 1, 1, 0, 0);
				}
				return;
			}
		}
		if ((ke->event.action == KEY_ACTION_DOWN) &&
			(ke->event.modifiers & KEY_MOD_LEFT_ALT) &&
			(ke->event.keycode == KEY_F4)) {
			if (focused->z != YUTANI_ZORDER_BOTTOM && focused->z != YUTANI_ZORDER_TOP) {
				yutani_msg_buildx_window_close_alloc(response);
				yutani_msg_buildx_window_close(response, focused->wid);
				pex_send(yg->server, focused->owner, response->size, (char *)response);
				return;
			}
		}
#if YUTANI_DEBUG_WINDOW_SHAPES
		if ((ke->event.action == KEY_ACTION_DOWN) &&
			(ke->event.modifiers & KEY_MOD_LEFT_SUPER) &&
			(ke->event.modifiers & KEY_MOD_LEFT_SHIFT) &&
			(ke->event.keycode == 'n')) {
			yg->debug_shapes = (1-yg->debug_shapes);
			return;
		}
#endif
#if YUTANI_DEBUG_WINDOW_BOUNDS
		if ((ke->event.action == KEY_ACTION_DOWN) &&
			(ke->event.modifiers & KEY_MOD_LEFT_SUPER) &&
			(ke->event.modifiers & KEY_MOD_LEFT_SHIFT) &&
			(ke->event.keycode == 'b')) {
			yg->debug_bounds = (1-yg->debug_bounds);
			return;
		}
#endif
#ifdef ENABLE_BLUR_BEHIND
		if ((ke->event.action == KEY_ACTION_DOWN) &&
			(ke->event.modifiers & KEY_MOD_LEFT_SUPER) &&
			(ke->event.modifiers & KEY_MOD_LEFT_SHIFT) &&
			(ke->event.keycode == 'v')) {
			if (focused->z != YUTANI_ZORDER_BOTTOM && focused->z != YUTANI_ZORDER_TOP) {
				focused->server_flags ^= YUTANI_WINDOW_FLAG_BLUR_BEHIND;
				mark_window(yg, focused);
			}
			return;
		}
#endif
		/* Screenshot key */
		if ((ke->event.action == KEY_ACTION_DOWN) &&
			(ke->event.keycode == KEY_PRINT_SCREEN)) {
			if (ke->event.modifiers & (KEY_MOD_LEFT_SHIFT | KEY_MOD_RIGHT_SHIFT)) {
				yg->screenshot_frame = YUTANI_SCREENSHOT_WINDOW;
			} else {
				yg->screenshot_frame = YUTANI_SCREENSHOT_FULL;
			}
		}
		if ((ke->event.action == KEY_ACTION_DOWN) &&
			(ke->event.keycode == KEY_ESCAPE) &&
			(yg->mouse_state == YUTANI_MOUSE_STATE_MOVING)) {
			mouse_stop_drag(yg);
			return;
		}
		/*
		 * Tiling hooks.
		 * These are based on the compiz grid plugin.
		 */
		if ((ke->event.action == KEY_ACTION_DOWN) &&
			(ke->event.modifiers & KEY_MOD_LEFT_SUPER)) {
			if ((ke->event.modifiers & KEY_MOD_LEFT_SHIFT) &&
				(ke->event.keycode == KEY_ARROW_LEFT)) {
				if (focused->z != YUTANI_ZORDER_BOTTOM && focused->z != YUTANI_ZORDER_TOP) {
					window_tile(yg, focused, 2, 2, 0, 0);
					return;
				}
			}
			if ((ke->event.modifiers & KEY_MOD_LEFT_SHIFT) &&
				(ke->event.keycode == KEY_ARROW_RIGHT)) {
				if (focused->z != YUTANI_ZORDER_BOTTOM && focused->z != YUTANI_ZORDER_TOP) {
					window_tile(yg, focused, 2, 2, 1, 0);
					return;
				}
			}
			if ((ke->event.modifiers & KEY_MOD_LEFT_CTRL) &&
				(ke->event.keycode == KEY_ARROW_LEFT)) {
				if (focused->z != YUTANI_ZORDER_BOTTOM && focused->z != YUTANI_ZORDER_TOP) {
					window_tile(yg, focused, 2, 2, 0, 1);
					return;
				}
			}
			if ((ke->event.modifiers & KEY_MOD_LEFT_CTRL) &&
				(ke->event.keycode == KEY_ARROW_RIGHT)) {
				if (focused->z != YUTANI_ZORDER_BOTTOM && focused->z != YUTANI_ZORDER_TOP) {
					window_tile(yg, focused, 2, 2, 1, 1);
					return;
				}
			}
			if ((ke->event.keycode == KEY_ARROW_LEFT)) {
				if (focused->z != YUTANI_ZORDER_BOTTOM && focused->z != YUTANI_ZORDER_TOP) {
					window_tile(yg, focused, 2, 1, 0, 0);
					return;
				}
			}
			if ((ke->event.keycode == KEY_ARROW_RIGHT)) {
				if (focused->z != YUTANI_ZORDER_BOTTOM && focused->z != YUTANI_ZORDER_TOP) {
					window_tile(yg, focused, 2, 1, 1, 0);
					return;
				}
			}
			if ((ke->event.keycode == KEY_ARROW_UP)) {
				if (focused->z != YUTANI_ZORDER_BOTTOM && focused->z != YUTANI_ZORDER_TOP) {
					window_tile(yg, focused, 1, 2, 0, 0);
					return;
				}
			}
			if ((ke->event.keycode == KEY_ARROW_DOWN)) {
				if (focused->z != YUTANI_ZORDER_BOTTOM && focused->z != YUTANI_ZORDER_TOP) {
					window_tile(yg, focused, 1, 2, 0, 1);
					return;
				}
			}
		}
	}

	/*
	 * External bindings registered by clients.
	 */
	uint32_t key_code = ((ke->event.modifiers << 24) | (ke->event.keycode));
	if (hashmap_has(yg->key_binds, (void*)(uintptr_t)key_code)) {
		struct key_bind * bind = hashmap_get(yg->key_binds, (void*)(uintptr_t)key_code);

		yutani_msg_buildx_key_event_alloc(response);
		yutani_msg_buildx_key_event(response,focused ? focused->wid : UINT32_MAX, &ke->event, &ke->state);
		pex_send(yg->server, bind->owner, response->size, (char *)response);

		if (bind->response == YUTANI_BIND_STEAL) {
			/* If this keybinding was registered as "steal", we'll stop here. */
			return;
		}
	}

	/* Finally, send the key to the focused client. */
	if (focused) {

		yutani_msg_buildx_key_event_alloc(response);
		yutani_msg_buildx_key_event(response,focused->wid, &ke->event, &ke->state);
		pex_send(yg->server, focused->owner, response->size, (char *)response);

	}
}

/**
 * Register a new keybinding.
 *
 * req - bind message
 * owner - client to assign the binding to
 */
static void add_key_bind(yutani_globals_t * yg, struct yutani_msg_key_bind * req, uintptr_t owner) {
	uint32_t key_code = (((uint8_t)req->modifiers << 24) | ((uint32_t)req->key & 0xFFFFFF));
	struct key_bind * bind = hashmap_get(yg->key_binds, (void*)(uintptr_t)key_code);

	if (!bind) {
		bind = malloc(sizeof(struct key_bind));

		bind->owner = owner;
		bind->response = req->response;

		hashmap_set(yg->key_binds, (void*)(uintptr_t)key_code, bind);
	} else {
		bind->owner = owner;
		bind->response = req->response;
	}
}

static void adjust_window_opacity(yutani_globals_t * yg, int direction) {
	yutani_server_window_t * window = top_at(yg, yg->mouse_x / MOUSE_SCALE, yg->mouse_y / MOUSE_SCALE);

	if (window && window->z != YUTANI_ZORDER_BOTTOM) {
		window->opacity += direction;
		if (window->opacity < 0) {
			window->opacity = 0;
		}
		if (window->opacity > 255) {
			window->opacity = 255;
		}
		mark_window(yg, window);
	}

}

static void mouse_stop_drag(yutani_globals_t * yg) {
	yg->mouse_window = NULL;
	yg->mouse_state = YUTANI_MOUSE_STATE_NORMAL;
	mark_screen(yg, yg->mouse_x / MOUSE_SCALE - MOUSE_OFFSET_X, yg->mouse_y / MOUSE_SCALE - MOUSE_OFFSET_Y, MOUSE_WIDTH, MOUSE_HEIGHT);
}

static void mouse_start_drag(yutani_globals_t * yg, yutani_server_window_t * w) {
	if (yg->mouse_state == YUTANI_MOUSE_STATE_RESIZING || yg->mouse_state == YUTANI_MOUSE_STATE_ROTATING) return; /* Refuse */
	set_focused_at(yg, yg->mouse_x / MOUSE_SCALE, yg->mouse_y / MOUSE_SCALE);
	if (!w) {
		yg->mouse_window = get_focused(yg);
	} else {
		yg->mouse_window = w;
	}
	if (yg->mouse_window) {
		if (yg->mouse_window->z == YUTANI_ZORDER_BOTTOM || yg->mouse_window->z == YUTANI_ZORDER_TOP
		    || yg->mouse_window->server_flags & YUTANI_WINDOW_FLAG_DISALLOW_DRAG) {
			yg->mouse_state = YUTANI_MOUSE_STATE_NORMAL;
			yg->mouse_window = NULL;
		} else {
			yg->mouse_state = YUTANI_MOUSE_STATE_MOVING;
			yg->mouse_init_x = yg->mouse_x;
			yg->mouse_init_y = yg->mouse_y;
			yg->mouse_win_x  = yg->mouse_window->x;
			yg->mouse_win_y  = yg->mouse_window->y;
			yg->mouse_drag_button = yg->last_mouse_buttons;
			mark_screen(yg, yg->mouse_x / MOUSE_SCALE - MOUSE_OFFSET_X, yg->mouse_y / MOUSE_SCALE - MOUSE_OFFSET_Y, MOUSE_WIDTH, MOUSE_HEIGHT);
			make_top(yg, yg->mouse_window);
		}
	}
}

static void mouse_start_rotate(yutani_globals_t * yg) {
	set_focused_at(yg, yg->mouse_x / MOUSE_SCALE, yg->mouse_y / MOUSE_SCALE);
	yg->mouse_window = get_focused(yg);
	if (yg->mouse_window) {
		if (yg->mouse_window->z == YUTANI_ZORDER_BOTTOM || yg->mouse_window->z == YUTANI_ZORDER_TOP) {
			/* Prevent rotating panel and wallpaper */
			yg->mouse_state = YUTANI_MOUSE_STATE_NORMAL;
			yg->mouse_window = NULL;
			return;
		}
		yg->mouse_state = YUTANI_MOUSE_STATE_ROTATING;
		yg->mouse_init_x = yg->mouse_x;
		yg->mouse_init_y = yg->mouse_y;
		int32_t x_diff = yg->mouse_x / MOUSE_SCALE - (yg->mouse_window->x + yg->mouse_window->width / 2);
		int32_t y_diff = yg->mouse_y / MOUSE_SCALE - (yg->mouse_window->y + yg->mouse_window->height / 2);
		int new_r = atan2(x_diff, y_diff) * 180.0 / (-M_PI);
		yg->mouse_init_r = yg->mouse_window->rotation - new_r;
		make_top(yg, yg->mouse_window);
	}
}

static void mouse_start_resize(yutani_globals_t * yg, yutani_scale_direction_t direction) {
	set_focused_at(yg, yg->mouse_x / MOUSE_SCALE, yg->mouse_y / MOUSE_SCALE);
	yg->mouse_window = get_focused(yg);
	if (yg->mouse_window) {
		if (yg->mouse_window->z == YUTANI_ZORDER_BOTTOM || yg->mouse_window->z == YUTANI_ZORDER_TOP
		    || yg->mouse_window->server_flags & YUTANI_WINDOW_FLAG_DISALLOW_RESIZE) {
			/* Prevent resizing panel and wallpaper */
			yg->mouse_state = YUTANI_MOUSE_STATE_NORMAL;
			yg->mouse_window = NULL;
			yg->resizing_window = NULL;
		} else {
			TRACE("resize starting for wid=%d", yg->mouse_window -> wid);
			yg->mouse_state = YUTANI_MOUSE_STATE_RESIZING;
			yg->mouse_init_x = yg->mouse_x;
			yg->mouse_init_y = yg->mouse_y;
			yg->mouse_win_x  = yg->mouse_window->x;
			yg->mouse_win_y  = yg->mouse_window->y;
			yg->resizing_window = yg->mouse_window;
			yg->resizing_w = yg->mouse_window->width;
			yg->resizing_h = yg->mouse_window->height;
			yg->resizing_offset_x = 0;
			yg->resizing_offset_y = 0;
			yg->resizing_init_w = yg->mouse_window->width;
			yg->resizing_init_h = yg->mouse_window->height;

			if (direction == SCALE_AUTO) {
				/* Determine the best direction to scale in based on simple 9-cell system. */
				int32_t x, y;
				yutani_device_to_window(yg->resizing_window, yg->mouse_x / MOUSE_SCALE, yg->mouse_y / MOUSE_SCALE, &x, &y);

				int h_d = 0;
				int v_d = 0;

				if (y <= yg->resizing_h / 3) {
					v_d = -1;
				} else if (y >= (yg->resizing_h / 3) * 2) {
					v_d = 1;
				}
				if (x <= yg->resizing_w / 3) {
					h_d = -1;
				} else if (x >= (yg->resizing_w / 3) * 2) {
					h_d = 1;
				}

				/* Fall back */
				if (h_d ==  0 && v_d ==  0) direction = SCALE_DOWN_RIGHT;

				else if (h_d ==  1 && v_d ==  1) direction = SCALE_DOWN_RIGHT;
				else if (h_d ==  1 && v_d == -1) direction = SCALE_UP_RIGHT;
				else if (h_d == -1 && v_d ==  1) direction = SCALE_DOWN_LEFT;
				else if (h_d == -1 && v_d == -1) direction = SCALE_UP_LEFT;

				else if (h_d ==  1 && v_d ==  0) direction = SCALE_RIGHT;
				else if (h_d == -1 && v_d ==  0) direction = SCALE_LEFT;
				else if (h_d ==  0 && v_d ==  1) direction = SCALE_DOWN;
				else if (h_d ==  0 && v_d == -1) direction = SCALE_UP;
			}

			yg->resizing_direction = direction;
			make_top(yg, yg->mouse_window);
			mark_window(yg, yg->resizing_window);
		}
	}
}

static void handle_mouse_event(yutani_globals_t * yg, struct yutani_msg_mouse_event * me)  {
	if (me->type == YUTANI_MOUSE_EVENT_TYPE_RELATIVE) {
		yg->mouse_x += me->event.x_difference YUTANI_INCOMING_MOUSE_SCALE;
		yg->mouse_y -= me->event.y_difference YUTANI_INCOMING_MOUSE_SCALE;
	} else if (me->type == YUTANI_MOUSE_EVENT_TYPE_ABSOLUTE) {
		yg->mouse_x = me->event.x_difference * MOUSE_SCALE;
		yg->mouse_y = me->event.y_difference * MOUSE_SCALE;
	}

	if (yg->mouse_x < 0) yg->mouse_x = 0;
	if (yg->mouse_y < 0) yg->mouse_y = 0;
	if (yg->mouse_x > (int)(yg->width) * MOUSE_SCALE) yg->mouse_x = (yg->width) * MOUSE_SCALE;
	if (yg->mouse_y > (int)(yg->height) * MOUSE_SCALE) yg->mouse_y = (yg->height) * MOUSE_SCALE;

	switch (yg->mouse_state) {
		case YUTANI_MOUSE_STATE_NORMAL:
			{
				if ((me->event.buttons & YUTANI_MOUSE_BUTTON_LEFT) &&
						(yg->active_modifiers & YUTANI_KEY_MODIFIER_ALT)) {
					mouse_start_drag(yg, NULL);
				} else if ((me->event.buttons & YUTANI_MOUSE_SCROLL_UP) &&
						(yg->active_modifiers & YUTANI_KEY_MODIFIER_ALT)) {
					adjust_window_opacity(yg, 8);
				} else if ((me->event.buttons & YUTANI_MOUSE_SCROLL_DOWN) &&
						(yg->active_modifiers & YUTANI_KEY_MODIFIER_ALT)) {
					adjust_window_opacity(yg, -8);
				} else if ((me->event.buttons & YUTANI_MOUSE_BUTTON_RIGHT) &&
						(yg->active_modifiers & YUTANI_KEY_MODIFIER_ALT)) {
					mouse_start_rotate(yg);
				} else if ((me->event.buttons & YUTANI_MOUSE_BUTTON_MIDDLE) &&
						(yg->active_modifiers & YUTANI_KEY_MODIFIER_ALT)) {
					yg->resizing_button = YUTANI_MOUSE_BUTTON_MIDDLE;
					mouse_start_resize(yg, SCALE_AUTO);
				} else if ((me->event.buttons & YUTANI_MOUSE_BUTTON_LEFT) &&
						!(yg->active_modifiers & YUTANI_KEY_MODIFIER_ALT)) {
					yg->mouse_state = YUTANI_MOUSE_STATE_DRAGGING;
					set_focused_at(yg, yg->mouse_x / MOUSE_SCALE, yg->mouse_y / MOUSE_SCALE);
					yg->mouse_window = get_focused(yg);
					yg->mouse_moved = 0;
					yg->mouse_drag_button = YUTANI_MOUSE_BUTTON_LEFT;
					if (yg->mouse_window) {
						yutani_device_to_window(yg->mouse_window, yg->mouse_x / MOUSE_SCALE, yg->mouse_y / MOUSE_SCALE, &yg->mouse_click_x, &yg->mouse_click_y);
						yutani_msg_buildx_window_mouse_event_alloc(response);
						yutani_msg_buildx_window_mouse_event(response,yg->mouse_window->wid, yg->mouse_click_x, yg->mouse_click_y, -1, -1, me->event.buttons, YUTANI_MOUSE_EVENT_DOWN, yg->active_modifiers);
						yg->mouse_click_x_orig = yg->mouse_click_x;
						yg->mouse_click_y_orig = yg->mouse_click_y;
						pex_send(yg->server, yg->mouse_window->owner, response->size, (char *)response);
					}
				} else {
					yg->mouse_window = get_focused(yg);
					yutani_server_window_t * tmp_window = top_at(yg, yg->mouse_x / MOUSE_SCALE, yg->mouse_y / MOUSE_SCALE);
					if (yg->mouse_window && !(me->event.buttons & YUTANI_MOUSE_BUTTON_RIGHT)) {
						int32_t x, y;
						yutani_device_to_window(yg->mouse_window, yg->mouse_x / MOUSE_SCALE, yg->mouse_y / MOUSE_SCALE, &x, &y);
						yutani_msg_buildx_window_mouse_event_alloc(response);
						yutani_msg_buildx_window_mouse_event(response,yg->mouse_window->wid, x, y, -1, -1, me->event.buttons, YUTANI_MOUSE_EVENT_MOVE, yg->active_modifiers);
						pex_send(yg->server, yg->mouse_window->owner, response->size, (char *)response);
					}
					if (tmp_window) {
						int32_t x, y;
						yutani_msg_buildx_window_mouse_event_alloc(response);
						if (tmp_window != yg->old_hover_window) {
							yutani_device_to_window(tmp_window, yg->mouse_x / MOUSE_SCALE, yg->mouse_y / MOUSE_SCALE, &x, &y);
							yutani_msg_buildx_window_mouse_event(response, tmp_window->wid, x, y, -1, -1, me->event.buttons, YUTANI_MOUSE_EVENT_ENTER, yg->active_modifiers);
							pex_send(yg->server, tmp_window->owner, response->size, (char *)response);
							if (yg->old_hover_window) {
								yutani_device_to_window(yg->old_hover_window, yg->mouse_x / MOUSE_SCALE, yg->mouse_y / MOUSE_SCALE, &x, &y);
								yutani_msg_buildx_window_mouse_event(response, yg->old_hover_window->wid, x, y, -1, -1, me->event.buttons, YUTANI_MOUSE_EVENT_LEAVE, yg->active_modifiers);
								pex_send(yg->server, yg->old_hover_window->owner, response->size, (char *)response);
							}
							yg->old_hover_window = tmp_window;
						}
						if (tmp_window != yg->mouse_window || (me->event.buttons & YUTANI_MOUSE_BUTTON_RIGHT)) {
							yutani_device_to_window(tmp_window, yg->mouse_x / MOUSE_SCALE, yg->mouse_y / MOUSE_SCALE, &x, &y);
							yutani_msg_buildx_window_mouse_event(response, tmp_window->wid, x, y, -1, -1, me->event.buttons, YUTANI_MOUSE_EVENT_MOVE, yg->active_modifiers);
							pex_send(yg->server, tmp_window->owner, response->size, (char *)response);
						}
					}
				}
			}
			break;
		case YUTANI_MOUSE_STATE_MOVING:
			{
				int button_down = (me->event.buttons & YUTANI_MOUSE_BUTTON_LEFT);
				int drag_stop = yg->mouse_drag_button != 0 ? (!button_down) : (button_down);
				if (drag_stop) {
					mouse_stop_drag(yg);
				} else {
					if (yg->mouse_y / MOUSE_SCALE < 10) {
						if (!yg->mouse_window->tiled) {
							window_tile(yg, yg->mouse_window, 1, 1, 0, 0);
						}
						break;
					}
					if (yg->mouse_x / MOUSE_SCALE < 10) {
						if (!yg->mouse_window->tiled) {
							window_tile(yg, yg->mouse_window, 2, 1, 0, 0);
						}
						break;
					} else if (yg->mouse_x / MOUSE_SCALE >= ((int)yg->width - 10)) {
						if (!yg->mouse_window->tiled) {
							window_tile(yg, yg->mouse_window, 2, 1, 1, 0);
						}
						break;
					}
					if (yg->mouse_window->tiled) {
						if ((abs(yg->mouse_x - yg->mouse_init_x) > UNTILE_SENSITIVITY) || (abs(yg->mouse_y - yg->mouse_init_y) > UNTILE_SENSITIVITY)) {
							/* Untile it */
							window_untile(yg,yg->mouse_window);
							/* Position the window such that it's representative of where it was, percentage-wise, in the untiled window */
							float percent_x = (float)(yg->mouse_x / MOUSE_SCALE - yg->mouse_window->x) / (float)yg->mouse_window->width;
							float percent_y = (float)(yg->mouse_y / MOUSE_SCALE - yg->mouse_window->y) / (float)yg->mouse_window->height;
							window_move(yg, yg->mouse_window,
							            yg->mouse_x / MOUSE_SCALE - yg->mouse_window->untiled_width * percent_x,
							            yg->mouse_y / MOUSE_SCALE - yg->mouse_window->untiled_height * percent_y);
							/* reset init_x / init_y */
							yg->mouse_init_x = yg->mouse_x;
							yg->mouse_init_y = yg->mouse_y;
							yg->mouse_win_x  = yg->mouse_window->x;
							yg->mouse_win_y  = yg->mouse_window->y;
						}
					} else {
						int x, y;
						x = yg->mouse_win_x + (yg->mouse_x - yg->mouse_init_x) / MOUSE_SCALE;
						y = yg->mouse_win_y + (yg->mouse_y - yg->mouse_init_y) / MOUSE_SCALE;
						window_move(yg, yg->mouse_window, x, y);
					}
				}
			}
			break;
		case YUTANI_MOUSE_STATE_ROTATING:
			{
				if (!(me->event.buttons & YUTANI_MOUSE_BUTTON_RIGHT)) {
					yg->mouse_window = NULL;
					yg->mouse_state = YUTANI_MOUSE_STATE_NORMAL;
					mark_screen(yg, yg->mouse_x / MOUSE_SCALE - MOUSE_OFFSET_X, yg->mouse_y / MOUSE_SCALE - MOUSE_OFFSET_Y, MOUSE_WIDTH, MOUSE_HEIGHT);
				} else if (yg->mouse_window) {
					/* Calculate rotation and make relative to initial rotation */
					int32_t x_diff = yg->mouse_x / MOUSE_SCALE - (yg->mouse_window->x + yg->mouse_window->width / 2);
					int32_t y_diff = yg->mouse_y / MOUSE_SCALE - (yg->mouse_window->y + yg->mouse_window->height / 2);
					int new_r = atan2(x_diff, y_diff) * 180.0 / (-M_PI);
					mark_window(yg, yg->mouse_window);
					yg->mouse_window->rotation = new_r + yg->mouse_init_r;
					mark_window(yg, yg->mouse_window);
				}
			}
			break;
		case YUTANI_MOUSE_STATE_DRAGGING:
			{
				if (!(me->event.buttons & yg->mouse_drag_button)) {
					/* Mouse released */
					yg->mouse_state = YUTANI_MOUSE_STATE_NORMAL;
					int32_t old_x = yg->mouse_click_x_orig;
					int32_t old_y = yg->mouse_click_y_orig;
					if (yg->mouse_window) {
						yutani_device_to_window(yg->mouse_window, yg->mouse_x / MOUSE_SCALE, yg->mouse_y / MOUSE_SCALE, &yg->mouse_click_x, &yg->mouse_click_y);
						if (!yg->mouse_moved) {
							yutani_msg_buildx_window_mouse_event_alloc(response);
							yutani_msg_buildx_window_mouse_event(response,yg->mouse_window->wid, yg->mouse_click_x, yg->mouse_click_y, -1, -1, me->event.buttons, YUTANI_MOUSE_EVENT_CLICK, yg->active_modifiers);
							pex_send(yg->server, yg->mouse_window->owner, response->size, (char *)response);
						} else {
							yutani_msg_buildx_window_mouse_event_alloc(response);
							yutani_msg_buildx_window_mouse_event(response,yg->mouse_window->wid, yg->mouse_click_x, yg->mouse_click_y, old_x, old_y, me->event.buttons, YUTANI_MOUSE_EVENT_RAISE, yg->active_modifiers);
							pex_send(yg->server, yg->mouse_window->owner, response->size, (char *)response);
						}
					}
				} else {
					yg->mouse_state = YUTANI_MOUSE_STATE_DRAGGING;
					yg->mouse_moved = 1;
					int32_t old_x = yg->mouse_click_x;
					int32_t old_y = yg->mouse_click_y;
					if (yg->mouse_window) {
						yutani_device_to_window(yg->mouse_window, yg->mouse_x / MOUSE_SCALE, yg->mouse_y / MOUSE_SCALE, &yg->mouse_click_x, &yg->mouse_click_y);
						if (old_x != yg->mouse_click_x || old_y != yg->mouse_click_y) {
							yutani_msg_buildx_window_mouse_event_alloc(response);
							yutani_msg_buildx_window_mouse_event(response,yg->mouse_window->wid, yg->mouse_click_x, yg->mouse_click_y, old_x, old_y, me->event.buttons, YUTANI_MOUSE_EVENT_DRAG, yg->active_modifiers);
							pex_send(yg->server, yg->mouse_window->owner, response->size, (char *)response);
						}
					}
				}
			}
			break;
		case YUTANI_MOUSE_STATE_RESIZING:
			{

				int32_t relative_x, relative_y;
				int32_t relative_init_x, relative_init_y;

				yutani_server_window_t fake_window = {
					.width = yg->resizing_init_w,
					.height = yg->resizing_init_h,
					.x = yg->resizing_window->x,
					.y = yg->resizing_window->y,
					.rotation = yg->resizing_window->rotation,
				};

				yutani_device_to_window(&fake_window, yg->mouse_init_x / MOUSE_SCALE, yg->mouse_init_y / MOUSE_SCALE, &relative_init_x, &relative_init_y);
				yutani_device_to_window(&fake_window, yg->mouse_x / MOUSE_SCALE, yg->mouse_y / MOUSE_SCALE, &relative_x, &relative_y);

				int width_diff  = (relative_x - relative_init_x);
				int height_diff = (relative_y - relative_init_y);

				mark_window(yg, yg->resizing_window);

				if (yg->resizing_direction == SCALE_UP || yg->resizing_direction == SCALE_DOWN) {
					width_diff = 0;
					yg->resizing_offset_x = 0;
				}

				if (yg->resizing_direction == SCALE_LEFT || yg->resizing_direction == SCALE_RIGHT) {
					height_diff = 0;
					yg->resizing_offset_y = 0;
				}

				if (yg->resizing_direction == SCALE_LEFT ||
				    yg->resizing_direction == SCALE_UP_LEFT ||
				    yg->resizing_direction == SCALE_DOWN_LEFT) {
					yg->resizing_offset_x = width_diff;
					width_diff = -width_diff;
				} else if (yg->resizing_direction == SCALE_RIGHT ||
				           yg->resizing_direction == SCALE_UP_RIGHT ||
				           yg->resizing_direction == SCALE_DOWN_RIGHT) {
					yg->resizing_offset_x = 0;
				}

				if (yg->resizing_direction == SCALE_UP ||
				    yg->resizing_direction == SCALE_UP_LEFT ||
				    yg->resizing_direction == SCALE_UP_RIGHT) {
					yg->resizing_offset_y = height_diff;
					height_diff = -height_diff;
				} else if (yg->resizing_direction == SCALE_DOWN ||
				           yg->resizing_direction == SCALE_DOWN_LEFT ||
				           yg->resizing_direction == SCALE_DOWN_RIGHT) {
					yg->resizing_offset_y = 0;
				}

				yg->resizing_w = yg->resizing_init_w + width_diff;
				yg->resizing_h = yg->resizing_init_h + height_diff;

				/* Enforce logical boundaries */
				if (yg->resizing_w < 1) {
					yg->resizing_w = 1;
				}
				if (yg->resizing_h < 1) {
					yg->resizing_h = 1;
				}
				if (yg->resizing_offset_x > yg->resizing_init_w) {
					yg->resizing_offset_x = yg->resizing_init_w;
				}
				if (yg->resizing_offset_y > yg->resizing_init_h) {
					yg->resizing_offset_y = yg->resizing_init_h;
				}

				mark_window(yg, yg->resizing_window);

				if (!yg->resize_release_time || !(me->event.buttons & yg->resizing_button)) {
					yg->resize_release_time = yutani_current_time(yg);
					yutani_msg_buildx_window_resize_alloc(response);
					yutani_msg_buildx_window_resize(response,YUTANI_MSG_RESIZE_OFFER, yg->resizing_window->wid, yg->resizing_w, yg->resizing_h, 0, yg->resizing_window->tiled);
					pex_send(yg->server, yg->resizing_window->owner, response->size, (char *)response);
				}

				if (!(me->event.buttons & yg->resizing_button)) {
					yg->mouse_state = YUTANI_MOUSE_STATE_NORMAL;
				}
			}
			break;
		default:
			/* XXX ? */
			break;
	}
}

static yutani_globals_t * _static_yg;
static void yutani_display_resize_handle(int signum) {
	(void)signum;
	TRACE("Display change request, one moment.");
	_static_yg->resize_on_next = 1;
	signal(SIGWINEVENT, yutani_display_resize_handle);
}

#define FONT_PATH "/usr/share/fonts/"
#define FONT(a,b) {a, FONT_PATH b}

struct font_def {
	char * identifier;
	char * path;
};

/**
 * TODO: This should be configurable...
 */
static struct font_def fonts[] = {
	FONT("sans-serif",            "truetype/dejavu/DejaVuSans.ttf"),
	FONT("sans-serif.bold",       "truetype/dejavu/DejaVuSans-Bold.ttf"),
	FONT("sans-serif.italic",     "truetype/dejavu/DejaVuSans-Oblique.ttf"),
	FONT("sans-serif.bolditalic", "truetype/dejavu/DejaVuSans-BoldOblique.ttf"),
	FONT("monospace",             "truetype/dejavu/DejaVuSansMono.ttf"),
	FONT("monospace.bold",        "truetype/dejavu/DejaVuSansMono-Bold.ttf"),
	FONT("monospace.italic",      "truetype/dejavu/DejaVuSansMono-Oblique.ttf"),
	FONT("monospace.bolditalic",  "truetype/dejavu/DejaVuSansMono-BoldOblique.ttf"),
	{NULL, NULL}
};

static char * precache_shmfont(char * ident, char * name) {
	FILE * f = fopen(name, "r");
	if (!f) return NULL;
	size_t s = 0;
	fseek(f, 0, SEEK_END);
	s = ftell(f);
	fseek(f, 0, SEEK_SET);

	size_t shm_size = s;
	char * font = shm_obtain(ident, &shm_size);
	assert((shm_size >= s) && "shm_obtain returned too little memory to load a font into!");

	fread(font, s, 1, f);

	fclose(f);
	return font;
}

static void load_fonts(yutani_globals_t * yg) {
	int i = 0;
	while (fonts[i].identifier) {
		char tmp[100];
		sprintf(tmp, "sys.%s.fonts.%s", yg->server_ident, fonts[i].identifier);
		TRACE("Loading font %s -> %s", fonts[i].path, tmp);
		if (!precache_shmfont(tmp, fonts[i].path)) {
			TRACE("  ... failed.");
		}
		++i;
	}
}

/**
 * main
 */
int main(int argc, char * argv[]) {

	int argx = 0;
	int results = parse_args(argc, argv, &argx);
	if (results) return results;

	yutani_globals_t * yg = malloc(sizeof(yutani_globals_t));
	memset(yg, 0x00, sizeof(yutani_globals_t));

	if (yutani_options.nested) {
		yg->host_context = yutani_init();
		yg->host_window = yutani_window_create(yg->host_context, yutani_options.nest_width, yutani_options.nest_height);
		yutani_window_move(yg->host_context, yg->host_window, 50, 50);
		yutani_window_advertise_icon(yg->host_context, yg->host_window, "Compositor", "compositor");
		yg->backend_ctx = init_graphics_yutani_double_buffer(yg->host_window);
	} else {
		char * d = getenv("DISPLAY");
		if (d && *d) {
			fprintf(stderr, "DISPLAY is already set but not running nested. This is probably wrong.\n");
			return 1;
		}
		_static_yg = yg;
		signal(SIGWINEVENT, yutani_display_resize_handle);
		yg->backend_ctx = init_graphics_fullscreen_double_buffer();
	}

#ifdef ENABLE_BLUR_BEHIND
	blur_texture = realloc(blur_texture, yg->backend_ctx->stride * yg->backend_ctx->height);
	blur_ctx = init_graphics_with_store(yg->backend_ctx, blur_texture);
	clip_ctx = calloc(1, sizeof(gfx_context_t));
	clip_ctx->width = yg->backend_ctx->width;
	clip_ctx->height = yg->backend_ctx->height;
#endif

	if (!yg->backend_ctx) {
		free(yg);
		TRACE("Failed to open framebuffer, bailing.");
		return 1;
	}

	{
		struct timeval t;
		gettimeofday(&t, NULL);
		yg->start_time = t.tv_sec;
		yg->start_subtime = t.tv_usec;
	}

	yg->width = yg->backend_ctx->width;
	yg->height = yg->backend_ctx->height;

	draw_fill(yg->backend_ctx, rgb(0,0,0));
	flip(yg->backend_ctx);

	yg->backend_framebuffer = yg->backend_ctx->backbuffer;

	if (yutani_options.nested) {
		char * name = malloc(sizeof(char) * 64);
		sprintf(name, "compositor-nest-%d", getpid());
		yg->server_ident = name;
	} else {
		/* XXX check if this already exists? */
		yg->server_ident = "compositor";
	}
	setenv("DISPLAY", yg->server_ident, 1);

	FILE * server = pex_bind(yg->server_ident);
	TRACE("pex bound? %d", server);
	yg->server = server;

	load_fonts(yg);

	TRACE("Loading sprites...");
#define MOUSE_DIR "/usr/share/cursor/"
	load_sprite(&yg->mouse_sprite, MOUSE_DIR "normal.png");
	load_sprite(&yg->mouse_sprite_drag, MOUSE_DIR "grab.png");
	load_sprite(&yg->mouse_sprite_resize_v, MOUSE_DIR "resize-vertical.png");
	load_sprite(&yg->mouse_sprite_resize_h, MOUSE_DIR "resize-horizontal.png");
	load_sprite(&yg->mouse_sprite_resize_da, MOUSE_DIR "resize-uldr.png");
	load_sprite(&yg->mouse_sprite_resize_db, MOUSE_DIR "resize-dlur.png");
	load_sprite(&yg->mouse_sprite_point, MOUSE_DIR "point.png");
	load_sprite(&yg->mouse_sprite_ibeam, MOUSE_DIR "ibeam.png");
	TRACE("Done.");

	TRACE("Initializing variables...");
	yg->last_mouse_x = 0;
	yg->last_mouse_y = 0;
	yg->mouse_x = yg->width * MOUSE_SCALE / 2;
	yg->mouse_y = yg->height * MOUSE_SCALE / 2;

	yg->windows = list_create();
	yg->wids_to_windows = hashmap_create_int(10);
	yg->key_binds = hashmap_create_int(10);
	yg->clients_to_windows = hashmap_create_int(10);
	yg->mid_zs = list_create();
	yg->menu_zs = list_create();
	yg->overlay_zs = list_create();
	yg->windows_to_remove = list_create();

	yg->window_subscribers = list_create();

	yg->last_mouse_buttons = 0;
	yg->resize_release_time = 0;
	TRACE("Done.");

	yutani_clip_init(yg);

	if (!fork()) {
		if (argx < argc) {
			TRACE("Starting alternate startup app: %s", argv[argx]);
			execvp(argv[argx], &argv[argx]);
		} else {
			TRACE("Starting application");
			char * args[] = {"/bin/glogin", NULL};
			execvp(args[0], args);
			TRACE("Failed to start app?");
		}
	}

	int fds[4];
	int mfd = -1;
	int kfd = -1;
	int amfd = -1;
	int vmmouse = 0;
	mouse_device_packet_t packet;
	key_event_t event;
	key_event_state_t state = {0};

	fds[0] = fileno(server);

	if (yutani_options.nested) {
		fds[1] = fileno(yg->host_context->sock);
	} else {
		mfd = open("/dev/mouse", O_RDONLY);
		kfd = open("/dev/kbd", O_RDONLY);
		amfd = open("/dev/absmouse", O_RDONLY);
		if (amfd < 0) {
			amfd = open("/dev/vmmouse", O_RDONLY);
			vmmouse = 1;
		}
		yg->vbox_rects = open("/dev/vboxrects", O_WRONLY);
		yg->vbox_pointer = open("/dev/vboxpointer", O_WRONLY);

		fds[1] = mfd;
		fds[2] = kfd;
		fds[3] = amfd;
	}

	uint64_t last_redraw = 0;

	while (1) {

		unsigned long frameTime = yutani_time_since(yg, last_redraw);
		if (frameTime > 15) {
			redraw_windows(yg);
			last_redraw = yutani_current_time(yg);
			frameTime = 0;
		}

		if (yutani_options.nested) {
			int index = fswait2(2, fds, 16 - frameTime);

			if (index == 1) {
				yutani_msg_t * m = yutani_poll(yg->host_context);
				if (m) {
					switch (m->type) {
						case YUTANI_MSG_KEY_EVENT:
							{
								struct yutani_msg_key_event * ke = (void*)m->data;
								yutani_msg_buildx_key_event_alloc(m_);
								yutani_msg_buildx_key_event(m_, 0, &ke->event, &ke->state);
								handle_key_event(yg, (struct yutani_msg_key_event *)m_->data);
							}
							break;
						case YUTANI_MSG_WINDOW_MOUSE_EVENT:
							{
								struct yutani_msg_window_mouse_event * me = (void*)m->data;
								mouse_device_packet_t packet;

								packet.buttons = me->buttons;
								packet.x_difference = me->new_x;
								packet.y_difference = me->new_y;

								yg->last_mouse_buttons = packet.buttons;

								yutani_msg_buildx_mouse_event_alloc(m_);
								yutani_msg_buildx_mouse_event(m_, 0, &packet, YUTANI_MOUSE_EVENT_TYPE_ABSOLUTE);
								handle_mouse_event(yg, (struct yutani_msg_mouse_event *)m_->data);
							}
							break;
						case YUTANI_MSG_RESIZE_OFFER:
							{
								struct yutani_msg_window_resize * wr = (void*)m->data;
								TRACE("Resize request from host compositor for size %dx%d", wr->width, wr->height);
								yutani_window_resize_accept(yg->host_context, yg->host_window, wr->width, wr->height);
								yg->resize_on_next = 1;
							}
							break;
						case YUTANI_MSG_WINDOW_CLOSE:
						case YUTANI_MSG_SESSION_END:
							{
								TRACE("Host session ended. Should exit.");
								yutani_msg_buildx_session_end_alloc(response);
								yutani_msg_buildx_session_end(response);
								pex_broadcast(server, response->size, (char *)response);
								yg->server = NULL;
								exit(0);
							}
							break;
						default:
							break;
					}
				}
				free(m);
				continue;
			} else if (index > 0) {
				continue;
			}
		} else {
			int index = fswait2(amfd == -1 ? 3 : 4, fds, 16 - frameTime);

			if (index == 2) {
				unsigned char buf[1];
				int r = read(kfd, buf, 1);
				if (r > 0) {
					if (kbd_scancode(&state, buf[0], &event)) {
						yutani_msg_buildx_key_event_alloc(m);
						yutani_msg_buildx_key_event(m,0, &event, &state);
						handle_key_event(yg, (struct yutani_msg_key_event *)m->data);
					}
				}
				continue;
			} else if (index == 1) {
				int r = read(mfd, (char *)&packet, sizeof(mouse_device_packet_t));
				if (r > 0) {
					yg->last_mouse_buttons = packet.buttons;
					yutani_msg_buildx_mouse_event_alloc(m);
					yutani_msg_buildx_mouse_event(m,0, &packet, YUTANI_MOUSE_EVENT_TYPE_RELATIVE);
					handle_mouse_event(yg, (struct yutani_msg_mouse_event *)m->data);
				}
				continue;
			} else if (amfd != -1 && index == 3) {
				int r = read(amfd, (char *)&packet, sizeof(mouse_device_packet_t));
				if (r > 0) {
					if (!vmmouse) {
						packet.buttons = yg->last_mouse_buttons & 0xF;
					} else {
						yg->last_mouse_buttons = packet.buttons;
					}
					yutani_msg_buildx_mouse_event_alloc(m);
					yutani_msg_buildx_mouse_event(m,0, &packet, YUTANI_MOUSE_EVENT_TYPE_ABSOLUTE);
					handle_mouse_event(yg, (struct yutani_msg_mouse_event *)m->data);
				}
				continue;
			} else if (index > 0) {
				continue;
			}
		}

		pex_packet_t * p = calloc(PACKET_SIZE, 1);
		pex_listen(server, p);

		yutani_msg_t * m = (yutani_msg_t *)p->data;

		if (p->size == 0) {
			/* Connection closed for client */
			TRACE("Connection closed for client  %x", p->source);

			list_t * client_list = hashmap_get(yg->clients_to_windows, (void *)p->source);
			if (client_list) {
				foreach(node, client_list) {
					yutani_server_window_t * win = node->value;
					TRACE("Killing window %d", win->wid);
					window_mark_for_close(yg, win);
				}
				hashmap_remove(yg->clients_to_windows, (void *)p->source);
				list_free(client_list);
				free(client_list);
			}

			if (hashmap_is_empty(yg->clients_to_windows)) {
				TRACE("Last compositor client disconnected, exiting.");
				yg->server = NULL;
				exit(0);
			}

			free(p);
			continue;
		}

		if (m->magic != YUTANI_MSG__MAGIC) {
			TRACE("Message has bad magic. (Should eject client, but will instead skip this message.) 0x%x", m->magic);
			free(p);
			continue;
		}

		switch(m->type) {
			case YUTANI_MSG_HELLO:
				{
					TRACE("And hello to you, %p!", p->source);
					list_t * client_list = hashmap_get(yg->clients_to_windows, (void *)p->source);
					if (!client_list) {
						TRACE("Client is new: %p", p->source);
						client_list = list_create();
						hashmap_set(yg->clients_to_windows, (void *)p->source, client_list);
					}
					yutani_msg_buildx_welcome_alloc(response);
					yutani_msg_buildx_welcome(response,yg->width, yg->height);
					pex_send(server, p->source, response->size, (char *)response);
				}
				break;
			case YUTANI_MSG_WINDOW_NEW:
			case YUTANI_MSG_WINDOW_NEW_FLAGS:
				{
					struct yutani_msg_window_new_flags * wn = (void *)m->data;
					TRACE("Client %p requested a new window (%dx%d).", p->source, wn->width, wn->height);
					yutani_server_window_t * w = server_window_create(yg, wn->width, wn->height, p->source, m->type != YUTANI_MSG_WINDOW_NEW ? wn->flags : 0);
					yutani_msg_buildx_window_init_alloc(response);
					yutani_msg_buildx_window_init(response,w->wid, w->width, w->height, w->bufid);
					pex_send(server, p->source, response->size, (char *)response);

					if (!(w->server_flags & YUTANI_WINDOW_FLAG_NO_STEAL_FOCUS)) {
						set_focused_window(yg, w);
					}

					notify_subscribers(yg);
				}
				break;
			case YUTANI_MSG_FLIP:
				{
					struct yutani_msg_flip * wf = (void *)m->data;
					yutani_server_window_t * w = hashmap_get(yg->wids_to_windows, (void *)(uintptr_t)wf->wid);
					if (w) {
						window_reveal(yg, w);
						mark_window(yg, w);
					}
				}
				break;
			case YUTANI_MSG_FLIP_REGION:
				{
					struct yutani_msg_flip_region * wf = (void *)m->data;
					yutani_server_window_t * w = hashmap_get(yg->wids_to_windows, (void *)(uintptr_t)wf->wid);
					if (w) {
						window_reveal(yg, w);
						mark_window_relative(yg, w, wf->x, wf->y, wf->width, wf->height);
					}
				}
				break;
			case YUTANI_MSG_KEY_EVENT:
				{
					/* XXX Verify this is from a valid device client */
					struct yutani_msg_key_event * ke = (void *)m->data;
					handle_key_event(yg, ke);
				}
				break;
			case YUTANI_MSG_MOUSE_EVENT:
				{
					/* XXX Verify this is from a valid device client */
					struct yutani_msg_mouse_event * me = (void *)m->data;
					handle_mouse_event(yg, me);
				}
				break;
			case YUTANI_MSG_WINDOW_MOVE:
				{
					struct yutani_msg_window_move * wm = (void *)m->data;
					//TRACE("%08x wanted to move window %d to %d, %d", p->source, wm->wid, (int)wm->x, (int)wm->y);
					if (wm->x > (int)yg->width + 100 || wm->x < -(int)yg->width || wm->y > (int)yg->height + 100 || wm->y < -(int)yg->height) {
						TRACE("Refusing to move window to these coordinates.");
						break;
					}
					yutani_server_window_t * win = hashmap_get(yg->wids_to_windows, (void*)(uintptr_t)wm->wid);
					if (win) {
						window_move(yg, win, wm->x, wm->y);
					} else {
						TRACE("%08x wanted to move window %d, but I can't find it?", p->source, wm->wid);
					}
				}
				break;
			case YUTANI_MSG_WINDOW_MOVE_RELATIVE:
				{
					struct yutani_msg_window_move_relative * wm = (void *)m->data;

					yutani_server_window_t * movee = hashmap_get(yg->wids_to_windows, (void*)(uintptr_t)wm->wid_to_move);
					yutani_server_window_t * base  = hashmap_get(yg->wids_to_windows, (void*)(uintptr_t)wm->wid_base);

					if (!movee || !base) break;

					/* Map coordinate to new origin location */
					int32_t nx, ny;
					yutani_window_to_device(base, wm->x + movee->width / 2, wm->y + movee->height / 2, &nx, &ny);
					window_move(yg, movee, nx - movee->width / 2, ny - movee->height / 2);

					/* Match window rotation to base window */
					movee->rotation = base->rotation;
				}
				break;
			case YUTANI_MSG_WINDOW_CLOSE:
				{
					struct yutani_msg_window_close * wc = (void *)m->data;
					yutani_server_window_t * w = hashmap_get(yg->wids_to_windows, (void *)(uintptr_t)wc->wid);
					if (w) {
						window_mark_for_close(yg, w);
						window_remove_from_client(yg, w);
					}
				}
				break;
			case YUTANI_MSG_WINDOW_STACK:
				{
					struct yutani_msg_window_stack * ws = (void *)m->data;
					yutani_server_window_t * w = hashmap_get(yg->wids_to_windows, (void *)(uintptr_t)ws->wid);
					if (w) {
						reorder_window(yg, w, ws->z);
					}
				}
				break;
			case YUTANI_MSG_RESIZE_REQUEST:
				{
					struct yutani_msg_window_resize * wr = (void *)m->data;
					yutani_server_window_t * w = hashmap_get(yg->wids_to_windows, (void *)(uintptr_t)wr->wid);
					if (w) {
						yutani_msg_buildx_window_resize_alloc(response);
						yutani_msg_buildx_window_resize(response,YUTANI_MSG_RESIZE_OFFER, w->wid, wr->width, wr->height, 0, w->tiled);
						pex_send(server, p->source, response->size, (char *)response);
					}
				}
				break;
			case YUTANI_MSG_RESIZE_OFFER:
				{
					struct yutani_msg_window_resize * wr = (void *)m->data;
					yutani_server_window_t * w = hashmap_get(yg->wids_to_windows, (void *)(uintptr_t)wr->wid);
					if (w) {
						yutani_msg_buildx_window_resize_alloc(response);
						yutani_msg_buildx_window_resize(response,YUTANI_MSG_RESIZE_OFFER, w->wid, wr->width, wr->height, 0, w->tiled);
						pex_send(server, p->source, response->size, (char *)response);
					}
				}
				break;
			case YUTANI_MSG_RESIZE_ACCEPT:
				{
					struct yutani_msg_window_resize * wr = (void *)m->data;
					yutani_server_window_t * w = hashmap_get(yg->wids_to_windows, (void *)(uintptr_t)wr->wid);
					if (w) {
						uint32_t newbufid = server_window_resize(yg, w, wr->width, wr->height);
						yutani_msg_buildx_window_resize_alloc(response);
						yutani_msg_buildx_window_resize(response,YUTANI_MSG_RESIZE_BUFID, w->wid, wr->width, wr->height, newbufid, 0);
						pex_send(server, p->source, response->size, (char *)response);
					}
				}
				break;
			case YUTANI_MSG_RESIZE_DONE:
				{
					struct yutani_msg_window_resize * wr = (void *)m->data;
					yutani_server_window_t * w = hashmap_get(yg->wids_to_windows, (void *)(uintptr_t)wr->wid);
					if (w) {
						server_window_resize_finish(yg, w, wr->width, wr->height);
						notify_subscribers(yg);
					}
				}
				break;
			case YUTANI_MSG_QUERY_WINDOWS:
				{
					yutani_query_result(yg, p->source, yg->bottom_z);
					foreach (node, yg->mid_zs) {
						yutani_query_result(yg, p->source, node->value);
					}
					/* Exclude menus, overlay windows, top and bottom. */
					yutani_query_result(yg, p->source, yg->top_z);
					yutani_msg_buildx_window_advertise_alloc(response, 0);
					yutani_msg_buildx_window_advertise(response,0, 0, 0, 0, 0, 0, 0, NULL);
					pex_send(server, p->source, response->size, (char *)response);
				}
				break;
			case YUTANI_MSG_SUBSCRIBE:
				{
					foreach(node, yg->window_subscribers) {
						if ((uintptr_t)node->value == p->source) {
							break;
						}
					}
					list_insert(yg->window_subscribers, (void*)p->source);
				}
				break;
			case YUTANI_MSG_UNSUBSCRIBE:
				{
					node_t * node = list_find(yg->window_subscribers, (void*)p->source);
					if (node) {
						list_delete(yg->window_subscribers, node);
					}
				}
				break;
			case YUTANI_MSG_WINDOW_ADVERTISE:
				{
					struct yutani_msg_window_advertise * wa = (void *)m->data;
					yutani_server_window_t * w = hashmap_get(yg->wids_to_windows, (void *)(uintptr_t)wa->wid);
					if (w) {
						if (w->client_strings) free(w->client_strings);

						w->client_icon    = wa->icon;
						w->client_flags   = wa->flags;
						w->client_length  = wa->size;
						w->client_strings = malloc(wa->size);
						memcpy(w->client_strings, wa->strings, wa->size);

						notify_subscribers(yg);
					}
				}
				break;
			case YUTANI_MSG_SESSION_END:
				{
					yutani_msg_buildx_session_end_alloc(response);
					yutani_msg_buildx_session_end(response);
					pex_broadcast(server, response->size, (char *)response);
				}
				break;
			case YUTANI_MSG_WINDOW_FOCUS:
				{
					struct yutani_msg_window_focus * wa = (void *)m->data;
					yutani_server_window_t * w = hashmap_get(yg->wids_to_windows, (void *)(uintptr_t)wa->wid);
					if (w) {
						set_focused_window(yg, w);
					}
				}
				break;
			case YUTANI_MSG_KEY_BIND:
				{
					struct yutani_msg_key_bind * wa = (void *)m->data;
					add_key_bind(yg, wa, p->source);
				}
				break;
			case YUTANI_MSG_WINDOW_DRAG_START:
				{
					struct yutani_msg_window_drag_start * wa = (void *)m->data;
					yutani_server_window_t * w = hashmap_get(yg->wids_to_windows, (void *)(uintptr_t)wa->wid);
					if (w) {
						/* Start dragging */
						mouse_start_drag(yg, w);
					}
				}
				break;
			case YUTANI_MSG_WINDOW_UPDATE_SHAPE:
				{
					struct yutani_msg_window_update_shape * wa = (void *)m->data;
					yutani_server_window_t * w = hashmap_get(yg->wids_to_windows, (void *)(uintptr_t)wa->wid);
					if (w) {
						/* Set shape parameter */
						server_window_update_shape(yg, w, wa->set_shape);
					}
				}
				break;
			case YUTANI_MSG_WINDOW_WARP_MOUSE:
				{
					struct yutani_msg_window_warp_mouse * wa = (void *)m->data;
					yutani_server_window_t * w = hashmap_get(yg->wids_to_windows, (void *)(uintptr_t)wa->wid);
					if (w) {
						if (yg->focused_window == w) {
							int32_t x, y;
							yutani_window_to_device(w, wa->x, wa->y, &x, &y);

							struct yutani_msg_mouse_event me;
							me.event.x_difference = x;
							me.event.y_difference = y;
							me.event.buttons = yg->last_mouse_buttons;
							me.type = YUTANI_MOUSE_EVENT_TYPE_ABSOLUTE;
							me.wid = wa->wid;

							handle_mouse_event(yg, &me);
						}
					}
				}
				break;
			case YUTANI_MSG_WINDOW_SHOW_MOUSE:
				{
					struct yutani_msg_window_show_mouse * wa = (void *)m->data;
					yutani_server_window_t * w = hashmap_get(yg->wids_to_windows, (void *)(uintptr_t)wa->wid);
					if (w) {
						if (wa->show_mouse == -1) {
							w->show_mouse = w->default_mouse;
						} else if (wa->show_mouse < 2) {
							w->default_mouse = wa->show_mouse;
							w->show_mouse = wa->show_mouse;
						} else {
							w->show_mouse = wa->show_mouse;
						}
						if (yg->focused_window == w) {
							mark_screen(yg, yg->mouse_x / MOUSE_SCALE - MOUSE_OFFSET_X, yg->mouse_y / MOUSE_SCALE - MOUSE_OFFSET_Y, MOUSE_WIDTH, MOUSE_HEIGHT);
						}
					}
				}
				break;
			case YUTANI_MSG_WINDOW_RESIZE_START:
				{
					struct yutani_msg_window_resize_start * wa = (void *)m->data;
					yutani_server_window_t * w = hashmap_get(yg->wids_to_windows, (void *)(uintptr_t)wa->wid);
					if (w) {
						if (yg->focused_window == w && !yg->resizing_window) {
							yg->resizing_window = w;
							yg->resizing_button = YUTANI_MOUSE_BUTTON_LEFT; /* XXX Uh, what if we used something else */
							mouse_start_resize(yg, wa->direction);
						}
					}
				}
				break;
			case YUTANI_MSG_SPECIAL_REQUEST:
				{
					struct yutani_msg_special_request * sr = (void *)m->data;
					yutani_server_window_t * w = hashmap_get(yg->wids_to_windows, (void *)(uintptr_t)sr->wid);
					switch (sr->request) {
						case YUTANI_SPECIAL_REQUEST_MAXIMIZE:
							if (w) {
								if (w->tiled) {
									window_untile(yg,w);
									window_move(yg,w,w->untiled_left,w->untiled_top);
								} else {
									window_tile(yg, w, 1, 1, 0, 0);
								}
							}
							break;
						case YUTANI_SPECIAL_REQUEST_PLEASE_CLOSE:
							if (w) {
								yutani_msg_buildx_window_close_alloc(response);
								yutani_msg_buildx_window_close(response, w->wid);
								pex_send(yg->server, w->owner, response->size, (char *)response);
							}
							break;
						case YUTANI_SPECIAL_REQUEST_CLIPBOARD:
							{
								yutani_msg_buildx_clipboard_alloc(response, yg->clipboard_size);
								yutani_msg_buildx_clipboard(response, yg->clipboard);
								pex_send(server, p->source, response->size, (char *)response);
							}
							break;
						default:
							TRACE("Unknown special request type: 0x%x", sr->request);
							break;
					}

				}
				break;
			case YUTANI_MSG_CLIPBOARD:
				{
					struct yutani_msg_clipboard * cb = (void *)m->data;
					yg->clipboard_size = min(cb->size, 511);
					memcpy(yg->clipboard, cb->content, yg->clipboard_size);
					yg->clipboard[yg->clipboard_size] = '\0';
					TRACE("Copied text to clipbard (size=%d)", yg->clipboard_size);
				}
				break;
			default:
				{
					TRACE("Unknown type: 0x%8x", m->type);
				}
				break;
		}
		free(p);
	}

	return 0;
}
