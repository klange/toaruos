/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014 Kevin Lange
 */
#include <string.h>
#include <stdlib.h>
#include <syscall.h>

#include "yutani.h"
#include "pex.h"
#include "graphics.h"
#include "kbd.h"
#include "mouse.h"
#include "hashmap.h"
#include "list.h"

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

size_t yutani_query(yutani_t * y) {
	if (y->queued->length > 0) return 1;
	return pex_query(y->sock);
}

yutani_msg_t * yutani_poll(yutani_t * y) {
	yutani_msg_t * out;

	if (y->queued->length > 0) {
		node_t * node = list_dequeue(y->queued);
		out = (yutani_msg_t *)node->value;
		free(node);
		return out;
	}

	size_t size;
	{
		char tmp[MAX_PACKET_SIZE];
		size = pex_recv(y->sock, tmp);
		out = malloc(size);
		memcpy(out, tmp, size);
	}

	return out;
}

yutani_msg_t * yutani_poll_async(yutani_t * y) {
	if (yutani_query(y) > 0) {
		return yutani_poll(y);
	}
	return NULL;
}

yutani_msg_t * yutani_msg_build_hello(void) {
	size_t s = sizeof(struct yutani_message);
	yutani_msg_t * msg = malloc(s);

	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_HELLO;
	msg->size  = s;

	return msg;
}

yutani_msg_t * yutani_msg_build_flip(yutani_wid_t wid) {
	size_t s = sizeof(struct yutani_message) + sizeof(struct yutani_msg_flip);
	yutani_msg_t * msg = malloc(s);

	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_FLIP;
	msg->size  = s;

	struct yutani_msg_flip * mw = (void *)msg->data;

	mw->wid = wid;

	return msg;
}

yutani_msg_t * yutani_msg_build_welcome(uint32_t width, uint32_t height) {
	size_t s = sizeof(struct yutani_message) + sizeof(struct yutani_msg_welcome);
	yutani_msg_t * msg = malloc(s);

	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_WELCOME;
	msg->size  = s;

	struct yutani_msg_welcome * mw = (void *)msg->data;

	mw->display_width = width;
	mw->display_height = height;

	return msg;
}

yutani_msg_t * yutani_msg_build_window_new(uint32_t width, uint32_t height) {
	size_t s = sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_new);
	yutani_msg_t * msg = malloc(s);

	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_WINDOW_NEW;
	msg->size  = s;

	struct yutani_msg_window_new * mw = (void *)msg->data;

	mw->width = width;
	mw->height = height;

	return msg;
}

yutani_msg_t * yutani_msg_build_window_init(yutani_wid_t wid, uint32_t width, uint32_t height, uint32_t bufid) {
	size_t s = sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_init);
	yutani_msg_t * msg = malloc(s);

	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_WINDOW_INIT;
	msg->size  = s;

	struct yutani_msg_window_init * mw = (void *)msg->data;

	mw->wid = wid;
	mw->width = width;
	mw->height = height;
	mw->bufid = bufid;

	return msg;
}

yutani_msg_t * yutani_msg_build_window_close(yutani_wid_t wid) {
	size_t s = sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_close);
	yutani_msg_t * msg = malloc(s);

	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_WINDOW_CLOSE;
	msg->size  = s;

	struct yutani_msg_window_close * mw = (void *)msg->data;

	mw->wid = wid;

	return msg;
}

yutani_msg_t * yutani_msg_build_key_event(yutani_wid_t wid, key_event_t * event, key_event_state_t * state) {
	size_t s = sizeof(struct yutani_message) + sizeof(struct yutani_msg_key_event);
	yutani_msg_t * msg = malloc(s);

	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_KEY_EVENT;
	msg->size  = s;

	struct yutani_msg_key_event * mw = (void *)msg->data;

	mw->wid = wid;
	memcpy(&mw->event, event, sizeof(key_event_t));
	memcpy(&mw->state, state, sizeof(key_event_state_t));

	return msg;
}

