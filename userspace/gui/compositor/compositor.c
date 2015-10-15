/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2014 Kevin Lange
 */
#include <stdio.h>
#include <stdint.h>
#include <syscall.h>
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

#include <cairo.h>

#include "lib/graphics.h"
#include "lib/pthread.h"
#include "lib/mouse.h"
#include "lib/kbd.h"
#include "lib/pex.h"
#include "lib/yutani.h"
#include "lib/hashmap.h"
#include "lib/list.h"
#include "lib/spinlock.h"

#include "lib/trace.h"
#define TRACE_APP_NAME "yutani"

#include "yutani_int.h"

#define YUTANI_DEBUG_WINDOW_BOUNDS 1
#define YUTANI_DEBUG_WINDOW_SHAPES 1
#define YUTANI_RESIZE_RIGHT 0
#define YUTANI_INCOMING_MOUSE_SCALE * 3

#define MOUSE_WIDTH 64
#define MOUSE_HEIGHT 64

struct {
	int nested;
	int nest_width;
	int nest_height;
} yutani_options = {
	.nested = 0,
	.nest_width = 640,
	.nest_height = 480,
};

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
		{"nest",       no_argument,       0, 'n'},
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

int32_t min(int32_t a, int32_t b) {
	return (a < b) ? a : b;
}

