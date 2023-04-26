/**
 * @brief Yutani Client Library
 *
 * Client library for the compositing window system.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014-2018 K. Lange
 */
#include <string.h>
#include <stdlib.h>
#include <sys/shm.h>

#include <toaru/pex.h>
#include <toaru/graphics.h>
#include <toaru/kbd.h>
#include <toaru/hashmap.h>
#include <toaru/list.h>
#include <toaru/yutani.h>
#include <toaru/yutani-internal.h>
#include <toaru/mouse.h>

/* We need the flags but don't want the library dep (maybe the flags should be here?) */
#include <toaru/./decorations.h>

/**
 * yutani_wait_for
 *
 * Wait for a particular kind of message, queuing other types
 * of messages for processing later.
 */
yutani_msg_t * yutani_wait_for(yutani_t * y, uint32_t type) {
	do {
		yutani_msg_t * out;
		size_t size;
		{
			char tmp[MAX_PACKET_SIZE];
			size = pex_recv(y->sock, tmp);
			out = malloc(size);
			memcpy(out, tmp, size);
		}

		if (out->type == type) {
			return out;
		} else {
			list_insert(y->queued, out);
		}
	} while (1); /* XXX: (!y->abort) */
}

/**
 * yutani_query
 *
 * Check if there is an available message, either in the
 * internal queue or directly from the server interface.
 */
size_t yutani_query(yutani_t * y) {
	if (y->queued->length > 0) return 1;
	return pex_query(y->sock);
}

/**
 * _handle_internal
 *
 * Some messages are processed internally. They are still
 * available to the client application, but some work will
 * be done before they are handed off.
 *
 * WELCOME: Update the display_width and display_height for the connection.
 * WINDOW_MOVE: Update the window location.
 */
static void _handle_internal(yutani_t * y, yutani_msg_t * out) {
	switch (out->type) {
		case YUTANI_MSG_WELCOME:
			{
				struct yutani_msg_welcome * mw = (void *)out->data;
				y->display_width = mw->display_width;
				y->display_height = mw->display_height;
			}
			break;
		case YUTANI_MSG_WINDOW_MOVE:
			{
				struct yutani_msg_window_move * wm = (void *)out->data;
				yutani_window_t * win = hashmap_get(y->windows, (void *)(uintptr_t)wm->wid);
				if (win) {
					win->x = wm->x;
					win->y = wm->y;
				}
			}
			break;
		case YUTANI_MSG_RESIZE_OFFER:
			{
				struct yutani_msg_window_resize * wr = (void *)out->data;
				yutani_window_t * win = hashmap_get(y->windows, (void *)(uintptr_t)wr->wid);
				if (win) {
					win->decorator_flags &= ~(DECOR_FLAG_TILED);
					win->decorator_flags |= (wr->flags & YUTANI_RESIZE_TILED) << 2;
				}
			}
		default:
			break;
	}
}

/**
 * yutani_poll
 *
 * Wait for a message to be available, processing it if
 * it has internal processing requirements.
 */
yutani_msg_t * yutani_poll(yutani_t * y) {
	yutani_msg_t * out;

	if (y->queued->length > 0) {
		node_t * node = list_dequeue(y->queued);
		out = (yutani_msg_t *)node->value;
		free(node);
		_handle_internal(y, out);
		return out;
	}

	ssize_t size;
	{
		char tmp[MAX_PACKET_SIZE];
		size = pex_recv(y->sock, tmp);
		if (size <= 0) return NULL;
		out = malloc(size);
		memcpy(out, tmp, size);
	}

	_handle_internal(y, out);

	return out;
}

/**
 * yutani_poll_async
 *
 * Get the next available message, if there is one, otherwise
 * return immediately. Generally should be called in a loop
 * after an initial call to yutani_poll in case processing
 * caused additional messages to be queued.
 */
yutani_msg_t * yutani_poll_async(yutani_t * y) {
	if (yutani_query(y) > 0) {
		return yutani_poll(y);
	}
	return NULL;
}

void yutani_msg_buildx_hello(yutani_msg_t * msg) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_HELLO;
	msg->size  = sizeof(struct yutani_message);
}


void yutani_msg_buildx_flip(yutani_msg_t * msg, yutani_wid_t wid) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_FLIP;
	msg->size  = sizeof(struct yutani_message) + sizeof(struct yutani_msg_flip);

	struct yutani_msg_flip * mw = (void *)msg->data;

	mw->wid = wid;
}


void yutani_msg_buildx_welcome(yutani_msg_t * msg, uint32_t width, uint32_t height) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_WELCOME;
	msg->size  = sizeof(struct yutani_message) + sizeof(struct yutani_msg_welcome);

	struct yutani_msg_welcome * mw = (void *)msg->data;

	mw->display_width = width;
	mw->display_height = height;
}