yutani_msg_t * yutani_msg_build_mouse_event(yutani_wid_t wid, mouse_device_packet_t * event, int32_t type) {
	size_t s = sizeof(struct yutani_message) + sizeof(struct yutani_msg_mouse_event);
	yutani_msg_t * msg = malloc(s);

	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_MOUSE_EVENT;
	msg->size  = s;

	struct yutani_msg_mouse_event * mw = (void *)msg->data;

	mw->wid = wid;
	memcpy(&mw->event, event, sizeof(mouse_device_packet_t));
	mw->type = type;

	return msg;
}

yutani_msg_t * yutani_msg_build_window_move(yutani_wid_t wid, int32_t x, int32_t y) {
	size_t s = sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_move);
	yutani_msg_t * msg = malloc(s);

	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_WINDOW_MOVE;
	msg->size  = s;

	struct yutani_msg_window_move * mw = (void *)msg->data;

	mw->wid = wid;
	mw->x = x;
	mw->y = y;

	return msg;
}

yutani_msg_t * yutani_msg_build_window_stack(yutani_wid_t wid, int z) {
	size_t s = sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_stack);
	yutani_msg_t * msg = malloc(s);

	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_WINDOW_STACK;
	msg->size  = s;

	struct yutani_msg_window_stack * mw = (void *)msg->data;

	mw->wid = wid;
	mw->z = z;

	return msg;
}

yutani_msg_t * yutani_msg_build_window_focus_change(yutani_wid_t wid, int focused) {
	size_t s = sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_focus_change);
	yutani_msg_t * msg = malloc(s);

	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_WINDOW_FOCUS_CHANGE;
	msg->size  = s;

	struct yutani_msg_window_focus_change * mw = (void *)msg->data;

	mw->wid = wid;
	mw->focused = focused;

	return msg;
}

yutani_msg_t * yutani_msg_build_window_mouse_event(yutani_wid_t wid, int32_t new_x, int32_t new_y, int32_t old_x, int32_t old_y, uint8_t buttons, uint8_t command) {
	size_t s = sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_mouse_event);
	yutani_msg_t * msg = malloc(s);

	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_WINDOW_MOUSE_EVENT;
	msg->size  = s;

	struct yutani_msg_window_mouse_event * mw = (void *)msg->data;

	mw->wid = wid;
	mw->new_x = new_x;
	mw->new_y = new_y;
	mw->old_x = old_x;
	mw->old_y = old_y;
	mw->buttons = buttons;
	mw->command = command;

	return msg;
}

yutani_msg_t * yutani_msg_build_flip_region(yutani_wid_t wid, int32_t x, int32_t y, int32_t width, int32_t height) {
	size_t s = sizeof(struct yutani_message) + sizeof(struct yutani_msg_flip_region);
	yutani_msg_t * msg = malloc(s);

	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_FLIP_REGION;
	msg->size  = s;

	struct yutani_msg_flip_region * mw = (void *)msg->data;

	mw->wid = wid;
	mw->x = x;
	mw->y = y;
	mw->width = width;
	mw->height = height;

	return msg;
}

yutani_msg_t * yutani_msg_build_window_resize(uint32_t type, yutani_wid_t wid, uint32_t width, uint32_t height, uint32_t bufid) {
	size_t s = sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_resize);
	yutani_msg_t * msg = malloc(s);

	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = type;
	msg->size  = s;

	struct yutani_msg_window_resize * mw = (void *)msg->data;

	mw->wid = wid;
	mw->width = width;
	mw->height = height;
	mw->bufid = bufid;

	return msg;
}

yutani_msg_t * yutani_msg_build_window_advertise(yutani_wid_t wid, uint32_t flags, uint16_t * offsets, size_t length, char * data) {
	size_t s = sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_advertise) + length;
	yutani_msg_t * msg = malloc(s);

	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_WINDOW_ADVERTISE;
	msg->size  = s;

	struct yutani_msg_window_advertise * mw = (void *)msg->data;

	mw->wid = wid;
	mw->flags = flags;
	mw->size = length;
	if (offsets) {
		memcpy(mw->offsets, offsets, sizeof(uint16_t)*5);
	} else {
		memset(mw->offsets, 0, sizeof(uint16_t)*5);
	}
	if (data) {
		memcpy(mw->strings, data, mw->size);
	}

	return msg;
}