int32_t max(int32_t a, int32_t b) {
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

static uint32_t yutani_current_time(yutani_globals_t * yg) {
	struct timeval t;
	gettimeofday(&t, NULL);

	uint32_t sec_diff = t.tv_sec - yg->start_time;
	uint32_t usec_diff = t.tv_usec - yg->start_subtime;

	if (t.tv_usec < yg->start_subtime) {
		sec_diff -= 1;
		usec_diff = (1000000 + t.tv_usec) - yg->start_subtime;
	}

	return (uint32_t)(sec_diff * 1000 + usec_diff / 1000);
}

static uint32_t yutani_time_since(yutani_globals_t * yg, uint32_t start_time) {

	uint32_t now = yutani_current_time(yg);
	uint32_t diff = now - start_time; /* Milliseconds */

	return diff;
}

/**
 * Translate and transform coordinate from screen-relative to window-relative.
 */
static void device_to_window(yutani_server_window_t * window, int32_t x, int32_t y, int32_t * out_x, int32_t * out_y) {
	if (!window) return;
	*out_x = x - window->x;
	*out_y = y - window->y;

	if (!window->rotation) return;

	double t_x = *out_x - (window->width / 2);
	double t_y = *out_y - (window->height / 2);

	double s = sin(-M_PI * (window->rotation/ 180.0));
	double c = cos(-M_PI * (window->rotation/ 180.0));

	double n_x = t_x * c - t_y * s;
	double n_y = t_x * s + t_y * c;

	*out_x = (int32_t)n_x + (window->width / 2);
	*out_y = (int32_t)n_y + (window->height / 2);
}

/**
 * Translate and transform coordinate from window-relative to screen-relative.
 */
static void window_to_device(yutani_server_window_t * window, int32_t x, int32_t y, int32_t * out_x, int32_t * out_y) {

	if (!window->rotation) {
		*out_x = window->x + x;
		*out_y = window->y + y;
		return;
	}

	double t_x = x - (window->width / 2);
	double t_y = y - (window->height / 2);

	double s = sin(M_PI * (window->rotation/ 180.0));
	double c = cos(M_PI * (window->rotation/ 180.0));

	double n_x = t_x * c - t_y * s;
	double n_y = t_x * s + t_y * c;

	*out_x = (int32_t)n_x + (window->width / 2) + window->x;
	*out_y = (int32_t)n_y + (window->height / 2) + window->y;
}

/**
 * Remove a window from the z stack.
 */
static void unorder_window(yutani_globals_t * yg, yutani_server_window_t * w) {
	unsigned short index = w->z;
	w->z = -1;
	if (index == YUTANI_ZORDER_BOTTOM) {
		yg->bottom_z = NULL;
		return;
	}
	if (index == YUTANI_ZORDER_TOP) {
		yg->top_z = NULL;
		return;
	}

	node_t * n = list_find(yg->mid_zs, w);
	if (!n) return;
	list_delete(yg->mid_zs, n);
	free(n);
}

/**
 * Move a window to a new stack order.
 */
static void reorder_window(yutani_globals_t * yg, yutani_server_window_t * window, uint16_t new_zed) {
	if (!window) {
		return;
	}

	int z = window->z;
	spin_lock(&yg->redraw_lock);
	unorder_window(yg, window);
	spin_unlock(&yg->redraw_lock);

	window->z = new_zed;

	if (new_zed != YUTANI_ZORDER_TOP && new_zed != YUTANI_ZORDER_BOTTOM) {
		spin_lock(&yg->redraw_lock);
		list_insert(yg->mid_zs, window);
		spin_unlock(&yg->redraw_lock);
		return;
	}

	if (new_zed == YUTANI_ZORDER_TOP) {
		if (yg->top_z) {
			spin_lock(&yg->redraw_lock);
			unorder_window(yg, yg->top_z);
			spin_unlock(&yg->redraw_lock);
		}
		yg->top_z = window;
		return;
	}

	if (new_zed == YUTANI_ZORDER_BOTTOM) {
		if (yg->bottom_z) {
			spin_lock(&yg->redraw_lock);
			unorder_window(yg, yg->bottom_z);
			spin_unlock(&yg->redraw_lock);
		}
		yg->bottom_z = window;
		return;
	}
}

/**
 * Move a window to the top of the basic z stack, if valid.
 */
static void make_top(yutani_globals_t * yg, yutani_server_window_t * w) {
	unsigned short index = w->z;

	if (index == YUTANI_ZORDER_BOTTOM) return;
	if (index == YUTANI_ZORDER_TOP) return;

	node_t * n = list_find(yg->mid_zs, w);
	if (!n) return; /* wat */

	list_delete(yg->mid_zs, n);
	list_append(yg->mid_zs, n);
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
		yutani_msg_t * response = yutani_msg_build_window_focus_change(yg->focused_window->wid, 0);
		pex_send(yg->server, yg->focused_window->owner, response->size, (char *)response);
		free(response);
	}
	yg->focused_window = w;
	if (w) {
		/* Send focus change to new focused window */
		yutani_msg_t * response = yutani_msg_build_window_focus_change(w->wid, 1);
		pex_send(yg->server, w->owner, response->size, (char *)response);
		free(response);
		make_top(yg, w);
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

/**
 * Create a server window object.
 *
 * Initializes a window of the particular size for a given client.
 */
static yutani_server_window_t * server_window_create(yutani_globals_t * yg, int width, int height, uint32_t owner) {
	yutani_server_window_t * win = malloc(sizeof(yutani_server_window_t));

	win->wid = next_wid();
	win->owner = owner;
	list_insert(yg->windows, win);
	hashmap_set(yg->wids_to_windows, (void*)win->wid, win);

	list_t * client_list = hashmap_get(yg->clients_to_windows, (void *)owner);
	if (!client_list) {
		TRACE("Window creation from new client: %x", owner);
		client_list = list_create();
		hashmap_set(yg->clients_to_windows, (void *)owner, client_list);
	}
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
	win->client_offsets[0] = 0;
	win->client_offsets[1] = 0;
	win->client_offsets[2] = 0;
	win->client_offsets[3] = 0;
	win->client_offsets[4] = 0;
	win->client_length  = 0;
	win->client_strings = NULL;
	win->anim_mode = YUTANI_EFFECT_FADE_IN;
	win->anim_start = yutani_current_time(yg);
	win->alpha_threshold = 0;
	win->show_mouse = 1;
	win->tiled = 0;
	win->untiled_width = 0;
	win->untiled_height = 0;
	win->default_mouse = 1;

	char key[1024];
	YUTANI_SHMKEY(yg->server_ident, key, 1024, win);

	size_t size = (width * height * 4);

	win->buffer = (uint8_t *)syscall_shm_obtain(key, &size);
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
		win->newbuffer = (uint8_t *)syscall_shm_obtain(key, &size);
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

	spin_lock(&yg->redraw_lock);

	win->width = width;
	win->height = height;

	win->bufid = win->newbufid;
	win->buffer = win->newbuffer;

	win->newbuffer = NULL;
	win->newbufid = 0;

	{
		char key[1024];
		YUTANI_SHMKEY_EXP(yg->server_ident, key, 1024, oldbufid);
		syscall_shm_release(key);
	}

	spin_unlock(&yg->redraw_lock);

	mark_window(yg, win);
}

/**
 * Nested Yutani input thread
 *
 * Handles keyboard and mouse events, as well as
 * other Yutani events from the nested window.
 */
void * nested_input(void * _yg) {

	yutani_globals_t * yg = _yg;

	yutani_t * y = yutani_init();

	while (1) {
		yutani_msg_t * m = yutani_poll(yg->host_context);
		if (m) {
			switch (m->type) {
				case YUTANI_MSG_KEY_EVENT:
					{
						struct yutani_msg_key_event * ke = (void*)m->data;
						yutani_msg_t * m_ = yutani_msg_build_key_event(0, &ke->event, &ke->state);
						int result = yutani_msg_send(y, m_);
						free(m_);
					}
					break;
				case YUTANI_MSG_WINDOW_MOUSE_EVENT:
					{
						struct yutani_msg_window_mouse_event * me = (void*)m->data;
						mouse_device_packet_t packet;

						packet.buttons = me->buttons;
						packet.x_difference = me->new_x;
						packet.y_difference = me->new_y;

						yutani_msg_t * m_ = yutani_msg_build_mouse_event(0, &packet, YUTANI_MOUSE_EVENT_TYPE_ABSOLUTE);
						int result = yutani_msg_send(y, m_);
						free(m_);
					}
					break;
				case YUTANI_MSG_SESSION_END:
					TRACE("Host session ended. Should exit.");
					break;
				default:
					break;
			}
		}
		free(m);
	}
}

/**
 * Mouse input thread
 *
 * Reads the kernel mouse device and converts
 * mouse clicks and movements into event objects
 * to send to the core compositor.
 */
void * mouse_input(void * garbage) {
	int mfd = open("/dev/mouse", O_RDONLY);

	yutani_t * y = yutani_init();
	mouse_device_packet_t packet;

	while (1) {
		int r = read(mfd, (char *)&packet, sizeof(mouse_device_packet_t));
		if (r > 0) {
			yutani_msg_t * m = yutani_msg_build_mouse_event(0, &packet, YUTANI_MOUSE_EVENT_TYPE_RELATIVE);
			int result = yutani_msg_send(y, m);
			free(m);
		}
	}
}

/**
 * Keyboard input thread
 *
 * Reads the kernel keyboard device and converts
 * key presses into event objects to send to the
 * core compositor.
 */
void * keyboard_input(void * garbage) {
	int kfd = open("/dev/kbd", O_RDONLY);

	yutani_t * y = yutani_init();
	key_event_t event;
	key_event_state_t state = {0};

	while (1) {
		char buf[1];
		int r = read(kfd, buf, 1);
		if (r > 0) {
			kbd_scancode(&state, buf[0], &event);
			yutani_msg_t * m = yutani_msg_build_key_event(0, &event, &state);
			int result = yutani_msg_send(y, m);
			free(m);
		}
	}
}

#define FONT_PATH "/usr/share/fonts/"
#define FONT(a,b) {a, FONT_PATH b}

struct font_def {
	char * identifier;
	char * path;
};

static struct font_def fonts[] = {
	FONT("sans-serif",            "DejaVuSans.ttf"),
	FONT("sans-serif.bold",       "DejaVuSans-Bold.ttf"),
	FONT("sans-serif.italic",     "DejaVuSans-Oblique.ttf"),
	FONT("sans-serif.bolditalic", "DejaVuSans-BoldOblique.ttf"),
	FONT("monospace",             "DejaVuSansMono.ttf"),
	FONT("monospace.bold",        "DejaVuSansMono-Bold.ttf"),
	FONT("monospace.italic",      "DejaVuSansMono-Oblique.ttf"),
	FONT("monospace.bolditalic",  "DejaVuSansMono-BoldOblique.ttf"),
	{NULL, NULL}
};

/**
 * Preload a font into the font cache.
 *
 * TODO This should probably be moved out of the compositor,
 *      perhaps into a generic resource cache daemon. This
 *      is mostly kept this way for legacy reasons - the old
 *      compositor did it, but it was also using some of the
 *      fonts for internal rendering. We don't draw any text.
 */
static char * precache_shmfont(char * ident, char * name) {
	FILE * f = fopen(name, "r");
	size_t s = 0;
	fseek(f, 0, SEEK_END);
	s = ftell(f);
	fseek(f, 0, SEEK_SET);

	size_t shm_size = s;
	char * font = (char *)syscall_shm_obtain(ident, &shm_size);
	assert((shm_size >= s) && "shm_obtain returned too little memory to load a font into!");

	fread(font, s, 1, f);

	fclose(f);
	return font;
}

/**
 * Load all of the fonts into the cache.
 */
static void load_fonts(yutani_globals_t * yg) {
	int i = 0;
	while (fonts[i].identifier) {
		char tmp[100];
		snprintf(tmp, 100, "sys.%s.fonts.%s", yg->server_ident, fonts[i].identifier);
		TRACE("Loading font %s -> %s", fonts[i].path, tmp);
		precache_shmfont(tmp, fonts[i].path);
		++i;
	}
}

/**
 * Add a clip region from a rectangle.
 */
static void yutani_add_clip(yutani_globals_t * yg, double x, double y, double w, double h) {
	cairo_rectangle(yg->framebuffer_ctx, x, y, w, h);
	cairo_rectangle(yg->real_ctx, x, y, w, h);
}

/**
 * Save cairo states for the framebuffers to the stack.
 */
static void save_cairo_states(yutani_globals_t * yg) {
	cairo_save(yg->framebuffer_ctx);
	cairo_save(yg->real_ctx);
}

/**
 * Pop previous framebuffer cairo states.
 */
static void restore_cairo_states(yutani_globals_t * yg) {
	cairo_restore(yg->framebuffer_ctx);
	cairo_restore(yg->real_ctx);
}

/**
 * Apply the clips we built earlier.
 */
static void yutani_set_clip(yutani_globals_t * yg) {
	cairo_clip(yg->framebuffer_ctx);
	cairo_clip(yg->real_ctx);
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

	spin_lock(&yg->update_list_lock);
	list_insert(yg->update_list, rect);
	spin_unlock(&yg->update_list_lock);
}

/**
 * Draw the cursor sprite.
 *
 * TODO This should probably use Cairo's PNG functionality, or something
 *      else other than our own rendering tools...
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
		}
	}
	if (sprite != previous) {
		mark_screen(yg, x / MOUSE_SCALE - MOUSE_OFFSET_X, y / MOUSE_SCALE - MOUSE_OFFSET_Y, MOUSE_WIDTH, MOUSE_HEIGHT);
		previous = sprite;
	}
	draw_sprite(yg->backend_ctx, sprite, x / MOUSE_SCALE - MOUSE_OFFSET_X, y / MOUSE_SCALE - MOUSE_OFFSET_Y);
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
yutani_server_window_t * check_top_at(yutani_globals_t * yg, yutani_server_window_t * w, uint16_t x, uint16_t y){
	if (!w) return NULL;
	int32_t _x = -1, _y = -1;
	device_to_window(w, x, y, &_x, &_y);
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
yutani_server_window_t * top_at(yutani_globals_t * yg, uint16_t x, uint16_t y) {
	if (check_top_at(yg, yg->top_z, x, y)) return yg->top_z;
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
static int window_is_top(yutani_globals_t * yg, yutani_server_window_t * window) {
	/* For now, just use simple z-order */
	return window->z == YUTANI_ZORDER_TOP;
}

static int window_is_bottom(yutani_globals_t * yg, yutani_server_window_t * window) {
	/* For now, just use simple z-order */
	return window->z == YUTANI_ZORDER_BOTTOM;
}

/**
 * Get a color for a wid for debugging.
 *
 * Makes a pretty rainbow pattern.
 */
static uint32_t color_for_wid(yutani_wid_t wid) {
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
 * Blit a window to the framebuffer.
 *
 * Applies transformations (rotation, animations) and then renders
 * the window with Cairo.
 */
static int yutani_blit_window(yutani_globals_t * yg, cairo_t * ctx, yutani_server_window_t * window, int x, int y) {

	/* Obtain the previously initialized cairo contexts */
	cairo_t * cr = ctx;

	/* Window stride is always 4 bytes per pixel... */
	int stride = window->width * 4;

	/* Initialize a cairo surface object for this window */
	cairo_surface_t * surf = cairo_image_surface_create_for_data(
			window->buffer, CAIRO_FORMAT_ARGB32, window->width, window->height, stride);

	/* Save cairo context */
	cairo_save(cr);

	/*
	 * Offset the rendering context appropriately for the position of the window
	 * based on the modifier paramters
	 */
	cairo_translate(cr, x, y);

	/* Top and bottom windows can not be rotated. */
	if (!window_is_top(yg, window) && !window_is_bottom(yg, window)) {
		/* Calcuate radians from degrees */

		/* XXX Window rotation is disabled until damage rects can take it into account */
		if (window->rotation != 0) {
			double r = M_PI * (((double)window->rotation) / 180.0);

			/* Rotate the render context about the center of the window */
			cairo_translate(cr, (int)( window->width / 2), (int)( (int)window->height / 2));
			cairo_rotate(cr, r);
			cairo_translate(cr, (int)(-window->width / 2), (int)(-window->height / 2));

			/* Prefer faster filter when rendering rotated windows */
			cairo_pattern_set_filter (cairo_get_source (cr), CAIRO_FILTER_FAST);
		}
	}
	if (window->anim_mode) {
		int frame = yutani_time_since(yg, window->anim_start);
		if (frame >= yutani_animation_lengths[window->anim_mode]) {
			/* XXX handle animation-end things like cleanup of closing windows */
			if (window->anim_mode == YUTANI_EFFECT_FADE_OUT) {
				list_insert(yg->windows_to_remove, window);
				goto draw_finish;
			}
			window->anim_mode = 0;
			window->anim_start = 0;
			goto draw_window;
		} else {
			switch (window->anim_mode) {
				case YUTANI_EFFECT_FADE_OUT:
					{
						frame = yutani_animation_lengths[window->anim_mode] - frame;
					}
				case YUTANI_EFFECT_FADE_IN:
					{
						double time_diff = ((double)frame / (float)yutani_animation_lengths[window->anim_mode]);
						double x = 0.75 + time_diff * 0.25;
						int t_x = (window->width * (1.0 - x)) / 2;
						int t_y = (window->height * (1.0 - x)) / 2;

						if (!window_is_top(yg, window) && !window_is_bottom(yg, window)) {
							cairo_translate(cr, t_x, t_y);
							cairo_scale(cr, x, x);
						}

						cairo_set_source_surface(cr, surf, 0, 0);
						cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_FAST);
						cairo_paint_with_alpha(cr, time_diff);
					}
					break;
				default:
					goto draw_window;
					break;
			}
		}
	} else {
draw_window:
		/* Paint window */
		cairo_set_source_surface(cr, surf, 0, 0);
		cairo_paint(cr);
	}

draw_finish:

	/* Clean up */
	cairo_surface_destroy(surf);


	/* Restore context stack */
	cairo_restore(cr);

#if YUTANI_DEBUG_WINDOW_BOUNDS
	/*
	 * If window bound debugging is enabled, we also draw a box
	 * representing the rectangular (possibly rotated) boundary
	 * for a window texture.
	 */
	if (yg->debug_bounds) {
		cairo_save(cr);

		int32_t t_x, t_y;
		int32_t s_x, s_y;
		int32_t r_x, r_y;
		int32_t q_x, q_y;

		window_to_device(window, 0, 0, &t_x, &t_y);
		window_to_device(window, window->width, window->height, &s_x, &s_y);
		window_to_device(window, 0, window->height, &r_x, &r_y);
		window_to_device(window, window->width, 0, &q_x, &q_y);

		uint32_t x = color_for_wid(window->wid);
		cairo_set_source_rgba(cr,
				_RED(x) / 255.0,
				_GRE(x) / 255.0,
				_BLU(x) / 255.0,
				0.7
		);

		cairo_move_to(cr, t_x, t_y);
		cairo_line_to(cr, r_x, r_y);
		cairo_line_to(cr, s_x, s_y);
		cairo_line_to(cr, q_x, q_y);
		cairo_fill(cr);

		cairo_restore(cr);
	}
#endif

	return 0;
}

/**
 * Draw the bounding box for a resizing window.
 *
 * This also takes into account rotation of the window.
 */
static void draw_resizing_box(yutani_globals_t * yg) {
	cairo_t * cr = yg->framebuffer_ctx;
	cairo_save(cr);

	int32_t t_x, t_y;
	int32_t s_x, s_y;
	int32_t r_x, r_y;
	int32_t q_x, q_y;

	window_to_device(yg->resizing_window, yg->resizing_offset_x, yg->resizing_offset_y, &t_x, &t_y);
	window_to_device(yg->resizing_window, yg->resizing_offset_x + yg->resizing_w, yg->resizing_offset_y + yg->resizing_h, &s_x, &s_y);
	window_to_device(yg->resizing_window, yg->resizing_offset_x, yg->resizing_offset_y + yg->resizing_h, &r_x, &r_y);
	window_to_device(yg->resizing_window, yg->resizing_offset_x + yg->resizing_w, yg->resizing_offset_y, &q_x, &q_y);
	cairo_set_line_width(cr, 2.0);

	cairo_move_to(cr, t_x, t_y);
	cairo_line_to(cr, q_x, q_y);
	cairo_line_to(cr, s_x, s_y);
	cairo_line_to(cr, r_x, r_y);
	cairo_line_to(cr, t_x, t_y);
	cairo_close_path(cr);
	cairo_stroke_preserve(cr);
	cairo_set_source_rgba(cr, 0.33, 0.55, 1.0, 0.5);
	cairo_fill(cr);
	cairo_set_source_rgba(cr, 0.0, 0.4, 1.0, 0.9);
	cairo_stroke(cr);

	cairo_restore(cr);

}

/**
 * Blit all windows into the given context.
 *
 * This is called for rendering and for screenshots.
 */
static void yutani_blit_windows(yutani_globals_t * yg, cairo_t * ctx) {
	if (yg->bottom_z) yutani_blit_window(yg, ctx, yg->bottom_z, yg->bottom_z->x, yg->bottom_z->y);
	foreach (node, yg->mid_zs) {
		yutani_server_window_t * w = node->value;
		if (w) yutani_blit_window(yg, ctx, w, w->x, w->y);
	}
	if (yg->top_z) yutani_blit_window(yg, ctx, yg->top_z, yg->top_z->x, yg->top_z->y);
}

/**
 * Take a screenshot
 */
static void yutani_screenshot(yutani_globals_t * yg) {
	int target_width;
	int target_height;
	void * target_data;

	switch (yg->screenshot_frame) {
		case YUTANI_SCREENSHOT_FULL:
			target_width = yg->width;
			target_height = yg->height;
			target_data = yg->backend_framebuffer;
			break;
		case YUTANI_SCREENSHOT_WINDOW:
			if (!yg->focused_window) goto screenshot_done;
			target_width = yg->focused_window->width;
			target_height = yg->focused_window->height;
			target_data = yg->focused_window->buffer;
			break;
		default:
			/* ??? */
			goto screenshot_done;
	}

	cairo_surface_t * s = cairo_image_surface_create_for_data(target_data, CAIRO_FORMAT_ARGB32, target_width, target_height, target_width * 4);

	cairo_surface_write_to_png(s, "/tmp/screenshot.png");

	cairo_surface_destroy(s);

screenshot_done:
	yg->screenshot_frame = 0;
}

/**
 * Redraw all windows, as well as the mouse cursor.
 *
 * This is the main redraw function.
 */
static void redraw_windows(yutani_globals_t * yg) {
	/* Save the cairo contexts so we can apply clipping */
	save_cairo_states(yg);
	int has_updates = 0;

	/* We keep our own temporary mouse coordinates as they may change while we're drawing. */
	int tmp_mouse_x = yg->mouse_x;
	int tmp_mouse_y = yg->mouse_y;

	/* If the mouse has moved, that counts as two damage regions */
	if ((yg->last_mouse_x != tmp_mouse_x) || (yg->last_mouse_y != tmp_mouse_y)) {
		has_updates = 2;
		yutani_add_clip(yg, yg->last_mouse_x / MOUSE_SCALE - MOUSE_OFFSET_X, yg->last_mouse_y / MOUSE_SCALE - MOUSE_OFFSET_Y, MOUSE_WIDTH, MOUSE_HEIGHT);
		yutani_add_clip(yg, tmp_mouse_x / MOUSE_SCALE - MOUSE_OFFSET_X, tmp_mouse_y / MOUSE_SCALE - MOUSE_OFFSET_Y, MOUSE_WIDTH, MOUSE_HEIGHT);
	}

	yg->last_mouse_x = tmp_mouse_x;
	yg->last_mouse_y = tmp_mouse_y;

	if (yg->bottom_z && yg->bottom_z->anim_mode) mark_window(yg, yg->bottom_z);
	if (yg->top_z && yg->top_z->anim_mode) mark_window(yg, yg->top_z);
	foreach (node, yg->mid_zs) {
		yutani_server_window_t * w = node->value;
		if (w && w->anim_mode) mark_window(yg, w);
	}

	/* Calculate damage regions from currently queued updates */
	spin_lock(&yg->update_list_lock);
	while (yg->update_list->length) {
		node_t * win = list_dequeue(yg->update_list);
		yutani_damage_rect_t * rect = (void *)win->value;

		/* We add a clip region for each window in the update queue */
		has_updates = 1;
		yutani_add_clip(yg, rect->x, rect->y, rect->width, rect->height);
		free(rect);
		free(win);
	}
	spin_unlock(&yg->update_list_lock);

	/* Render */
	if (has_updates) {

		yutani_set_clip(yg);

		yg->windows_to_remove = list_create();

		/*
		 * In theory, we should restrict this to windows within the clip region,
		 * but calculating that may be more trouble than it's worth;
		 * we also need to render windows in stacking order...
		 */
		spin_lock(&yg->redraw_lock);
		yutani_blit_windows(yg, yg->framebuffer_ctx);

		if (yg->resizing_window) {
			/* Draw box */
			draw_resizing_box(yg);
		}

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
					if (w) { GFX(yg->backend_ctx, x, y) = color_for_wid(w->wid); }
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
			cairo_set_operator(yg->real_ctx, CAIRO_OPERATOR_SOURCE);
			cairo_translate(yg->real_ctx, 0, 0);
			cairo_set_source_surface(yg->real_ctx, yg->framebuffer_surface, 0, 0);
			cairo_paint(yg->real_ctx);
		}
		spin_unlock(&yg->redraw_lock);

		/*
		 * If any windows were marked for removal,
		 * then remove them.
		 */
		while (yg->windows_to_remove->head) {
			node_t * node = list_pop(yg->windows_to_remove);

			window_actually_close(yg, node->value);

			free(node);
		}
		free(yg->windows_to_remove);

	}

	if (yg->screenshot_frame) {
		yutani_screenshot(yg);
	}

	/* Restore the cairo contexts to reset clip regions */
	restore_cairo_states(yg);
}