void yutani_msg_buildx_window_new(yutani_msg_t * msg, uint32_t width, uint32_t height) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_WINDOW_NEW;
	msg->size  = sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_new);

	struct yutani_msg_window_new * mw = (void *)msg->data;

	mw->width = width;
	mw->height = height;
}


void yutani_msg_buildx_window_new_flags(yutani_msg_t * msg, uint32_t width, uint32_t height, uint32_t flags) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_WINDOW_NEW_FLAGS;
	msg->size  = sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_new_flags);

	struct yutani_msg_window_new_flags * mw = (void *)msg->data;

	mw->width = width;
	mw->height = height;
	mw->flags = flags;
}


void yutani_msg_buildx_window_init(yutani_msg_t * msg, yutani_wid_t wid, uint32_t width, uint32_t height, uint32_t bufid) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_WINDOW_INIT;
	msg->size  = sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_init);

	struct yutani_msg_window_init * mw = (void *)msg->data;

	mw->wid = wid;
	mw->width = width;
	mw->height = height;
	mw->bufid = bufid;
}


void yutani_msg_buildx_window_close(yutani_msg_t * msg, yutani_wid_t wid) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_WINDOW_CLOSE;
	msg->size  = sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_close);

	struct yutani_msg_window_close * mw = (void *)msg->data;

	mw->wid = wid;
}


void yutani_msg_buildx_key_event(yutani_msg_t * msg, yutani_wid_t wid, key_event_t * event, key_event_state_t * state) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_KEY_EVENT;
	msg->size  = sizeof(struct yutani_message) + sizeof(struct yutani_msg_key_event);

	struct yutani_msg_key_event * mw = (void *)msg->data;

	mw->wid = wid;
	memcpy(&mw->event, event, sizeof(key_event_t));
	memcpy(&mw->state, state, sizeof(key_event_state_t));
}


void yutani_msg_buildx_mouse_event(yutani_msg_t * msg, yutani_wid_t wid, mouse_device_packet_t * event, int32_t type) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_MOUSE_EVENT;
	msg->size  = sizeof(struct yutani_message) + sizeof(struct yutani_msg_mouse_event);

	struct yutani_msg_mouse_event * mw = (void *)msg->data;

	mw->wid = wid;
	memcpy(&mw->event, event, sizeof(mouse_device_packet_t));
	mw->type = type;
}


void yutani_msg_buildx_window_move(yutani_msg_t * msg, yutani_wid_t wid, int32_t x, int32_t y) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_WINDOW_MOVE;
	msg->size  = sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_move);

	struct yutani_msg_window_move * mw = (void *)msg->data;

	mw->wid = wid;
	mw->x = x;
	mw->y = y;
}

void yutani_msg_buildx_window_move_relative(yutani_msg_t * msg, yutani_wid_t wid, yutani_wid_t wid2, int32_t x, int32_t y) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_WINDOW_MOVE_RELATIVE;
	msg->size  = sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_move_relative);

	struct yutani_msg_window_move_relative * mw = (void *)msg->data;

	mw->wid_to_move = wid;
	mw->wid_base = wid2;
	mw->x = x;
	mw->y = y;
}

void yutani_msg_buildx_window_stack(yutani_msg_t * msg, yutani_wid_t wid, int z) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_WINDOW_STACK;
	msg->size  = sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_stack);

	struct yutani_msg_window_stack * mw = (void *)msg->data;

	mw->wid = wid;
	mw->z = z;
}


void yutani_msg_buildx_window_focus_change(yutani_msg_t * msg, yutani_wid_t wid, int focused) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_WINDOW_FOCUS_CHANGE;
	msg->size  = sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_focus_change);

	struct yutani_msg_window_focus_change * mw = (void *)msg->data;

	mw->wid = wid;
	mw->focused = focused;
}


void yutani_msg_buildx_window_mouse_event(yutani_msg_t * msg, yutani_wid_t wid, int32_t new_x, int32_t new_y, int32_t old_x, int32_t old_y, uint8_t buttons, uint8_t command, uint8_t modifiers) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_WINDOW_MOUSE_EVENT;
	msg->size  = sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_mouse_event);

	struct yutani_msg_window_mouse_event * mw = (void *)msg->data;

	mw->wid = wid;
	mw->new_x = new_x;
	mw->new_y = new_y;
	mw->old_x = old_x;
	mw->old_y = old_y;
	mw->buttons = buttons;
	mw->command = command;
	mw->modifiers = modifiers;
}