yutani_msg_t * yutani_msg_build_subscribe(void) {
	size_t s = sizeof(struct yutani_message);
	yutani_msg_t * msg = malloc(s);

	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_SUBSCRIBE;
	msg->size  = s;

	return msg;
}

yutani_msg_t * yutani_msg_build_unsubscribe(void) {
	size_t s = sizeof(struct yutani_message);
	yutani_msg_t * msg = malloc(s);

	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_UNSUBSCRIBE;
	msg->size  = s;

	return msg;
}

yutani_msg_t * yutani_msg_build_query_windows(void) {
	size_t s = sizeof(struct yutani_message);
	yutani_msg_t * msg = malloc(s);

	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_QUERY_WINDOWS;
	msg->size  = s;

	return msg;
}

yutani_msg_t * yutani_msg_build_notify(void) {
	size_t s = sizeof(struct yutani_message);
	yutani_msg_t * msg = malloc(s);

	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_NOTIFY;
	msg->size  = s;

	return msg;
}

yutani_msg_t * yutani_msg_build_session_end(void) {
	size_t s = sizeof(struct yutani_message);
	yutani_msg_t * msg = malloc(s);

	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_SESSION_END;
	msg->size  = s;

	return msg;
}

yutani_msg_t * yutani_msg_build_window_focus(yutani_wid_t wid) {
	size_t s = sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_focus);
	yutani_msg_t * msg = malloc(s);

	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_WINDOW_FOCUS;
	msg->size  = s;

	struct yutani_msg_window_focus * mw = (void *)msg->data;

	mw->wid = wid;

	return msg;
}

yutani_msg_t * yutani_msg_build_key_bind(kbd_key_t key, kbd_mod_t mod, int response) {
	size_t s = sizeof(struct yutani_message) + sizeof(struct yutani_msg_key_bind);
	yutani_msg_t * msg = malloc(s);

	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_KEY_BIND;
	msg->size  = s;

	struct yutani_msg_key_bind * mw = (void *)msg->data;

	mw->key = key;
	mw->modifiers = mod;
	mw->response = response;

	return msg;
}

yutani_msg_t * yutani_msg_build_window_drag_start(yutani_wid_t wid) {
	size_t s = sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_drag_start);
	yutani_msg_t * msg = malloc(s);

	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_WINDOW_DRAG_START;
	msg->size  = s;

	struct yutani_msg_window_drag_start * mw = (void *)msg->data;

	mw->wid = wid;

	return msg;
}

yutani_msg_t * yutani_msg_build_window_update_shape(yutani_wid_t wid, int set_shape) {
	size_t s = sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_update_shape);
	yutani_msg_t * msg = malloc(s);

	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_WINDOW_UPDATE_SHAPE;
	msg->size  = s;

	struct yutani_msg_window_update_shape * mw = (void *)msg->data;

	mw->wid = wid;
	mw->set_shape = set_shape;

	return msg;
}

yutani_msg_t * yutani_msg_build_window_warp_mouse(yutani_wid_t wid, int32_t x, int32_t y) {
	size_t s = sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_warp_mouse);
	yutani_msg_t * msg = malloc(s);

	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_WINDOW_WARP_MOUSE;
	msg->size  = s;

	struct yutani_msg_window_warp_mouse * mw = (void *)msg->data;

	mw->wid = wid;
	mw->x = x;
	mw->y = y;

	return msg;
}

yutani_msg_t * yutani_msg_build_window_show_mouse(yutani_wid_t wid, int32_t show_mouse) {
	size_t s = sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_show_mouse);
	yutani_msg_t * msg = malloc(s);

	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_WINDOW_SHOW_MOUSE;
	msg->size  = s;

	struct yutani_msg_window_show_mouse * mw = (void *)msg->data;

	mw->wid = wid;
	mw->show_mouse = show_mouse;

	return msg;
}