/**
 * Initialize cairo contexts and surfaces for the framebuffers.
 */
void yutani_cairo_init(yutani_globals_t * yg) {

	int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, yg->width);
	yg->framebuffer_surface = cairo_image_surface_create_for_data(
			yg->backend_framebuffer, CAIRO_FORMAT_ARGB32, yg->width, yg->height, stride);
	yg->real_surface = cairo_image_surface_create_for_data(
			yg->backend_ctx->buffer, CAIRO_FORMAT_ARGB32, yg->width, yg->height, stride);

	yg->framebuffer_ctx = cairo_create(yg->framebuffer_surface);
	yg->real_ctx = cairo_create(yg->real_surface);

	yg->update_list = list_create();
	yg->update_list_lock = 0;
}

/**
 * Redraw thread.
 *
 * Calls the redraw functions in a loop, with some
 * additional yielding and sleeping.
 */
void * redraw(void * in) {
	yutani_globals_t * yg = in;
	while (1) {
		/*
		 * Perform whatever redraw work is required.
		 */
		redraw_windows(yg);

		/*
		 * Attempt to run at about 60fps...
		 * we should actually see how long it took to render so
		 * we can sleep *less* if it took a long time to render
		 * this particular frame. We are definitely not
		 * going to run at 60fps unless there's nothing to do
		 * (and even then we've wasted cycles checking).
		 */
		usleep(16666);
	}
}

