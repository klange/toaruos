#pragma once

#include <alloca.h>

#define yutani_msg_buildx_hello_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
static inline void yutani_msg_buildx_hello(yutani_msg_t * msg) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_HELLO;
	msg->size  = sizeof(struct yutani_message);
}

#define yutani_msg_buildx_flip_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message) + sizeof(struct yutani_msg_flip)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
static inline void yutani_msg_buildx_flip(yutani_msg_t * msg, yutani_wid_t wid) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_FLIP;
	msg->size  = sizeof(struct yutani_message) + sizeof(struct yutani_msg_flip);

	struct yutani_msg_flip * mw = (void *)msg->data;

	mw->wid = wid;
}

#define yutani_msg_buildx_welcome_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message) + sizeof(struct yutani_msg_welcome)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
static inline void yutani_msg_buildx_welcome(yutani_msg_t * msg, uint32_t width, uint32_t height) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_WELCOME;
	msg->size  = sizeof(struct yutani_message) + sizeof(struct yutani_msg_welcome);

	struct yutani_msg_welcome * mw = (void *)msg->data;

	mw->display_width = width;
	mw->display_height = height;
}

#define yutani_msg_buildx_window_new_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_new)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
static inline void yutani_msg_buildx_window_new(yutani_msg_t * msg, uint32_t width, uint32_t height) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_WINDOW_NEW;
	msg->size  = sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_new);

	struct yutani_msg_window_new * mw = (void *)msg->data;

	mw->width = width;
	mw->height = height;
}

#define yutani_msg_buildx_window_new_flags_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_new_flags)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
static inline void yutani_msg_buildx_window_new_flags(yutani_msg_t * msg, uint32_t width, uint32_t height, uint32_t flags) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_WINDOW_NEW_FLAGS;
	msg->size  = sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_new_flags);

	struct yutani_msg_window_new_flags * mw = (void *)msg->data;

	mw->width = width;
	mw->height = height;
	mw->flags = flags;
}

#define yutani_msg_buildx_window_init_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_init)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
static inline void yutani_msg_buildx_window_init(yutani_msg_t * msg, yutani_wid_t wid, uint32_t width, uint32_t height, uint32_t bufid) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_WINDOW_INIT;
	msg->size  = sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_init);

	struct yutani_msg_window_init * mw = (void *)msg->data;

	mw->wid = wid;
	mw->width = width;
	mw->height = height;
	mw->bufid = bufid;
}

#define yutani_msg_buildx_window_close_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_close)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
static inline void yutani_msg_buildx_window_close(yutani_msg_t * msg, yutani_wid_t wid) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_WINDOW_CLOSE;
	msg->size  = sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_close);

	struct yutani_msg_window_close * mw = (void *)msg->data;

	mw->wid = wid;
}

#define yutani_msg_buildx_key_event_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message) + sizeof(struct yutani_msg_key_event)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
static inline void yutani_msg_buildx_key_event(yutani_msg_t * msg, yutani_wid_t wid, key_event_t * event, key_event_state_t * state) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_KEY_EVENT;
	msg->size  = sizeof(struct yutani_message) + sizeof(struct yutani_msg_key_event);

	struct yutani_msg_key_event * mw = (void *)msg->data;

	mw->wid = wid;
	memcpy(&mw->event, event, sizeof(key_event_t));
	memcpy(&mw->state, state, sizeof(key_event_state_t));
}

#define yutani_msg_buildx_mouse_event_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message) + sizeof(struct yutani_msg_mouse_event)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
static inline void yutani_msg_buildx_mouse_event(yutani_msg_t * msg, yutani_wid_t wid, mouse_device_packet_t * event, int32_t type) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_MOUSE_EVENT;
	msg->size  = sizeof(struct yutani_message) + sizeof(struct yutani_msg_mouse_event);

	struct yutani_msg_mouse_event * mw = (void *)msg->data;

	mw->wid = wid;
	memcpy(&mw->event, event, sizeof(mouse_device_packet_t));
	mw->type = type;
}

#define yutani_msg_buildx_window_move_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_move)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
static inline void yutani_msg_buildx_window_move(yutani_msg_t * msg, yutani_wid_t wid, int32_t x, int32_t y) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_WINDOW_MOVE;
	msg->size  = sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_move);

	struct yutani_msg_window_move * mw = (void *)msg->data;

	mw->wid = wid;
	mw->x = x;
	mw->y = y;
}

#define yutani_msg_buildx_window_stack_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_stack)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
static inline void yutani_msg_buildx_window_stack(yutani_msg_t * msg, yutani_wid_t wid, int z) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_WINDOW_STACK;
	msg->size  = sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_stack);

	struct yutani_msg_window_stack * mw = (void *)msg->data;

	mw->wid = wid;
	mw->z = z;
}

#define yutani_msg_buildx_window_focus_change_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_focus_change)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
static inline void yutani_msg_buildx_window_focus_change(yutani_msg_t * msg, yutani_wid_t wid, int focused) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_WINDOW_FOCUS_CHANGE;
	msg->size  = sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_focus_change);

	struct yutani_msg_window_focus_change * mw = (void *)msg->data;

	mw->wid = wid;
	mw->focused = focused;
}

#define yutani_msg_buildx_window_mouse_event_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_mouse_event)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
static inline void yutani_msg_buildx_window_mouse_event(yutani_msg_t * msg, yutani_wid_t wid, int32_t new_x, int32_t new_y, int32_t old_x, int32_t old_y, uint8_t buttons, uint8_t command) {
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
}