void yutani_msg_buildx_flip_region(yutani_msg_t * msg, yutani_wid_t wid, int32_t x, int32_t y, int32_t width, int32_t height) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_FLIP_REGION;
	msg->size  = sizeof(struct yutani_message) + sizeof(struct yutani_msg_flip_region);

	struct yutani_msg_flip_region * mw = (void *)msg->data;

	mw->wid = wid;
	mw->x = x;
	mw->y = y;
	mw->width = width;
	mw->height = height;
}


void yutani_msg_buildx_window_resize(yutani_msg_t * msg, uint32_t type, yutani_wid_t wid, uint32_t width, uint32_t height, uint32_t bufid, uint32_t flags) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = type;
	msg->size  = sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_resize);

	struct yutani_msg_window_resize * mw = (void *)msg->data;

	mw->wid = wid;
	mw->width = width;
	mw->height = height;
	mw->bufid = bufid;
	mw->flags = flags;
}


void yutani_msg_buildx_window_advertise(yutani_msg_t * msg, yutani_wid_t wid, uint32_t flags, uint32_t icon, uint32_t bufid, uint32_t width, uint32_t height, size_t length, char * data) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_WINDOW_ADVERTISE;
	msg->size  = sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_advertise) + length;

	struct yutani_msg_window_advertise * mw = (void *)msg->data;

	mw->wid = wid;
	mw->flags = flags;
	mw->size = length;
	mw->icon = icon;
	mw->bufid = bufid;
	mw->width = width;
	mw->height = height;
	if (data) {
		memcpy(mw->strings, data, mw->size);
	}
}


void yutani_msg_buildx_subscribe(yutani_msg_t * msg) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_SUBSCRIBE;
	msg->size  = sizeof(struct yutani_message);
}


void yutani_msg_buildx_unsubscribe(yutani_msg_t * msg) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_UNSUBSCRIBE;
	msg->size  = sizeof(struct yutani_message);
}


void yutani_msg_buildx_query_windows(yutani_msg_t * msg) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_QUERY_WINDOWS;
	msg->size  = sizeof(struct yutani_message);
}


void yutani_msg_buildx_notify(yutani_msg_t * msg) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_NOTIFY;
	msg->size  = sizeof(struct yutani_message);
}


void yutani_msg_buildx_session_end(yutani_msg_t * msg) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_SESSION_END;
	msg->size  = sizeof(struct yutani_message);
}


void yutani_msg_buildx_window_focus(yutani_msg_t * msg, yutani_wid_t wid) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_WINDOW_FOCUS;
	msg->size  = sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_focus);

	struct yutani_msg_window_focus * mw = (void *)msg->data;

	mw->wid = wid;
}


void yutani_msg_buildx_key_bind(yutani_msg_t * msg, kbd_key_t key, kbd_mod_t mod, int response) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_KEY_BIND;
	msg->size  = sizeof(struct yutani_message) + sizeof(struct yutani_msg_key_bind);

	struct yutani_msg_key_bind * mw = (void *)msg->data;

	mw->key = key;
	mw->modifiers = mod;
	mw->response = response;
}


void yutani_msg_buildx_window_drag_start(yutani_msg_t * msg, yutani_wid_t wid) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_WINDOW_DRAG_START;
	msg->size  = sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_drag_start);

	struct yutani_msg_window_drag_start * mw = (void *)msg->data;

	mw->wid = wid;
}


void yutani_msg_buildx_window_update_shape(yutani_msg_t * msg, yutani_wid_t wid, int set_shape) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_WINDOW_UPDATE_SHAPE;
	msg->size  = sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_update_shape);

	struct yutani_msg_window_update_shape * mw = (void *)msg->data;

	mw->wid = wid;
	mw->set_shape = set_shape;
}


void yutani_msg_buildx_window_warp_mouse(yutani_msg_t * msg, yutani_wid_t wid, int32_t x, int32_t y) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_WINDOW_WARP_MOUSE;
	msg->size  = sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_warp_mouse);

	struct yutani_msg_window_warp_mouse * mw = (void *)msg->data;

	mw->wid = wid;
	mw->x = x;
	mw->y = y;
}


void yutani_msg_buildx_window_show_mouse(yutani_msg_t * msg, yutani_wid_t wid, int32_t show_mouse) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_WINDOW_SHOW_MOUSE;
	msg->size  = sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_show_mouse);

	struct yutani_msg_window_show_mouse * mw = (void *)msg->data;

	mw->wid = wid;
	mw->show_mouse = show_mouse;
}