/**
 * Mark a region within a window as damaged.
 *
 * If the window is rotated, we calculate the minimum rectangle that covers
 * the whole region specified and then mark that.
 */
static void mark_window_relative(yutani_globals_t * yg, yutani_server_window_t * window, int32_t x, int32_t y, int32_t width, int32_t height) {
	yutani_damage_rect_t * rect = malloc(sizeof(yutani_damage_rect_t));

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

		window_to_device(window, x, y, &ul_x, &ul_y);
		window_to_device(window, x, y + height, &ll_x, &ll_y);
		window_to_device(window, x + width, y, &ur_x, &ur_y);
		window_to_device(window, x + width, y + height, &lr_x, &lr_y);

		/* Calculate bounds */

		int32_t left_bound = min(min(ul_x, ll_x), min(ur_x, lr_x));
		int32_t top_bound  = min(min(ul_y, ll_y), min(ur_y, lr_y));

		int32_t right_bound = max(max(ul_x, ll_x), max(ur_x, lr_x));
		int32_t bottom_bound = max(max(ul_y, ll_y), max(ur_y, lr_y));

		rect->x = left_bound;
		rect->y = top_bound;
		rect->width = right_bound - left_bound;
		rect->height = bottom_bound - top_bound;
	}

	spin_lock(&yg->update_list_lock);
	list_insert(yg->update_list, rect);
	spin_unlock(&yg->update_list_lock);
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
	w->anim_mode = YUTANI_EFFECT_FADE_OUT;
	w->anim_start = yutani_current_time(yg);
}

/**
 * Remove a window from its owner's child set.
 */
static void window_remove_from_client(yutani_globals_t * yg, yutani_server_window_t * w) {
	list_t * client_list = hashmap_get(yg->clients_to_windows, (void *)w->owner);
	if (client_list) {
		node_t * n = list_find(client_list, w);
		if (n) {
			list_delete(client_list, n);
			free(n);
		}
		if (client_list->length == 0) {
			free(client_list);
			hashmap_remove(yg->clients_to_windows, (void *)w->owner);
		}
	}
}

/**
 * Actually remove a window and free the associated resources.
 */