#define yutani_msg_buildx_flip_region_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message) + sizeof(struct yutani_msg_flip_region)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
static inline void yutani_msg_buildx_flip_region(yutani_msg_t * msg, yutani_wid_t wid, int32_t x, int32_t y, int32_t width, int32_t height) {
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

#define yutani_msg_buildx_window_resize_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_resize)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
static inline void yutani_msg_buildx_window_resize(yutani_msg_t * msg, uint32_t type, yutani_wid_t wid, uint32_t width, uint32_t height, uint32_t bufid) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = type;
	msg->size  = sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_resize);

	struct yutani_msg_window_resize * mw = (void *)msg->data;

	mw->wid = wid;
	mw->width = width;
	mw->height = height;
	mw->bufid = bufid;
}

#define yutani_msg_buildx_window_advertise_alloc(out, length) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_advertise) + length]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
static inline void yutani_msg_buildx_window_advertise(yutani_msg_t * msg, yutani_wid_t wid, uint32_t flags, uint16_t * offsets, size_t length, char * data) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_WINDOW_ADVERTISE;
	msg->size  = sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_advertise) + length;

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
}

#define yutani_msg_buildx_subscribe_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
static inline void yutani_msg_buildx_subscribe(yutani_msg_t * msg) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_SUBSCRIBE;
	msg->size  = sizeof(struct yutani_message);
}

#define yutani_msg_buildx_unsubscribe_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
static inline void yutani_msg_buildx_unsubscribe(yutani_msg_t * msg) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_UNSUBSCRIBE;
	msg->size  = sizeof(struct yutani_message);
}

#define yutani_msg_buildx_query_windows_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
static inline void yutani_msg_buildx_query_windows(yutani_msg_t * msg) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_QUERY_WINDOWS;
	msg->size  = sizeof(struct yutani_message);
}

#define yutani_msg_buildx_notify_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
static inline void yutani_msg_buildx_notify(yutani_msg_t * msg) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_NOTIFY;
	msg->size  = sizeof(struct yutani_message);
}

#define yutani_msg_buildx_session_end_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
static inline void yutani_msg_buildx_session_end(yutani_msg_t * msg) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_SESSION_END;
	msg->size  = sizeof(struct yutani_message);
}

#define yutani_msg_buildx_window_focus_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_focus)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
static inline void yutani_msg_buildx_window_focus(yutani_msg_t * msg, yutani_wid_t wid) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_WINDOW_FOCUS;
	msg->size  = sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_focus);

	struct yutani_msg_window_focus * mw = (void *)msg->data;

	mw->wid = wid;
}

#define yutani_msg_buildx_key_bind_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message) + sizeof(struct yutani_msg_key_bind)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
static inline void yutani_msg_buildx_key_bind(yutani_msg_t * msg, kbd_key_t key, kbd_mod_t mod, int response) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_KEY_BIND;
	msg->size  = sizeof(struct yutani_message) + sizeof(struct yutani_msg_key_bind);

	struct yutani_msg_key_bind * mw = (void *)msg->data;

	mw->key = key;
	mw->modifiers = mod;
	mw->response = response;
}

#define yutani_msg_buildx_window_drag_start_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_drag_start)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
static inline void yutani_msg_buildx_window_drag_start(yutani_msg_t * msg, yutani_wid_t wid) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_WINDOW_DRAG_START;
	msg->size  = sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_drag_start);

	struct yutani_msg_window_drag_start * mw = (void *)msg->data;

	mw->wid = wid;
}

#define yutani_msg_buildx_window_update_shape_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_update_shape)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
static inline void yutani_msg_buildx_window_update_shape(yutani_msg_t * msg, yutani_wid_t wid, int set_shape) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_WINDOW_UPDATE_SHAPE;
	msg->size  = sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_update_shape);

	struct yutani_msg_window_update_shape * mw = (void *)msg->data;

	mw->wid = wid;
	mw->set_shape = set_shape;
}

#define yutani_msg_buildx_window_warp_mouse_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_warp_mouse)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
static inline void yutani_msg_buildx_window_warp_mouse(yutani_msg_t * msg, yutani_wid_t wid, int32_t x, int32_t y) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_WINDOW_WARP_MOUSE;
	msg->size  = sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_warp_mouse);

	struct yutani_msg_window_warp_mouse * mw = (void *)msg->data;

	mw->wid = wid;
	mw->x = x;
	mw->y = y;
}

#define yutani_msg_buildx_window_show_mouse_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_show_mouse)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
static inline void yutani_msg_buildx_window_show_mouse(yutani_msg_t * msg, yutani_wid_t wid, int32_t show_mouse) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_WINDOW_SHOW_MOUSE;
	msg->size  = sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_show_mouse);

	struct yutani_msg_window_show_mouse * mw = (void *)msg->data;

	mw->wid = wid;
	mw->show_mouse = show_mouse;
}

#define yutani_msg_buildx_window_resize_start_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_resize_start)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
static inline void yutani_msg_buildx_window_resize_start(yutani_msg_t * msg, yutani_wid_t wid, yutani_scale_direction_t direction) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_WINDOW_RESIZE_START;
	msg->size  = sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_resize_start);

	struct yutani_msg_window_resize_start * mw = (void *)msg->data;

	mw->wid = wid;
	mw->direction = direction;
}

#define yutani_msg_buildx_special_request_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message) + sizeof(struct yutani_msg_special_request)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
static inline void yutani_msg_buildx_special_request(yutani_msg_t * msg, yutani_wid_t wid, uint32_t request) {
	msg->magic = YUTANI_MSG__MAGIC;
	msg->type  = YUTANI_MSG_SPECIAL_REQUEST;
	msg->size  = sizeof(struct yutani_message) + sizeof(struct yutani_msg_special_request);

	struct yutani_msg_special_request * sr = (void *)msg->data;

	sr->wid   = wid;
	sr->request = request;
}