yutani_msg_t * yutani_msg_build_window_resize_start(yutani_wid_t wid, yutani_scale_direction_t direction) {
	size_t s = sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_resize_start);
	yutani_msg_t * msg = malloc(s);

	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_WINDOW_RESIZE_START;
	msg->size  = s;

	struct yutani_msg_window_resize_start * mw = (void *)msg->data;

	mw->wid = wid;
	mw->direction = direction;

	return msg;
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

yutani_t * yutani_init(void) {
	/* XXX: Display, etc? */
	if (!getenv("DISPLAY")) {
		return NULL;
	}

	char * server_name = getenv("DISPLAY");
	FILE * c = pex_connect(server_name);

	if (!c) return NULL; /* Connection failed. */

	yutani_t * y = yutani_context_create(c);
	yutani_msg_t * m = yutani_msg_build_hello();
	int result = yutani_msg_send(y, m);
	free(m);

	m = yutani_wait_for(y, YUTANI_MSG_WELCOME);
	struct yutani_msg_welcome * mw = (void *)&m->data;
	y->display_width = mw->display_width;
	y->display_height = mw->display_height;
	y->server_ident = server_name;
	free(m);

	return y;
}

yutani_window_t * yutani_window_create(yutani_t * y, int width, int height) {
	yutani_window_t * win = malloc(sizeof(yutani_window_t));

	yutani_msg_t * m = yutani_msg_build_window_new(width, height);
	int result = yutani_msg_send(y, m);
	free(m);

	m = yutani_wait_for(y, YUTANI_MSG_WINDOW_INIT);
	struct yutani_msg_window_init * mw = (void *)&m->data;

	win->width = mw->width;
	win->height = mw->height;
	win->bufid = mw->bufid;
	win->wid = mw->wid;
	win->focused = 0;

	hashmap_set(y->windows, (void*)win->wid, win);

	char key[1024];
	YUTANI_SHMKEY(y->server_ident, key, 1024, win);

	size_t size = (width * height * 4);
	win->buffer = (uint8_t *)syscall_shm_obtain(key, &size);
	return win;
}

void yutani_flip(yutani_t * y, yutani_window_t * win) {
	yutani_msg_t * m = yutani_msg_build_flip(win->wid);
	int result = yutani_msg_send(y, m);
	free(m);
}

void yutani_flip_region(yutani_t * yctx, yutani_window_t * win, int32_t x, int32_t y, int32_t width, int32_t height) {
	yutani_msg_t * m = yutani_msg_build_flip_region(win->wid, x, y, width, height);
	int result = yutani_msg_send(yctx, m);
	free(m);
}

void yutani_close(yutani_t * y, yutani_window_t * win) {
	yutani_msg_t * m = yutani_msg_build_window_close(win->wid);
	int result = yutani_msg_send(y, m);
	free(m);

	/* Now destroy our end of the window */
	{
		char key[1024];
		YUTANI_SHMKEY_EXP(y->server_ident, key, 1024, win->bufid);
		syscall_shm_release(key);
	}

	hashmap_remove(y->windows, (void*)win->wid);
	free(win);
}

void yutani_window_move(yutani_t * yctx, yutani_window_t * window, int x, int y) {
	yutani_msg_t * m = yutani_msg_build_window_move(window->wid, x, y);
	int reuslt = yutani_msg_send(yctx, m);
	free(m);
}

void yutani_set_stack(yutani_t * yctx, yutani_window_t * window, int z) {
	yutani_msg_t * m = yutani_msg_build_window_stack(window->wid, z);
	int reuslt = yutani_msg_send(yctx, m);
	free(m);
}