static void window_actually_close(yutani_globals_t * yg, yutani_server_window_t * w) {
	/* Remove from the wid -> window mapping */
	hashmap_remove(yg->wids_to_windows, (void *)w->wid);

	/* Remove from the general list of windows. */
	list_remove(yg->windows, list_index_of(yg->windows, w));

	/* Unstack the window */
	unorder_window(yg, w);

	/* Mark the region where the window was */
	mark_window(yg, w);

	/* And if it was focused, unfocus it. */
	if (w == yg->focused_window) {
		yg->focused_window = NULL;
	}

	{
		char key[1024];
		YUTANI_SHMKEY_EXP(yg->server_ident, key, 1024, w->bufid);

		/*
		 * Normally we would acquire a lock before doing this, but the render
		 * thread holds that lock already and we are only called from the
		 * render thread, so we don't bother.
		 */
		syscall_shm_release(key);
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
static void yutani_query_result(yutani_globals_t * yg, uint32_t dest, yutani_server_window_t * win) {
	if (win && win->client_length) {
		yutani_msg_t * response = yutani_msg_build_window_advertise(win->wid, ad_flags(yg, win), win->client_offsets, win->client_length, win->client_strings);
		pex_send(yg->server, dest, response->size, (char *)response);
		free(response);
	}
}

/**
 * Send a notice to all subscribed clients that windows have updated.
 */
static void notify_subscribers(yutani_globals_t * yg) {
	yutani_msg_t * response = yutani_msg_build_notify();
	foreach(node, yg->window_subscribers) {
		uint32_t subscriber = (uint32_t)node->value;
		pex_send(yg->server, subscriber, response->size, (char *)response);
	}
	free(response);
}

static void window_move(yutani_globals_t * yg, yutani_server_window_t * window, int x, int y) {
	mark_window(yg, window);
	window->x = x;
	window->y = y;
	mark_window(yg, window);

	yutani_msg_t * response = yutani_msg_build_window_move(window->wid, x, y);
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
		window->tiled = 1;
	}

	int w = yg->width / width_div;
	int h = (yg->height - panel_h) / height_div;

	/* Calculate, move, etc. */
	window_move(yg, window, w * x, panel_h + h * y);

	yutani_msg_t * response = yutani_msg_build_window_resize(YUTANI_MSG_RESIZE_OFFER, window->wid, w, h, 0);
	pex_send(yg->server, window->owner, response->size, (char *)response);
	free(response);
	return;
}

/**
 * Process a key event.
 *
 * These are mostly compositor shortcuts and bindings.
 * We also process key bindings for other applications.
 */
static void handle_key_event(yutani_globals_t * yg, struct yutani_msg_key_event * ke) {
	yutani_server_window_t * focused = get_focused(yg);
	memcpy(&yg->kbd_state, &ke->state, sizeof(key_event_state_t));
	if (focused) {
		if ((ke->event.action == KEY_ACTION_DOWN) &&
			(ke->event.modifiers & KEY_MOD_LEFT_CTRL) &&
			(ke->event.modifiers & KEY_MOD_LEFT_SHIFT) &&
			(ke->event.keycode == 'z')) {
			mark_window(yg,focused);
			focused->rotation -= 5;
			mark_window(yg,focused);
			return;
		}
		if ((ke->event.action == KEY_ACTION_DOWN) &&
			(ke->event.modifiers & KEY_MOD_LEFT_CTRL) &&
			(ke->event.modifiers & KEY_MOD_LEFT_SHIFT) &&
			(ke->event.keycode == 'x')) {
			mark_window(yg,focused);
			focused->rotation += 5;
			mark_window(yg,focused);
			return;
		}
		if ((ke->event.action == KEY_ACTION_DOWN) &&
			(ke->event.modifiers & KEY_MOD_LEFT_CTRL) &&
			(ke->event.modifiers & KEY_MOD_LEFT_SHIFT) &&
			(ke->event.keycode == 'c')) {
			mark_window(yg,focused);
			focused->rotation = 0;
			mark_window(yg,focused);
			return;
		}
		if ((ke->event.action == KEY_ACTION_DOWN) &&
			(ke->event.modifiers & KEY_MOD_LEFT_ALT) &&
			(ke->event.keycode == KEY_F10)) {
			if (focused->z != YUTANI_ZORDER_BOTTOM && focused->z != YUTANI_ZORDER_TOP) {
				window_tile(yg, focused, 1, 1, 0, 0);
				return;
			}
		}
		if ((ke->event.action == KEY_ACTION_DOWN) &&
			(ke->event.modifiers & KEY_MOD_LEFT_ALT) &&
			(ke->event.keycode == KEY_F4)) {
			if (focused->z != YUTANI_ZORDER_BOTTOM && focused->z != YUTANI_ZORDER_TOP) {
				yutani_msg_t * response = yutani_msg_build_session_end();
				pex_send(yg->server, focused->owner, response->size, (char *)response);
				free(response);
				return;
			}
		}
#if YUTANI_DEBUG_WINDOW_SHAPES
		if ((ke->event.action == KEY_ACTION_DOWN) &&
			(ke->event.modifiers & KEY_MOD_LEFT_CTRL) &&
			(ke->event.modifiers & KEY_MOD_LEFT_SHIFT) &&
			(ke->event.keycode == 'v')) {
			yg->debug_shapes = (1-yg->debug_shapes);
			return;
		}
#endif
#if YUTANI_DEBUG_WINDOW_BOUNDS
		if ((ke->event.action == KEY_ACTION_DOWN) &&
			(ke->event.modifiers & KEY_MOD_LEFT_CTRL) &&
			(ke->event.modifiers & KEY_MOD_LEFT_SHIFT) &&
			(ke->event.keycode == 'b')) {
			yg->debug_bounds = (1-yg->debug_bounds);
			return;
		}
#endif
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
			if ((ke->event.modifiers & KEY_MOD_LEFT_CTRL) &&
				(ke->event.keycode == 's')) {
				yg->screenshot_frame = YUTANI_SCREENSHOT_FULL;
				return;
			}
			if ((ke->event.modifiers & KEY_MOD_LEFT_CTRL) &&
				(ke->event.keycode == 'w')) {
				yg->screenshot_frame = YUTANI_SCREENSHOT_WINDOW;
				return;
			}
		}
	}

	/*
	 * External bindings registered by clients.
	 */
	uint32_t key_code = ((ke->event.modifiers << 24) | (ke->event.keycode));
	if (hashmap_has(yg->key_binds, (void*)key_code)) {
		struct key_bind * bind = hashmap_get(yg->key_binds, (void*)key_code);

		yutani_msg_t * response = yutani_msg_build_key_event(focused ? focused->wid : -1, &ke->event, &ke->state);
		pex_send(yg->server, bind->owner, response->size, (char *)response);
		free(response);

		if (bind->response == YUTANI_BIND_STEAL) {
			/* If this keybinding was registered as "steal", we'll stop here. */
			return;
		}
	}

	/* Finally, send the key to the focused client. */
	if (focused) {

		yutani_msg_t * response = yutani_msg_build_key_event(focused->wid, &ke->event, &ke->state);
		pex_send(yg->server, focused->owner, response->size, (char *)response);
		free(response);

	}
}

/**
 * Register a new keybinding.
 *
 * req - bind message
 * owner - client to assign the binding to
 */
static void add_key_bind(yutani_globals_t * yg, struct yutani_msg_key_bind * req, unsigned int owner) {
	uint32_t key_code = (((uint8_t)req->modifiers << 24) | ((uint32_t)req->key & 0xFFFFFF));
	struct key_bind * bind = hashmap_get(yg->key_binds, (void*)key_code);

	if (!bind) {
		bind = malloc(sizeof(struct key_bind));

		bind->owner = owner;
		bind->response = req->response;

		hashmap_set(yg->key_binds, (void*)key_code, bind);
	} else {
		bind->owner = owner;
		bind->response = req->response;
	}
}

