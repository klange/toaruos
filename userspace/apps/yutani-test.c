/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2015 Kevin Lange
 *
 * Yutani Test Tool
 *
 * Kinda like xev: Pops up a window and displays events in a
 * human-readable format.
 *
 */
#include <stdlib.h>
#include <assert.h>
#include <syscall.h>
#include <unistd.h>

#include "lib/yutani.h"
#include "lib/graphics.h"
#include "lib/pthread.h"

static int left, top, width, height;

static yutani_t * yctx;
static yutani_window_t * wina;
static gfx_context_t * ctx;
static int should_exit = 0;

const char * action_name(unsigned int action) {
	switch (action) {
		case KEY_ACTION_UP:
			return "up";
		case KEY_ACTION_DOWN:
			return "down";
		default:
			return "?";
	}
}

char * modifiers(unsigned int m) {
	static char out[] = "........";

	if (m & KEY_MOD_LEFT_CTRL)   out[0] = 'c'; else out[0] = '.';
	if (m & KEY_MOD_LEFT_SHIFT)  out[1] = 's'; else out[1] = '.';
	if (m & KEY_MOD_LEFT_ALT)    out[2] = 'a'; else out[2] = '.';
	if (m & KEY_MOD_LEFT_SUPER)  out[3] = 'x'; else out[3] = '.';
	if (m & KEY_MOD_RIGHT_CTRL)  out[4] = 'c'; else out[4] = '.';
	if (m & KEY_MOD_RIGHT_SHIFT) out[5] = 's'; else out[5] = '.';
	if (m & KEY_MOD_RIGHT_ALT)   out[6] = 'a'; else out[6] = '.';
	if (m & KEY_MOD_RIGHT_SUPER) out[7] = 'x'; else out[7] = '.';

	return out;
}

char * mouse_buttons(unsigned char button) {
	static char out[] = "....";

	if (button & YUTANI_MOUSE_BUTTON_LEFT)   out[0] = 'l'; else out[0] = '.';
	if (button & YUTANI_MOUSE_BUTTON_MIDDLE) out[1] = 'm'; else out[1] = '.';
	if (button & YUTANI_MOUSE_BUTTON_RIGHT)  out[2] = 'r'; else out[2] = '.';
	if (button & YUTANI_MOUSE_SCROLL_UP)     out[3] = 'u'; else \
	if (button & YUTANI_MOUSE_SCROLL_DOWN)   out[3] = 'd'; else out[3] = '.';

	return out;
}

const char * mouse_command(unsigned char type) {
	switch (type) {
		case (YUTANI_MOUSE_EVENT_CLICK):
			return "click";
		case (YUTANI_MOUSE_EVENT_DRAG ):
			return "drag";
		case (YUTANI_MOUSE_EVENT_RAISE):
			return "raise";
		case (YUTANI_MOUSE_EVENT_DOWN ):
			return "down";
		case (YUTANI_MOUSE_EVENT_MOVE ):
			return "move";
		case (YUTANI_MOUSE_EVENT_LEAVE):
			return "leave";
		case (YUTANI_MOUSE_EVENT_ENTER):
			return "enter";
		default:
			return "unknown";
	}
}

void redraw(void) {
	draw_fill(ctx, rgb(0,0,0));

	int w = width - 1, h = height - 1;

	draw_line(ctx, 0, w, 0, 0, rgb(255,255,255));
	draw_line(ctx, 0, w, h, h, rgb(255,255,255));

	draw_line(ctx, 0, 0, 0, h, rgb(255,255,255));
	draw_line(ctx, w, w, 0, h, rgb(255,255,255));
}

int main (int argc, char ** argv) {
	int show_cursor = 1;

	left   = 100;
	top    = 100;
	width  = 500;
	height = 500;

	yctx = yutani_init();
	wina = yutani_window_create(yctx, width, height);
	yutani_window_move(yctx, wina, left, top);

	ctx = init_graphics_yutani(wina);

	redraw();

	while (!should_exit) {
		yutani_msg_t * m = yutani_poll(yctx);
		if (m) {
			switch (m->type) {
				case YUTANI_MSG_KEY_EVENT:
					{
						struct yutani_msg_key_event * ke = (void*)m->data;
						fprintf(stderr, "Key Press (wid=%d) %s\n"
							"\tevent.action = %d\n"
							"\tevent.keycode = %d\n"
							"\tevent.modifiers = %s\n"
							"\tevent.key = %d (%c)\n",
							ke->wid,
							action_name(ke->event.action),
							ke->event.action,
							ke->event.keycode,
							modifiers(ke->event.modifiers),
							ke->event.key, ke->event.key);

						if (ke->event.key == 'm' && ke->event.action == KEY_ACTION_DOWN) {
							show_cursor = !show_cursor;
							yutani_window_show_mouse(yctx, wina, show_cursor);
						}
					}
					break;
				case YUTANI_MSG_WINDOW_MOUSE_EVENT:
					{
						struct yutani_msg_window_mouse_event * me = (void*)m->data;
						fprintf(stderr, "Mouse Event (wid=%d) %s\n"
							"\tnew = %d, %d\n"
							"\told = %d, %d\n"
							"\tbuttons = %s\n"
							"\tcommand = %d\n",
							me->wid,
							mouse_command(me->command),
							me->new_x, me->new_y,
							me->old_x, me->old_y,
							mouse_buttons(me->buttons),
							me->command);
					}
					break;
				case YUTANI_MSG_WINDOW_FOCUS_CHANGE:
					{
						struct yutani_msg_window_focus_change * fc = (void*)m->data;
						fprintf(stderr, "Focus Change (wid=%d) %s\n", fc->wid, fc->focused ? "on" : "off");
					}
					break;
				case YUTANI_MSG_WINDOW_MOVE:
					{
						struct yutani_msg_window_move * wm = (void*)m->data;
						fprintf(stderr, "Window Moved (wid=%d) %d, %d\n", wm->wid, wm->x, wm->y);
					}
					break;
				case YUTANI_MSG_RESIZE_OFFER:
					{
						struct yutani_msg_window_resize * wr = (void*)m->data;
						fprintf(stderr, "Resize Offer (wid=%d) %d x %d\n"
							"\tbufid = %d\n",
							wr->wid,
							wr->width, wr->height,
							wr->bufid);
					}
					break;
				case YUTANI_MSG_SESSION_END:
					should_exit = 1;
					break;
				default:
					break;
			}
		}
		free(m);
	}

	yutani_close(yctx, wina);

	return 0;
}