void yutani_msg_buildx_window_resize_start(yutani_msg_t * msg, yutani_wid_t wid, yutani_scale_direction_t direction) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_WINDOW_RESIZE_START;
	msg->size  = sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_resize_start);

	struct yutani_msg_window_resize_start * mw = (void *)msg->data;

	mw->wid = wid;
	mw->direction = direction;
}


void yutani_msg_buildx_special_request(yutani_msg_t * msg, yutani_wid_t wid, uint32_t request) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_SPECIAL_REQUEST;
	msg->size  = sizeof(struct yutani_message) + sizeof(struct yutani_msg_special_request);

	struct yutani_msg_special_request * sr = (void *)msg->data;

	sr->wid   = wid;
	sr->request = request;
}

void yutani_msg_buildx_clipboard(yutani_msg_t * msg, char * content) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_CLIPBOARD;
	msg->size  = sizeof(struct yutani_message) + sizeof(struct yutani_msg_clipboard) + strlen(content);

	struct yutani_msg_clipboard * cl = (void *)msg->data;

	cl->size = strlen(content);
	memcpy(cl->content, content, strlen(content));
}

void yutani_msg_buildx_window_panel_size(yutani_msg_t * msg, yutani_wid_t wid, int32_t x, int32_t y, int32_t w, int32_t h) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_WINDOW_PANEL_SIZE;
	msg->size  = sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_panel_size);

	struct yutani_msg_window_panel_size * ps = (void *)msg->data;
	ps->wid = wid;
	ps->x = x;
	ps->y = y;
	ps->w = w;
	ps->h = h;
}

int yutani_msg_send(yutani_t * y, yutani_msg_t * msg) {
	return pex_reply(y->sock, msg->size, (char *)msg);
}

yutani_t * yutani_context_create(FILE * socket) {
	yutani_t * out = malloc(sizeof(yutani_t));

	out->sock = socket;
	out->display_width  = 0;
	out->display_height = 0;
	out->windows = hashmap_create_int(10);
	out->queued = list_create();
	return out;
}

/**
 * yutani_init
 *
 * Connect to the compositor.
 *
 * Connects and handles the initial welcome message.
 */
yutani_t * yutani_init(void) {
	char * server_name = getenv("DISPLAY");
	if (!server_name) {
		server_name = "compositor";
	}
	FILE * c = pex_connect(server_name);

	if (!c) {
		return NULL; /* Connection failed. */
	}

	yutani_t * y = yutani_context_create(c);
	yutani_msg_buildx_hello_alloc(m);
	yutani_msg_buildx_hello(m);
	yutani_msg_send(y, m);

	yutani_msg_t * mm = yutani_wait_for(y, YUTANI_MSG_WELCOME);
	struct yutani_msg_welcome * mw = (void *)&mm->data;
	y->display_width = mw->display_width;
	y->display_height = mw->display_height;
	y->server_ident = server_name;
	free(mm);

	return y;
}

/**
 * yutani_window_create_flags
 *
 * Create a window with certain pre-specified properties.
 */
yutani_window_t * yutani_window_create_flags(yutani_t * y, int width, int height, uint32_t flags) {
	yutani_window_t * win = malloc(sizeof(yutani_window_t));

	yutani_msg_buildx_window_new_flags_alloc(m);
	yutani_msg_buildx_window_new_flags(m, width, height, flags);
	yutani_msg_send(y, m);

	yutani_msg_t * mm = yutani_wait_for(y, YUTANI_MSG_WINDOW_INIT);
	struct yutani_msg_window_init * mw = (void *)&mm->data;

	win->width = mw->width;
	win->height = mw->height;
	win->bufid = mw->bufid;
	win->wid = mw->wid;
	win->focused = 0;
	win->decorator_flags = 0;
	win->x = 0;
	win->y = 0;
	win->user_data = NULL;
	win->ctx = y;
	win->mouse_state = -1;
	free(mm);

	hashmap_set(y->windows, (void*)(uintptr_t)win->wid, win);

	char key[1024];
	YUTANI_SHMKEY(y->server_ident, key, 1024, win);

	size_t size = (width * height * 4);
	win->buffer = shm_obtain(key, &size);
	return win;

}

/**
 * yutani_window_create
 *
 * Create a basic window.
 */
yutani_window_t * yutani_window_create(yutani_t * y, int width, int height) {
	return yutani_window_create_flags(y,width,height,0);
}

/**
 * yutani_flip
 *
 * Ask the server to redraw the window.
 */
void yutani_flip(yutani_t * y, yutani_window_t * win) {
	yutani_msg_buildx_flip_alloc(m);
	yutani_msg_buildx_flip(m, win->wid);
	yutani_msg_send(y, m);
}