static void mouse_start_drag(yutani_globals_t * yg) {
	set_focused_at(yg, yg->mouse_x / MOUSE_SCALE, yg->mouse_y / MOUSE_SCALE);
	yg->mouse_window = get_focused(yg);
	if (yg->mouse_window) {
		if (yg->mouse_window->z == YUTANI_ZORDER_BOTTOM || yg->mouse_window->z == YUTANI_ZORDER_TOP) {
			yg->mouse_state = YUTANI_MOUSE_STATE_NORMAL;
			yg->mouse_window = NULL;
		} else {
			if (yg->mouse_window->tiled) {
				/* Untile it */
				yg->mouse_window->tiled = 0;
				yutani_msg_t * response = yutani_msg_build_window_resize(YUTANI_MSG_RESIZE_OFFER, yg->mouse_window->wid, yg->mouse_window->untiled_width, yg->mouse_window->untiled_height, 0);
				pex_send(yg->server, yg->mouse_window->owner, response->size, (char *)response);
				free(response);
			}
			yg->mouse_state = YUTANI_MOUSE_STATE_MOVING;
			yg->mouse_init_x = yg->mouse_x;
			yg->mouse_init_y = yg->mouse_y;
			yg->mouse_win_x  = yg->mouse_window->x;
			yg->mouse_win_y  = yg->mouse_window->y;
			mark_screen(yg, yg->mouse_x / MOUSE_SCALE - MOUSE_OFFSET_X, yg->mouse_y / MOUSE_SCALE - MOUSE_OFFSET_Y, MOUSE_WIDTH, MOUSE_HEIGHT);
			make_top(yg, yg->mouse_window);
		}
	}
}

