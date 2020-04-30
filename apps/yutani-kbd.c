/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2015-2018 K. Lange
 *
 * yutani-test - Yutani Test Tool
 *
 * Kinda like xev: Pops up a window and displays events in a
 * human-readable format.
 *
 */
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>

#include <toaru/yutani.h>
#include <toaru/graphics.h>

static int left, top, width, height;

static yutani_t * yctx;
static yutani_window_t * wina;
static gfx_context_t * ctx;
static int should_exit = 0;

char * modifiers(unsigned int m) {
	static char out[] = "........";

	if (m & YUTANI_KEY_MODIFIER_LEFT_CTRL)   out[0] = 'c'; else out[0] = '.';
	if (m & YUTANI_KEY_MODIFIER_LEFT_SHIFT)  out[1] = 's'; else out[1] = '.';
	if (m & YUTANI_KEY_MODIFIER_LEFT_ALT)    out[2] = 'a'; else out[2] = '.';
	if (m & YUTANI_KEY_MODIFIER_LEFT_SUPER)  out[3] = 'x'; else out[3] = '.';
	if (m & YUTANI_KEY_MODIFIER_RIGHT_CTRL)  out[4] = 'c'; else out[4] = '.';
	if (m & YUTANI_KEY_MODIFIER_RIGHT_SHIFT) out[5] = 's'; else out[5] = '.';
	if (m & YUTANI_KEY_MODIFIER_RIGHT_ALT)   out[6] = 'a'; else out[6] = '.';
	if (m & YUTANI_KEY_MODIFIER_RIGHT_SUPER) out[7] = 'x'; else out[7] = '.';

	return out;
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
	left   = 100;
	top    = 100;
	width  = 500;
	height = 500;

	yctx = yutani_init();
	wina = yutani_window_create(yctx, width, height);
	yutani_window_move(yctx, wina, left, top);

	ctx = init_graphics_yutani(wina);

	redraw();

	char keys[256] = {0};

	printf("\033[H\033[2J");

	while (!should_exit) {
		yutani_msg_t * m = yutani_poll(yctx);
		if (m) {
			switch (m->type) {
				case YUTANI_MSG_KEY_EVENT:
					{
						struct yutani_msg_key_event * ke = (void*)m->data;
						if (ke->event.keycode >= 'a' && ke->event.keycode < 'z') {
							keys[ke->event.keycode] = (ke->event.action == KEY_ACTION_DOWN);
						}
						printf("\033[1;1H");
						for (int i = 'a'; i < 'z'; ++i) {
							printf("\033[%dm%c ", keys[i] ? 0 : 31, i);
						}
						fflush(stdout);
					}
					break;
				case YUTANI_MSG_WINDOW_CLOSE:
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

