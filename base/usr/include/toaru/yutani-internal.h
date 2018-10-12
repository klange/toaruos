#pragma once

#include <_cheader.h>
#include <toaru/yutani.h>

_Begin_C_Header

#define YUTANI_SHMKEY(server_ident,buf,sz,win) sprintf(buf, "sys.%s.%d", server_ident, win->bufid);
#define YUTANI_SHMKEY_EXP(server_ident,buf,sz,bufid) sprintf(buf, "sys.%s.%d", server_ident, bufid);

#define yutani_msg_buildx_hello_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
#define yutani_msg_buildx_flip_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message) + sizeof(struct yutani_msg_flip)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
#define yutani_msg_buildx_welcome_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message) + sizeof(struct yutani_msg_welcome)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
#define yutani_msg_buildx_window_new_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_new)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
#define yutani_msg_buildx_window_new_flags_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_new_flags)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
#define yutani_msg_buildx_window_init_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_init)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
#define yutani_msg_buildx_window_close_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_close)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
#define yutani_msg_buildx_key_event_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message) + sizeof(struct yutani_msg_key_event)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
#define yutani_msg_buildx_mouse_event_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message) + sizeof(struct yutani_msg_mouse_event)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
#define yutani_msg_buildx_window_move_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_move)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
#define yutani_msg_buildx_window_stack_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_stack)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
#define yutani_msg_buildx_window_focus_change_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_focus_change)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
#define yutani_msg_buildx_window_mouse_event_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_mouse_event)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
#define yutani_msg_buildx_flip_region_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message) + sizeof(struct yutani_msg_flip_region)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
#define yutani_msg_buildx_window_resize_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_resize)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
#define yutani_msg_buildx_window_advertise_alloc(out, length) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_advertise) + length]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
#define yutani_msg_buildx_subscribe_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
#define yutani_msg_buildx_unsubscribe_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
#define yutani_msg_buildx_query_windows_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
#define yutani_msg_buildx_notify_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
#define yutani_msg_buildx_session_end_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
#define yutani_msg_buildx_window_focus_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_focus)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
#define yutani_msg_buildx_key_bind_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message) + sizeof(struct yutani_msg_key_bind)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
#define yutani_msg_buildx_window_drag_start_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_drag_start)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
#define yutani_msg_buildx_window_update_shape_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_update_shape)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
#define yutani_msg_buildx_window_warp_mouse_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_warp_mouse)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
#define yutani_msg_buildx_window_show_mouse_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_show_mouse)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
#define yutani_msg_buildx_window_resize_start_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message) + sizeof(struct yutani_msg_window_resize_start)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
#define yutani_msg_buildx_special_request_alloc(out) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message) + sizeof(struct yutani_msg_special_request)]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;
#define yutani_msg_buildx_clipboard_alloc(out, length) char _yutani_tmp_ ## LINE [sizeof(struct yutani_message) + sizeof(struct yutani_msg_clipboard)+length]; yutani_msg_t * out = (void *)&_yutani_tmp_ ## LINE;

extern void yutani_msg_buildx_hello(yutani_msg_t * msg);
extern void yutani_msg_buildx_flip(yutani_msg_t * msg, yutani_wid_t wid);
extern void yutani_msg_buildx_welcome(yutani_msg_t * msg, uint32_t width, uint32_t height);
extern void yutani_msg_buildx_window_new(yutani_msg_t * msg, uint32_t width, uint32_t height);
extern void yutani_msg_buildx_window_new_flags(yutani_msg_t * msg, uint32_t width, uint32_t height, uint32_t flags);
extern void yutani_msg_buildx_window_init(yutani_msg_t * msg, yutani_wid_t wid, uint32_t width, uint32_t height, uint32_t bufid);
extern void yutani_msg_buildx_window_close(yutani_msg_t * msg, yutani_wid_t wid);
extern void yutani_msg_buildx_key_event(yutani_msg_t * msg, yutani_wid_t wid, key_event_t * event, key_event_state_t * state);
extern void yutani_msg_buildx_mouse_event(yutani_msg_t * msg, yutani_wid_t wid, mouse_device_packet_t * event, int32_t type);
extern void yutani_msg_buildx_window_move(yutani_msg_t * msg, yutani_wid_t wid, int32_t x, int32_t y);
extern void yutani_msg_buildx_window_stack(yutani_msg_t * msg, yutani_wid_t wid, int z);
extern void yutani_msg_buildx_window_focus_change(yutani_msg_t * msg, yutani_wid_t wid, int focused);
extern void yutani_msg_buildx_window_mouse_event(yutani_msg_t * msg, yutani_wid_t wid, int32_t new_x, int32_t new_y, int32_t old_x, int32_t old_y, uint8_t buttons, uint8_t command);
extern void yutani_msg_buildx_flip_region(yutani_msg_t * msg, yutani_wid_t wid, int32_t x, int32_t y, int32_t width, int32_t height);
extern void yutani_msg_buildx_window_resize(yutani_msg_t * msg, uint32_t type, yutani_wid_t wid, uint32_t width, uint32_t height, uint32_t bufid, uint32_t flags);
extern void yutani_msg_buildx_window_advertise(yutani_msg_t * msg, yutani_wid_t wid, uint32_t flags, uint16_t * offsets, size_t length, char * data);
extern void yutani_msg_buildx_subscribe(yutani_msg_t * msg);
extern void yutani_msg_buildx_unsubscribe(yutani_msg_t * msg);
extern void yutani_msg_buildx_query_windows(yutani_msg_t * msg);
extern void yutani_msg_buildx_notify(yutani_msg_t * msg);
extern void yutani_msg_buildx_session_end(yutani_msg_t * msg);
extern void yutani_msg_buildx_window_focus(yutani_msg_t * msg, yutani_wid_t wid);
extern void yutani_msg_buildx_key_bind(yutani_msg_t * msg, kbd_key_t key, kbd_mod_t mod, int response);
extern void yutani_msg_buildx_window_drag_start(yutani_msg_t * msg, yutani_wid_t wid);
extern void yutani_msg_buildx_window_update_shape(yutani_msg_t * msg, yutani_wid_t wid, int set_shape);
extern void yutani_msg_buildx_window_warp_mouse(yutani_msg_t * msg, yutani_wid_t wid, int32_t x, int32_t y);
extern void yutani_msg_buildx_window_show_mouse(yutani_msg_t * msg, yutani_wid_t wid, int32_t show_mouse);
extern void yutani_msg_buildx_window_resize_start(yutani_msg_t * msg, yutani_wid_t wid, yutani_scale_direction_t direction);
extern void yutani_msg_buildx_special_request(yutani_msg_t * msg, yutani_wid_t wid, uint32_t request);
extern void yutani_msg_buildx_clipboard(yutani_msg_t * msg, char * content);

_End_C_Header