static void mouse_start_resize(yutani_globals_t * yg, yutani_scale_direction_t direction) {
	set_focused_at(yg, yg->mouse_x / MOUSE_SCALE, yg->mouse_y / MOUSE_SCALE);
	yg->mouse_window = get_focused(yg);
	if (yg->mouse_window) {
		if (yg->mouse_window->z == YUTANI_ZORDER_BOTTOM || yg->mouse_window->z == YUTANI_ZORDER_TOP) {
			/* Prevent resizing panel and wallpaper */
			yg->mouse_state = YUTANI_MOUSE_STATE_NORMAL;
			yg->mouse_window = NULL;
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

			if (direction == SCALE_AUTO) {
				/* Determine the best direction to scale in based on simple 9-cell system. */
				int32_t x, y;
				device_to_window(yg->resizing_window, yg->mouse_x / MOUSE_SCALE, yg->mouse_y / MOUSE_SCALE, &x, &y);

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
			mark_window(yg, yg->mouse_window);
		}
	}
}

static void handle_mouse_event(yutani_globals_t * yg, struct yutani_msg_mouse_event * me)  {
	if (me->type == YUTANI_MOUSE_EVENT_TYPE_RELATIVE) {
		/*
		 * DON'T COMMIT THIS
		 * If you see and you're not me, you have permission to laugh at me for this
		 * and bring it up at random for the next year.
		 */
		yg->mouse_x += me->event.x_difference YUTANI_INCOMING_MOUSE_SCALE;
		yg->mouse_y -= me->event.y_difference YUTANI_INCOMING_MOUSE_SCALE;
	} else if (me->type == YUTANI_MOUSE_EVENT_TYPE_ABSOLUTE) {
		yg->mouse_x = me->event.x_difference * MOUSE_SCALE;
		yg->mouse_y = me->event.y_difference * MOUSE_SCALE;
	}

	if (yg->mouse_x < 0) yg->mouse_x = 0;
	if (yg->mouse_y < 0) yg->mouse_y = 0;
	if (yg->mouse_x > (yg->width) * MOUSE_SCALE) yg->mouse_x = (yg->width) * MOUSE_SCALE;
	if (yg->mouse_y > (yg->height) * MOUSE_SCALE) yg->mouse_y = (yg->height) * MOUSE_SCALE;

	switch (yg->mouse_state) {
		case YUTANI_MOUSE_STATE_NORMAL:
			{
				if ((me->event.buttons & YUTANI_MOUSE_BUTTON_LEFT) && (yg->kbd_state.k_alt)) {
					mouse_start_drag(yg);
#if YUTANI_RESIZE_RIGHT
				} else if ((me->event.buttons & YUTANI_MOUSE_BUTTON_RIGHT) && (yg->kbd_state.k_alt)) {
					yg->resizing_button = YUTANI_MOUSE_BUTTON_RIGHT;
					mouse_start_resize(yg, SCALE_AUTO);
#else
				} else if ((me->event.buttons & YUTANI_MOUSE_BUTTON_MIDDLE) && (yg->kbd_state.k_alt)) {
					yg->resizing_button = YUTANI_MOUSE_BUTTON_MIDDLE;
					mouse_start_resize(yg, SCALE_AUTO);
#endif
				} else if ((me->event.buttons & YUTANI_MOUSE_BUTTON_LEFT) && (!yg->kbd_state.k_alt)) {
					yg->mouse_state = YUTANI_MOUSE_STATE_DRAGGING;
					set_focused_at(yg, yg->mouse_x / MOUSE_SCALE, yg->mouse_y / MOUSE_SCALE);
					yg->mouse_window = get_focused(yg);
					yg->mouse_moved = 0;
					yg->mouse_drag_button = YUTANI_MOUSE_BUTTON_LEFT;
					device_to_window(yg->mouse_window, yg->mouse_x / MOUSE_SCALE, yg->mouse_y / MOUSE_SCALE, &yg->mouse_click_x, &yg->mouse_click_y);
					yutani_msg_t * response = yutani_msg_build_window_mouse_event(yg->mouse_window->wid, yg->mouse_click_x, yg->mouse_click_y, -1, -1, me->event.buttons, YUTANI_MOUSE_EVENT_DOWN);
					pex_send(yg->server, yg->mouse_window->owner, response->size, (char *)response);
					free(response);
				} else {
					yg->mouse_window = get_focused(yg);
					yutani_server_window_t * tmp_window = top_at(yg, yg->mouse_x / MOUSE_SCALE, yg->mouse_y / MOUSE_SCALE);
					if (yg->mouse_window) {
						int32_t x, y;
						device_to_window(yg->mouse_window, yg->mouse_x / MOUSE_SCALE, yg->mouse_y / MOUSE_SCALE, &x, &y);
						yutani_msg_t * response = yutani_msg_build_window_mouse_event(yg->mouse_window->wid, x, y, -1, -1, me->event.buttons, YUTANI_MOUSE_EVENT_MOVE);
						pex_send(yg->server, yg->mouse_window->owner, response->size, (char *)response);
						free(response);
					}
					if (tmp_window) {
						int32_t x, y;
						yutani_msg_t * response;
						if (tmp_window != yg->old_hover_window) {
							device_to_window(tmp_window, yg->mouse_x / MOUSE_SCALE, yg->mouse_y / MOUSE_SCALE, &x, &y);
							response = yutani_msg_build_window_mouse_event(tmp_window->wid, x, y, -1, -1, me->event.buttons, YUTANI_MOUSE_EVENT_ENTER);
							pex_send(yg->server, tmp_window->owner, response->size, (char *)response);
							free(response);
							if (yg->old_hover_window) {
								device_to_window(yg->old_hover_window, yg->mouse_x / MOUSE_SCALE, yg->mouse_y / MOUSE_SCALE, &x, &y);
								response = yutani_msg_build_window_mouse_event(yg->old_hover_window->wid, x, y, -1, -1, me->event.buttons, YUTANI_MOUSE_EVENT_LEAVE);
								pex_send(yg->server, yg->old_hover_window->owner, response->size, (char *)response);
								free(response);
							}
							yg->old_hover_window = tmp_window;
						}
						if (tmp_window != yg->mouse_window) {
							device_to_window(tmp_window, yg->mouse_x / MOUSE_SCALE, yg->mouse_y / MOUSE_SCALE, &x, &y);
							response = yutani_msg_build_window_mouse_event(tmp_window->wid, x, y, -1, -1, me->event.buttons, YUTANI_MOUSE_EVENT_MOVE);
							pex_send(yg->server, tmp_window->owner, response->size, (char *)response);
							free(response);
						}
					}
				}
			}
			break;
		case YUTANI_MOUSE_STATE_MOVING:
			{
				if (!(me->event.buttons & YUTANI_MOUSE_BUTTON_LEFT)) {
					yg->mouse_window = NULL;
					yg->mouse_state = YUTANI_MOUSE_STATE_NORMAL;
					mark_screen(yg, yg->mouse_x / MOUSE_SCALE - MOUSE_OFFSET_X, yg->mouse_y / MOUSE_SCALE - MOUSE_OFFSET_Y, MOUSE_WIDTH, MOUSE_HEIGHT);
				} else {
					if (yg->mouse_y / MOUSE_SCALE < 2) {
						window_tile(yg, yg->mouse_window, 1, 1, 0, 0);
						yg->mouse_window = NULL;
						yg->mouse_state = YUTANI_MOUSE_STATE_NORMAL;
						break;
					}
					int x, y;
					x = yg->mouse_win_x + (yg->mouse_x - yg->mouse_init_x) / MOUSE_SCALE;
					y = yg->mouse_win_y + (yg->mouse_y - yg->mouse_init_y) / MOUSE_SCALE;
					window_move(yg, yg->mouse_window, x, y);
				}
			}
			break;
		case YUTANI_MOUSE_STATE_DRAGGING:
			{
				if (!(me->event.buttons & yg->mouse_drag_button)) {
					/* Mouse released */
					yg->mouse_state = YUTANI_MOUSE_STATE_NORMAL;
					int32_t old_x = yg->mouse_click_x;
					int32_t old_y = yg->mouse_click_y;
					device_to_window(yg->mouse_window, yg->mouse_x / MOUSE_SCALE, yg->mouse_y / MOUSE_SCALE, &yg->mouse_click_x, &yg->mouse_click_y);
					if (!yg->mouse_moved) {
						yutani_msg_t * response = yutani_msg_build_window_mouse_event(yg->mouse_window->wid, yg->mouse_click_x, yg->mouse_click_y, -1, -1, me->event.buttons, YUTANI_MOUSE_EVENT_CLICK);
						pex_send(yg->server, yg->mouse_window->owner, response->size, (char *)response);
						free(response);
					} else {
						yutani_msg_t * response = yutani_msg_build_window_mouse_event(yg->mouse_window->wid, yg->mouse_click_x, yg->mouse_click_y, old_x, old_y, me->event.buttons, YUTANI_MOUSE_EVENT_RAISE);
						pex_send(yg->server, yg->mouse_window->owner, response->size, (char *)response);
						free(response);
					}
				} else {
					yg->mouse_state = YUTANI_MOUSE_STATE_DRAGGING;
					yg->mouse_moved = 1;
					int32_t old_x = yg->mouse_click_x;
					int32_t old_y = yg->mouse_click_y;
					device_to_window(yg->mouse_window, yg->mouse_x / MOUSE_SCALE, yg->mouse_y / MOUSE_SCALE, &yg->mouse_click_x, &yg->mouse_click_y);
					if (old_x != yg->mouse_click_x || old_y != yg->mouse_click_y) {
						yutani_msg_t * response = yutani_msg_build_window_mouse_event(yg->mouse_window->wid, yg->mouse_click_x, yg->mouse_click_y, old_x, old_y, me->event.buttons, YUTANI_MOUSE_EVENT_DRAG);
						pex_send(yg->server, yg->mouse_window->owner, response->size, (char *)response);
						free(response);
					}
				}
			}
			break;
		case YUTANI_MOUSE_STATE_RESIZING:
			{
				int width_diff  = (yg->mouse_x - yg->mouse_init_x) / MOUSE_SCALE;
				int height_diff = (yg->mouse_y - yg->mouse_init_y) / MOUSE_SCALE;

				mark_window_relative(yg, yg->resizing_window, yg->resizing_offset_x - 2, yg->resizing_offset_y - 2, yg->resizing_w + 10, yg->resizing_h + 10);

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

				yg->resizing_w = yg->resizing_window->width + width_diff;
				yg->resizing_h = yg->resizing_window->height + height_diff;

				/* Enforce logical boundaries */
				if (yg->resizing_w < 0) {
					yg->resizing_w = 0;
				}
				if (yg->resizing_h < 0) {
					yg->resizing_h = 0;
				}
				if (yg->resizing_offset_x > yg->resizing_window->width) {
					yg->resizing_offset_x = yg->resizing_window->width;
				}
				if (yg->resizing_offset_y > yg->resizing_window->height) {
					yg->resizing_offset_y = yg->resizing_window->height;
				}

				mark_window_relative(yg, yg->resizing_window, yg->resizing_offset_x - 2, yg->resizing_offset_y - 2, yg->resizing_w + 10, yg->resizing_h + 10);

				if (!(me->event.buttons & yg->resizing_button)) {
					TRACE("resize complete, now %d x %d", yg->resizing_w, yg->resizing_h);
					window_move(yg, yg->resizing_window, yg->resizing_window->x + yg->resizing_offset_x, yg->resizing_window->y + yg->resizing_offset_y);
					yutani_msg_t * response = yutani_msg_build_window_resize(YUTANI_MSG_RESIZE_OFFER, yg->resizing_window->wid, yg->resizing_w, yg->resizing_h, 0);
					pex_send(yg->server, yg->resizing_window->owner, response->size, (char *)response);
					free(response);
					yg->resizing_window = NULL;
					yg->mouse_window = NULL;
					yg->mouse_state = YUTANI_MOUSE_STATE_NORMAL;
				}
			}
			break;
		default:
			/* XXX ? */
			break;
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
		yg->backend_ctx = init_graphics_yutani_double_buffer(yg->host_window);
	} else {
		yg->backend_ctx = init_graphics_fullscreen_double_buffer();
	}

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
		snprintf(name, 64, "compositor-nest-%d", getpid());
		yg->server_ident = name;
	} else {
		/* XXX check if this already exists? */
		yg->server_ident = "compositor";
	}
	setenv("DISPLAY", yg->server_ident, 1);

	FILE * server = pex_bind(yg->server_ident);
	yg->server = server;

	TRACE("Loading fonts...");
	load_fonts(yg);
	TRACE("Done.");

	load_sprite_png(&yg->mouse_sprite, "/usr/share/cursor/normal.png");

	load_sprite_png(&yg->mouse_sprite_drag, "/usr/share/cursor/drag.png");
	load_sprite_png(&yg->mouse_sprite_resize_v, "/usr/share/cursor/resize-vertical.png");
	load_sprite_png(&yg->mouse_sprite_resize_h, "/usr/share/cursor/resize-horizontal.png");
	load_sprite_png(&yg->mouse_sprite_resize_da, "/usr/share/cursor/resize-uldr.png");
	load_sprite_png(&yg->mouse_sprite_resize_db, "/usr/share/cursor/resize-dlur.png");

	yg->last_mouse_x = 0;
	yg->last_mouse_y = 0;
	yg->mouse_x = yg->width * MOUSE_SCALE / 2;
	yg->mouse_y = yg->height * MOUSE_SCALE / 2;

	yg->windows = list_create();
	yg->wids_to_windows = hashmap_create_int(10);
	yg->key_binds = hashmap_create_int(10);
	yg->clients_to_windows = hashmap_create_int(10);
	yg->mid_zs = list_create();

	yg->window_subscribers = list_create();

	yutani_cairo_init(yg);

	pthread_t mouse_thread;
	pthread_t keyboard_thread;
	pthread_t render_thread;
	pthread_t nested_thread;

	if (yutani_options.nested) {
		/* Nested Yutani-Yutani mouse+keyboard */
		pthread_create(&nested_thread, NULL, nested_input, yg);
	} else {
		/* Toaru mouse+keyboard driver */
		pthread_create(&mouse_thread, NULL, mouse_input, NULL);
		pthread_create(&keyboard_thread, NULL, keyboard_input, NULL);
	}

	pthread_create(&render_thread, NULL, redraw, yg);

	if (!fork()) {
		if (argx < argc) {
			TRACE("Starting alternate startup app: %s", argv[argx]);
			execvp(argv[argx], &argv[argx]);
		} else {
			char * args[] = {"/bin/glogin", NULL};
			execvp(args[0], args);
		}
	}

	while (1) {
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
					TRACE("And hello to you, %08x!", p->source);
					yutani_msg_t * response = yutani_msg_build_welcome(yg->width, yg->height);
					pex_send(server, p->source, response->size, (char *)response);
					free(response);
				}
				break;
			case YUTANI_MSG_WINDOW_NEW:
				{
					struct yutani_msg_window_new * wn = (void *)m->data;
					TRACE("Client %08x requested a new window (%dx%d).", p->source, wn->width, wn->height);
					yutani_server_window_t * w = server_window_create(yg, wn->width, wn->height, p->source);
					yutani_msg_t * response = yutani_msg_build_window_init(w->wid, w->width, w->height, w->bufid);
					pex_send(server, p->source, response->size, (char *)response);
					free(response);

					notify_subscribers(yg);
				}
				break;
			case YUTANI_MSG_FLIP:
				{
					struct yutani_msg_flip * wf = (void *)m->data;
					yutani_server_window_t * w = hashmap_get(yg->wids_to_windows, (void *)wf->wid);
					if (w) {
						mark_window(yg, w);
					}
				}
				break;
			case YUTANI_MSG_FLIP_REGION:
				{
					struct yutani_msg_flip_region * wf = (void *)m->data;
					yutani_server_window_t * w = hashmap_get(yg->wids_to_windows, (void *)wf->wid);
					if (w) {
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
					TRACE("%08x wanted to move window %d", p->source, wm->wid);
					yutani_server_window_t * win = hashmap_get(yg->wids_to_windows, (void*)wm->wid);
					if (win) {
						window_move(yg, win, wm->x, wm->y);
					} else {
						TRACE("%08x wanted to move window %d, but I can't find it?", p->source, wm->wid);
					}
				}
				break;
			case YUTANI_MSG_WINDOW_CLOSE:
				{
					struct yutani_msg_window_close * wc = (void *)m->data;
					yutani_server_window_t * w = hashmap_get(yg->wids_to_windows, (void *)wc->wid);
					if (w) {
						window_mark_for_close(yg, w);
						window_remove_from_client(yg, w);
					}
				}
				break;
			case YUTANI_MSG_WINDOW_STACK:
				{
					struct yutani_msg_window_stack * ws = (void *)m->data;
					yutani_server_window_t * w = hashmap_get(yg->wids_to_windows, (void *)ws->wid);
					if (w) {
						reorder_window(yg, w, ws->z);
					}
				}
				break;
			case YUTANI_MSG_RESIZE_REQUEST:
				{
					struct yutani_msg_window_resize * wr = (void *)m->data;
					yutani_server_window_t * w = hashmap_get(yg->wids_to_windows, (void *)wr->wid);
					if (w) {
						yutani_msg_t * response = yutani_msg_build_window_resize(YUTANI_MSG_RESIZE_OFFER, w->wid, wr->width, wr->height, 0);
						pex_send(server, p->source, response->size, (char *)response);
						free(response);
					}
				}
				break;
			case YUTANI_MSG_RESIZE_OFFER:
				{
					struct yutani_msg_window_resize * wr = (void *)m->data;
					yutani_server_window_t * w = hashmap_get(yg->wids_to_windows, (void *)wr->wid);
					if (w) {
						yutani_msg_t * response = yutani_msg_build_window_resize(YUTANI_MSG_RESIZE_OFFER, w->wid, wr->width, wr->height, 0);
						pex_send(server, p->source, response->size, (char *)response);
						free(response);
					}
				}
				break;
			case YUTANI_MSG_RESIZE_ACCEPT:
				{
					struct yutani_msg_window_resize * wr = (void *)m->data;
					yutani_server_window_t * w = hashmap_get(yg->wids_to_windows, (void *)wr->wid);
					if (w) {
						uint32_t newbufid = server_window_resize(yg, w, wr->width, wr->height);
						yutani_msg_t * response = yutani_msg_build_window_resize(YUTANI_MSG_RESIZE_BUFID, w->wid, wr->width, wr->height, newbufid);
						pex_send(server, p->source, response->size, (char *)response);
						free(response);
					}
				}
				break;
			case YUTANI_MSG_RESIZE_DONE:
				{
					struct yutani_msg_window_resize * wr = (void *)m->data;
					yutani_server_window_t * w = hashmap_get(yg->wids_to_windows, (void *)wr->wid);
					if (w) {
						server_window_resize_finish(yg, w, wr->width, wr->height);
					}
				}
				break;
			case YUTANI_MSG_QUERY_WINDOWS:
				{
					yutani_query_result(yg, p->source, yg->bottom_z);
					foreach (node, yg->mid_zs) {
						yutani_query_result(yg, p->source, node->value);
					}
					yutani_query_result(yg, p->source, yg->top_z);
					yutani_msg_t * response = yutani_msg_build_window_advertise(0, 0, NULL, 0, NULL);
					pex_send(server, p->source, response->size, (char *)response);
					free(response);
				}
				break;
			case YUTANI_MSG_SUBSCRIBE:
				{
					foreach(node, yg->window_subscribers) {
						if ((uint32_t)node->value == p->source) {
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
					yutani_server_window_t * w = hashmap_get(yg->wids_to_windows, (void *)wa->wid);
					if (w) {
						if (w->client_strings) free(w->client_strings);

						for (int i = 0; i < 5; ++i) {
							w->client_offsets[i] = wa->offsets[i];
						}

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
					yutani_msg_t * response = yutani_msg_build_session_end();
					pex_broadcast(server, response->size, (char *)response);
					free(response);
				}
				break;
			case YUTANI_MSG_WINDOW_FOCUS:
				{
					struct yutani_msg_window_focus * wa = (void *)m->data;
					yutani_server_window_t * w = hashmap_get(yg->wids_to_windows, (void *)wa->wid);
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
					yutani_server_window_t * w = hashmap_get(yg->wids_to_windows, (void *)wa->wid);
					if (w) {
						/* Start dragging */
						mouse_start_drag(yg);
					}
				}
				break;
			case YUTANI_MSG_WINDOW_UPDATE_SHAPE:
				{
					struct yutani_msg_window_update_shape * wa = (void *)m->data;
					yutani_server_window_t * w = hashmap_get(yg->wids_to_windows, (void *)wa->wid);
					if (w) {
						/* Set shape parameter */
						server_window_update_shape(yg, w, wa->set_shape);
					}
				}
				break;
			case YUTANI_MSG_WINDOW_WARP_MOUSE:
				{
					struct yutani_msg_window_warp_mouse * wa = (void *)m->data;
					yutani_server_window_t * w = hashmap_get(yg->wids_to_windows, (void *)wa->wid);
					if (w) {
						if (yg->focused_window == w) {
							int32_t x, y;
							window_to_device(w, wa->x, wa->y, &x, &y);

							struct yutani_msg_mouse_event me;
							me.event.x_difference = x;
							me.event.y_difference = y;
							me.event.buttons = 0;
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
					yutani_server_window_t * w = hashmap_get(yg->wids_to_windows, (void *)wa->wid);
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
					yutani_server_window_t * w = hashmap_get(yg->wids_to_windows, (void *)wa->wid);
					if (w) {
						if (yg->focused_window == w && !yg->resizing_window) {
							yg->resizing_window = w;
							yg->resizing_button = YUTANI_MOUSE_BUTTON_LEFT; /* XXX Uh, what if we used something else */
							mouse_start_resize(yg, wa->direction);
						}
					}
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