void yutani_window_resize(yutani_t * yctx, yutani_window_t * window, uint32_t width, uint32_t height) {
	yutani_msg_t * m = yutani_msg_build_window_resize(YUTANI_MSG_RESIZE_REQUEST, window->wid, width, height, 0);
	int result = yutani_msg_send(yctx, m);
	free(m);
}

void yutani_window_resize_offer(yutani_t * yctx, yutani_window_t * window, uint32_t width, uint32_t height) {
	yutani_msg_t * m = yutani_msg_build_window_resize(YUTANI_MSG_RESIZE_OFFER, window->wid, width, height, 0);
	int result = yutani_msg_send(yctx, m);
	free(m);
}

void yutani_window_resize_accept(yutani_t * yctx, yutani_window_t * window, uint32_t width, uint32_t height) {
	yutani_msg_t * m = yutani_msg_build_window_resize(YUTANI_MSG_RESIZE_ACCEPT, window->wid, width, height, 0);
	int result = yutani_msg_send(yctx, m);
	free(m);

	/* Now wait for the new bufid */
	m = yutani_wait_for(yctx, YUTANI_MSG_RESIZE_BUFID);
	struct yutani_msg_window_resize * wr = (void*)m->data;

	if (window->wid != wr->wid) {
		/* I am not sure what to do here. */
		return;
	}

	/* Update the window */
	window->width = wr->width;
	window->height = wr->height;
	window->oldbufid = window->bufid;
	window->bufid = wr->bufid;
	free(m);

	/* Allocate the buffer */
	{
		char key[1024];
		YUTANI_SHMKEY(yctx->server_ident, key, 1024, window);

		size_t size = (window->width * window->height * 4);
		window->buffer = (uint8_t *)syscall_shm_obtain(key, &size);
	}
}

void yutani_window_resize_done(yutani_t * yctx, yutani_window_t * window) {
	/* Destroy the old buffer */
	{
		char key[1024];
		YUTANI_SHMKEY_EXP(yctx->server_ident, key, 1024, window->oldbufid);
		syscall_shm_release(key);
	}

	yutani_msg_t * m = yutani_msg_build_window_resize(YUTANI_MSG_RESIZE_DONE, window->wid, window->width, window->height, window->bufid);
	int result = yutani_msg_send(yctx, m);
	free(m);
}

void yutani_window_advertise(yutani_t * yctx, yutani_window_t * window, char * name) {

	uint32_t flags = 0; /* currently, no client flags */
	uint16_t offsets[5] = {0,0,0,0,0};
	uint32_t length = 0;
	char * strings;

	if (!name) {
		length = 1;
		strings = " ";
	} else {
		length = strlen(name) + 1;
		strings = name;
		/* All the other offsets will point to null characters */
		offsets[1] = strlen(name);
		offsets[2] = strlen(name);
		offsets[3] = strlen(name);
		offsets[4] = strlen(name);
	}

	yutani_msg_t * m = yutani_msg_build_window_advertise(window->wid, flags, offsets, length, strings);
	int result = yutani_msg_send(yctx, m);
	free(m);
}

void yutani_window_advertise_icon(yutani_t * yctx, yutani_window_t * window, char * name, char * icon) {

	uint32_t flags = window->focused; /* currently no client flags */
	uint16_t offsets[5] = {0,0,0,0,0};
	uint32_t length = strlen(name) + strlen(icon) + 2;
	char * strings = malloc(length);

	if (name) {
		memcpy(&strings[0], name, strlen(name)+1);
		offsets[0] = 0;
		offsets[1] = strlen(name);
		offsets[2] = strlen(name);
		offsets[3] = strlen(name);
		offsets[4] = strlen(name);
	}
	if (icon) {
		memcpy(&strings[offsets[1]+1], icon, strlen(icon)+1);
		offsets[1] = strlen(name)+1;
		offsets[2] = strlen(name)+1+strlen(icon);
		offsets[3] = strlen(name)+1+strlen(icon);
		offsets[4] = strlen(name)+1+strlen(icon);
	}

	yutani_msg_t * m = yutani_msg_build_window_advertise(window->wid, flags, offsets, length, strings);
	int result = yutani_msg_send(yctx, m);
	free(m);
	free(strings);
}