/**
 * yutani_flip_region
 *
 * Ask the server to redraw a region relative the window.
 */
void yutani_flip_region(yutani_t * yctx, yutani_window_t * win, int32_t x, int32_t y, int32_t width, int32_t height) {
	yutani_msg_buildx_flip_region_alloc(m);
	yutani_msg_buildx_flip_region(m, win->wid, x, y, width, height);
	yutani_msg_send(yctx, m);
}

/**
 * yutani_close
 *
 * Close a window. A closed window should not be used again,
 * and its associated buffers will be freed.
 */
void yutani_close(yutani_t * y, yutani_window_t * win) {
	yutani_msg_buildx_window_close_alloc(m);
	yutani_msg_buildx_window_close(m, win->wid);
	yutani_msg_send(y, m);

	/* Now destroy our end of the window */
	{
		char key[1024];
		YUTANI_SHMKEY_EXP(y->server_ident, key, 1024, win->bufid);
		shm_release(key);
	}

	hashmap_remove(y->windows, (void*)(uintptr_t)win->wid);
	free(win);
}

/**
 * yutani_window_move
 *
 * Request a window be moved to new a location on screen.
 */
void yutani_window_move(yutani_t * yctx, yutani_window_t * window, int x, int y) {
	yutani_msg_buildx_window_move_alloc(m);
	yutani_msg_buildx_window_move(m, window->wid, x, y);
	yutani_msg_send(yctx, m);
}

/**
 * yutani_window_move_relative
 *
 * Move a window to a location based on the local coordinate space of a base window.
 */
void yutani_window_move_relative(yutani_t * yctx, yutani_window_t * window, yutani_window_t * base, int x, int y) {
	yutani_msg_buildx_window_move_relative_alloc(m);
	yutani_msg_buildx_window_move_relative(m, window->wid, base->wid, x, y);
	yutani_msg_send(yctx, m);
}

/**
 * yutani_set_stack
 *
 * Set the stacking order of the window.
 */
void yutani_set_stack(yutani_t * yctx, yutani_window_t * window, int z) {
	yutani_msg_buildx_window_stack_alloc(m);
	yutani_msg_buildx_window_stack(m, window->wid, z);
	yutani_msg_send(yctx, m);
}

/**
 * yutani_window_resize
 *
 * Request that the server resize a window.
 */
void yutani_window_resize(yutani_t * yctx, yutani_window_t * window, uint32_t width, uint32_t height) {
	yutani_msg_buildx_window_resize_alloc(m);
	yutani_msg_buildx_window_resize(m, YUTANI_MSG_RESIZE_REQUEST, window->wid, width, height, 0, 0);
	yutani_msg_send(yctx, m);
}

/**
 * yutani_window_resize_offer
 *
 * In a response to a server resize message, offer an alternative size.
 * Allows the client to reject a user-provided resize request due to
 * size constraints or other reasons.
 */
void yutani_window_resize_offer(yutani_t * yctx, yutani_window_t * window, uint32_t width, uint32_t height) {
	yutani_msg_buildx_window_resize_alloc(m);
	yutani_msg_buildx_window_resize(m, YUTANI_MSG_RESIZE_OFFER, window->wid, width, height, 0, 0);
	yutani_msg_send(yctx, m);
}

/**
 * yutani_window_resize_accept
 *
 * Accept the server's resize request, initialize new buffers
 * and all the client to draw into the new buffers.
 */
void yutani_window_resize_accept(yutani_t * yctx, yutani_window_t * window, uint32_t width, uint32_t height) {
	yutani_msg_buildx_window_resize_alloc(m);
	yutani_msg_buildx_window_resize(m, YUTANI_MSG_RESIZE_ACCEPT, window->wid, width, height, 0, 0);
	yutani_msg_send(yctx, m);

	/* Now wait for the new bufid */
	yutani_msg_t * mm = yutani_wait_for(yctx, YUTANI_MSG_RESIZE_BUFID);
	struct yutani_msg_window_resize * wr = (void*)mm->data;

	if (window->wid != wr->wid) {
		/* I am not sure what to do here. */
		return;
	}

	/* Update the window */
	window->width = wr->width;
	window->height = wr->height;
	window->oldbufid = window->bufid;
	window->bufid = wr->bufid;
	free(mm);

	/* Allocate the buffer */
	{
		char key[1024];
		YUTANI_SHMKEY(yctx->server_ident, key, 1024, window);

		size_t size = (window->width * window->height * 4);
		window->buffer = shm_obtain(key, &size);
	}
}

/**
 * yutani_window_resize_done
 *
 * The client has finished drawing into the new buffers after
 * accepting a resize request and the server should now
 * discard the old buffer and switch to the new one.
 */
void yutani_window_resize_done(yutani_t * yctx, yutani_window_t * window) {
	/* Destroy the old buffer */
	{
		char key[1024];
		YUTANI_SHMKEY_EXP(yctx->server_ident, key, 1024, window->oldbufid);
		shm_release(key);
	}

	yutani_msg_buildx_window_resize_alloc(m);
	yutani_msg_buildx_window_resize(m, YUTANI_MSG_RESIZE_DONE, window->wid, window->width, window->height, window->bufid, 0);
	yutani_msg_send(yctx, m);
}

/**
 * yutani_window_advertise
 *
 * Provide a title for a window to have it show up
 * in the panel window list.
 */
void yutani_window_advertise(yutani_t * yctx, yutani_window_t * window, char * name) {

	uint32_t flags = 0; /* currently, no client flags */
	uint32_t length = 0;
	uint32_t icon = 0;
	char * strings;

	if (!name) {
		length = 1;
		strings = " ";
	} else {
		length = strlen(name) + 1;
		strings = name;
		icon = strlen(name);
	}

	yutani_msg_buildx_window_advertise_alloc(m, length);
	yutani_msg_buildx_window_advertise(m, window->wid, flags, icon, 0, 0, 0, length, strings);
	yutani_msg_send(yctx, m);
}

/**
 * yutani_window_advertise_icon
 *
 * Provide a title and an icon for the panel to show.
 *
 * Note that three additional fields are available in the advertisement
 * messages which are not yet used. This is to allow for future expansion.
 */
void yutani_window_advertise_icon(yutani_t * yctx, yutani_window_t * window, char * name, char * icon) {

	uint32_t flags = 0; /* currently no client flags */
	uint32_t iconx = 0;
	uint32_t length = strlen(name) + strlen(icon) + 2;
	char * strings = malloc(length);

	if (name) {
		memcpy(&strings[0], name, strlen(name)+1);
		iconx = strlen(name);
	}
	if (icon) {
		memcpy(&strings[strlen(name)+1], icon, strlen(icon)+1);
		iconx = strlen(name)+1;
	}

	yutani_msg_buildx_window_advertise_alloc(m, length);
	yutani_msg_buildx_window_advertise(m, window->wid, flags, iconx, 0, 0, 0, length, strings);
	yutani_msg_send(yctx, m);
	free(strings);
}

/**
 * yutani_subscribe_windows
 *
 * Subscribe to messages about new window advertisements.
 * Basically, if you're a panel, you want to do this, so
 * you can know when windows move around or change focus.
 */
void yutani_subscribe_windows(yutani_t * y) {
	yutani_msg_buildx_subscribe_alloc(m);
	yutani_msg_buildx_subscribe(m);
	yutani_msg_send(y, m);
}

/**
 * yutani_unsubscribe_windows
 *
 * If you no longer wish to receive window change messages,
 * you can unsubscribe your client from them.
 */
void yutani_unsubscribe_windows(yutani_t * y) {
	yutani_msg_buildx_unsubscribe_alloc(m);
	yutani_msg_buildx_unsubscribe(m);
	yutani_msg_send(y, m);
}

/**
 * yutani_query_windows
 *
 * When notified of changes, call this to request
 * the new information.
 */
void yutani_query_windows(yutani_t * y) {
	yutani_msg_buildx_query_windows_alloc(m);
	yutani_msg_buildx_query_windows(m);
	yutani_msg_send(y, m);
}

/**
 * yutani_session_end
 *
 * For use by session managers, tell the compositor
 * that the session has ended and it should inform
 * other clients of this so they can exit.
 */
void yutani_session_end(yutani_t * y) {
	yutani_msg_buildx_session_end_alloc(m);
	yutani_msg_buildx_session_end(m);
	yutani_msg_send(y, m);
}

/**
 * yutani_focus_window
 *
 * Change focus to the given window. Mostly used by
 * panels and other window management things, but if you
 * have a multi-window application, such as one with a
 * model dialog, and you want to force focus away from one
 * window and onto another, you can use this.
 */
void yutani_focus_window(yutani_t * yctx, yutani_wid_t wid) {
	yutani_msg_buildx_window_focus_alloc(m);
	yutani_msg_buildx_window_focus(m, wid);
	yutani_msg_send(yctx, m);
}

/**
 * yutani_key_bind
 *
 * Request a key combination always be sent to this client.
 * You can request for the combination to be sent only to
 * this client (steal binding) or to also go to other clients
 * (spy binding), the latter of which is useful for catching
 * changes to modifier keys.
 */