void yutani_subscribe_windows(yutani_t * y) {
	yutani_msg_t * m = yutani_msg_build_subscribe();
	int result = yutani_msg_send(y, m);
	free(m);
}

void yutani_unsubscribe_windows(yutani_t * y) {
	yutani_msg_t * m = yutani_msg_build_unsubscribe();
	int result = yutani_msg_send(y, m);
	free(m);
}

void yutani_query_windows(yutani_t * y) {
	yutani_msg_t * m = yutani_msg_build_query_windows();
	int result = yutani_msg_send(y, m);
	free(m);
}

void yutani_session_end(yutani_t * y) {
	yutani_msg_t * m = yutani_msg_build_session_end();
	int result = yutani_msg_send(y, m);
	free(m);
}

void yutani_focus_window(yutani_t * yctx, yutani_wid_t wid) {
	yutani_msg_t * m = yutani_msg_build_window_focus(wid);
	int result = yutani_msg_send(yctx, m);
	free(m);
}

void yutani_key_bind(yutani_t * yctx, kbd_key_t key, kbd_mod_t mod, int response) {
	yutani_msg_t * m = yutani_msg_build_key_bind(key,mod,response);
	int result = yutani_msg_send(yctx, m);
	free(m);
}

void yutani_window_drag_start(yutani_t * yctx, yutani_window_t * window) {
	yutani_msg_t * m = yutani_msg_build_window_drag_start(window->wid);
	int result = yutani_msg_send(yctx, m);
	free(m);
}

void yutani_window_update_shape(yutani_t * yctx, yutani_window_t * window, int set_shape) {
	yutani_msg_t * m = yutani_msg_build_window_update_shape(window->wid, set_shape);
	int result = yutani_msg_send(yctx, m);
	free(m);
}

void yutani_window_warp_mouse(yutani_t * yctx, yutani_window_t * window, int32_t x, int32_t y) {
	yutani_msg_t * m = yutani_msg_build_window_warp_mouse(window->wid, x, y);
	int result = yutani_msg_send(yctx, m);
	free(m);
}

void yutani_window_show_mouse(yutani_t * yctx, yutani_window_t * window, int32_t show_mouse) {
	yutani_msg_t * m = yutani_msg_build_window_show_mouse(window->wid, show_mouse);
	int result = yutani_msg_send(yctx, m);
	free(m);
}

void yutani_window_resize_start(yutani_t * yctx, yutani_window_t * window, yutani_scale_direction_t direction) {
	yutani_msg_t * m = yutani_msg_build_window_resize_start(window->wid, direction);
	int result = yutani_msg_send(yctx, m);
	free(m);
}


gfx_context_t * init_graphics_yutani(yutani_window_t * window) {
	gfx_context_t * out = malloc(sizeof(gfx_context_t));
	out->width  = window->width;
	out->height = window->height;
	out->depth  = 32;
	out->size   = GFX_H(out) * GFX_W(out) * GFX_B(out);
	out->buffer = window->buffer;
	out->backbuffer = out->buffer;
	return out;
}

gfx_context_t *  init_graphics_yutani_double_buffer(yutani_window_t * window) {
	gfx_context_t * out = init_graphics_yutani(window);
	out->backbuffer = malloc(GFX_B(out) * GFX_W(out) * GFX_H(out));
	return out;
}

void reinit_graphics_yutani(gfx_context_t * out, yutani_window_t * window) {
	out->width  = window->width;
	out->height = window->height;
	out->depth  = 32;
	out->size   = GFX_H(out) * GFX_W(out) * GFX_B(out);
	if (out->buffer == out->backbuffer) {
		out->buffer = window->buffer;
		out->backbuffer = out->buffer;
	} else {
		out->buffer = window->buffer;
		out->backbuffer = realloc(out->backbuffer, GFX_B(out) * GFX_W(out) * GFX_H(out));
	}
}