void yutani_key_bind(yutani_t * yctx, kbd_key_t key, kbd_mod_t mod, int response) {
	yutani_msg_buildx_key_bind_alloc(m);
	yutani_msg_buildx_key_bind(m, key,mod,response);
	yutani_msg_send(yctx, m);
}

/**
 * yutani_window_drag_start
 *
 * Begin a mouse-driven window movement action.
 * Typically used by decorators to start moving the window
 * when the user clicks and drags on the title bar.
 */
void yutani_window_drag_start(yutani_t * yctx, yutani_window_t * window) {
	yutani_msg_buildx_window_drag_start_alloc(m);
	yutani_msg_buildx_window_drag_start(m, window->wid);
	yutani_msg_send(yctx, m);
}

/**
 * yutani_window_drag_start_wid
 *
 * Same as above, but takes a wid (of a presumably-foreign window)
 * instead of a window pointer; used by the panel to initiate
 * window movement through a drop-down menu for other clients.
 */
void yutani_window_drag_start_wid(yutani_t * yctx, yutani_wid_t wid) {
	yutani_msg_buildx_window_drag_start_alloc(m);
	yutani_msg_buildx_window_drag_start(m, wid);
	yutani_msg_send(yctx, m);
}

/**
 * yutani_window_update_shape
 *
 * Change the window shaping threshold.
 * Allows partially-transparent windows to control whether they
 * should still receive mouse events in their transparent regions.
 */
void yutani_window_update_shape(yutani_t * yctx, yutani_window_t * window, int set_shape) {
	yutani_msg_buildx_window_update_shape_alloc(m);
	yutani_msg_buildx_window_update_shape(m, window->wid, set_shape);
	yutani_msg_send(yctx, m);
}

/**
 * yutani_window_warp_mouse
 *
 * Move the mouse to a locate relative to the window.
 * Only works with relative mouse cursor.
 * Useful for games.
 *
 * TODO: We still need a way to lock the cursor to a particular window.
 *       Even in games where warping happens quickly, we can still
 *       end up with the cursor outside of the window when a click happens.
 */
void yutani_window_warp_mouse(yutani_t * yctx, yutani_window_t * window, int32_t x, int32_t y) {
	yutani_msg_buildx_window_warp_mouse_alloc(m);
	yutani_msg_buildx_window_warp_mouse(m, window->wid, x, y);
	yutani_msg_send(yctx, m);
}

/**
 * yutani_window_show_mouse
 *
 * Set the cursor type. Used to change to risize and drag indicators.
 * Could be used to show a text insertion bar, or a link-clicking hand,
 * but those cursors need to be added in the server.
 *
 * TODO: We should add a way to use client-provided cursor textures.
 */
void yutani_window_show_mouse(yutani_t * yctx, yutani_window_t * window, int32_t show_mouse) {
	if (window->mouse_state != show_mouse) {
		window->mouse_state = show_mouse;
		yutani_msg_buildx_window_show_mouse_alloc(m);
		yutani_msg_buildx_window_show_mouse(m, window->wid, show_mouse);
		yutani_msg_send(yctx, m);
	}
}

/**
 * yutani_window_resize_start
 *
 * Start a mouse-driven window resize action.
 * Used by decorators.
 */
void yutani_window_resize_start(yutani_t * yctx, yutani_window_t * window, yutani_scale_direction_t direction) {
	yutani_msg_buildx_window_resize_start_alloc(m);
	yutani_msg_buildx_window_resize_start(m, window->wid, direction);
	yutani_msg_send(yctx, m);
}

/**
 * yutani_special_request
 *
 * Send one of the special request messages that aren't
 * important enough to get their own message types.
 *
 * (MAXIMIZE, PLEASE_CLOSE, CLIPBOARD)
 *
 * Note that, especially in the CLIPBOARD case, the
 * window does not to be set.
 */
void yutani_special_request(yutani_t * yctx, yutani_window_t * window, uint32_t request) {
	/* wid isn't necessary; if window is null, set to 0 */
	yutani_msg_buildx_special_request_alloc(m);
	yutani_msg_buildx_special_request(m, window ? window->wid : 0, request);
	yutani_msg_send(yctx, m);
}

/**
 * yutani_special_request_wid
 *
 * Same as above, but takes a wid instead of a window pointer,
 * for use with foreign windows.
 */
void yutani_special_request_wid(yutani_t * yctx, yutani_wid_t wid, uint32_t request) {
	/* For working with other applications' windows */
	yutani_msg_buildx_special_request_alloc(m);
	yutani_msg_buildx_special_request(m, wid, request);
	yutani_msg_send(yctx, m);
}

/**
 * yutani_set_clipboard
 *
 * Set the clipboard content.
 *
 * If the clipboard content is too large for a message,
 * it will be stored in a file and a special clipboard string
 * will be set to indicate the real contents are
 * in the file.
 *
 * To get the clipboard contents, send a CLIPBOARD special
 * request and wait for the CLIPBOARD response message.
 */
void yutani_set_clipboard(yutani_t * yctx, char * content) {
	/* Set clipboard contents */
	int len = strlen(content);
	if (len > 511) {
		char tmp_file[100];
		sprintf(tmp_file, "/tmp/.clipboard.%s", yctx->server_ident);
		FILE * tmp = fopen(tmp_file, "w+");
		fwrite(content, len, 1, tmp);
		fclose(tmp);

		char tmp_data[100];
		sprintf(tmp_data, "\002 %d", len);
		yutani_msg_buildx_clipboard_alloc(m, strlen(tmp_data));
		yutani_msg_buildx_clipboard(m, tmp_data);
		yutani_msg_send(yctx, m);
	} else {
		yutani_msg_buildx_clipboard_alloc(m, len);
		yutani_msg_buildx_clipboard(m, content);
		yutani_msg_send(yctx, m);
	}
}

void yutani_window_panel_size(yutani_t * yctx, yutani_wid_t wid, int32_t x, int32_t y, int32_t w, int32_t h) {
	yutani_msg_buildx_window_panel_size_alloc(m);
	yutani_msg_buildx_window_panel_size(m,wid,x,y,w,h);
	yutani_msg_send(yctx, m);
}

/**
 * yutani_open_clipboard
 *
 * Open the clipboard contents file.
 */
FILE * yutani_open_clipboard(yutani_t * yctx) {
	char tmp_file[100];
	sprintf(tmp_file, "/tmp/.clipboard.%s", yctx->server_ident);
	return fopen(tmp_file, "r");
}

/**
 * init_graphics_yutani
 *
 * Create a graphical context around a Yutani window.
 */
gfx_context_t * init_graphics_yutani(yutani_window_t * window) {
	gfx_context_t * out = malloc(sizeof(gfx_context_t));
	out->width  = window->width;
	out->height = window->height;
	out->stride = window->width * sizeof(uint32_t);
	out->depth  = 32;
	out->size   = GFX_H(out) * GFX_W(out) * GFX_B(out);
	out->buffer = window->buffer;
	out->backbuffer = out->buffer;
	out->clips  = NULL;
	return out;
}

/**
 * init_graphics_yutani_double_buffer
 *
 * Create a graphics context around a Yutani window
 * with a separate backing store for double-buffering.
 */
gfx_context_t *  init_graphics_yutani_double_buffer(yutani_window_t * window) {
	gfx_context_t * out = init_graphics_yutani(window);
	out->backbuffer = malloc(GFX_B(out) * GFX_W(out) * GFX_H(out));
	return out;
}

/**
 * reinit_graphics_yutani
 *
 * Reinitialize a graphics context, such as when
 * the window size changes.
 */
void reinit_graphics_yutani(gfx_context_t * out, yutani_window_t * window) {
	out->width  = window->width;
	out->height = window->height;
	out->stride = window->width * 4;
	out->depth  = 32;
	out->size   = GFX_H(out) * GFX_W(out) * GFX_B(out);

	if (out->clips && out->clips_size != out->height) {
		free(out->clips);
		out->clips = NULL;
		out->clips_size = 0;
	}

	if (out->buffer == out->backbuffer) {
		out->buffer = window->buffer;
		out->backbuffer = out->buffer;
	} else {
		out->buffer = window->buffer;
		out->backbuffer = realloc(out->backbuffer, GFX_B(out) * GFX_W(out) * GFX_H(out));
	}
}

/**
 * release_graphics_yutani
 *
 * Release a graphics context.
 * XXX: This seems to work generically for any graphics context?
 */
void release_graphics_yutani(gfx_context_t * gfx) {
	if (gfx->backbuffer != gfx->buffer) {
		free(gfx->backbuffer);
	}
	free(gfx);
}

void yutani_internal_refocus(yutani_t * yctx, yutani_window_t * window) {
	/* Check if a refocus is already in our queue to be processed */
	foreach(node, yctx->queued) {
		yutani_msg_t * out = (yutani_msg_t *)node->value;
		if (out->type == YUTANI_MSG_WINDOW_FOCUS_CHANGE) return;
	}
	/* Otherwise, produce an artificial one matching the reported focus state of the window */
	yutani_msg_t * msg = malloc(sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_focus_change));
	yutani_msg_buildx_window_focus_change(msg, window->wid, window->focused);
	list_insert(yctx->queued, msg);
}
