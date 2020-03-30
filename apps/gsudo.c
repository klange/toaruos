/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014-2017 K. Lange
 *
 * gsudo - graphical implementation of sudo
 *
 * probably even less secure than the original
 *
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include <toaru/auth.h>
#include <toaru/yutani.h>
#include <toaru/graphics.h>
#include <toaru/sdf.h>
#include <toaru/button.h>

#define main __main_unused
#include "sudo.c"
#undef main

#define FONT_SIZE_TITLE 20
#define FONT_SIZE_MAIN 16
#define FONT_SIZE_PASSWD 25
#define FONT_COLOR (rgb(0,0,0))
#define FONT_RED (rgb(250,0,0))
#define BUTTON_HEIGHT 28
#define BUTTON_WIDTH 120
#define BUTTON_PADDING 18

static yutani_t * yctx;
static gfx_context_t * ctx;
static yutani_window_t * window;

struct TTKButton _button_cancel = {
	0, 0, BUTTON_WIDTH, BUTTON_HEIGHT, "Cancel", 0
};

struct TTKButton _button_authenticate = {
	410 - BUTTON_WIDTH - BUTTON_PADDING, 260, BUTTON_WIDTH, BUTTON_HEIGHT, "Authenticate", 0
};

struct TTKButton * _down_button = NULL;

static int in_button(struct TTKButton * button, struct yutani_msg_window_mouse_event * me) {
	if (me->new_y >= button->y && me->new_y < button->y  + button->height) {
		if (me->new_x >= button->x && me->new_x < button->x + button->width) {
			return 1;
		}
	}
	return 0;
}

static int set_hilight(struct TTKButton * button, int hilight) {
	if (!button && (_button_cancel.hilight || _button_authenticate.hilight)) {
		_button_cancel.hilight = 0;
		_button_authenticate.hilight = 0;
		return 1;
	} else if (button && (button->hilight != hilight)) {
		_button_cancel.hilight = 0;
		_button_authenticate.hilight = 0;
		button->hilight = hilight;
		return 1;
	}
	return 0;
}

static void redraw(char * username, char * password, int fails, char * argv[]) {

	sprite_t * prompt = create_sprite(420, 320, ALPHA_EMBEDDED);
	gfx_context_t * myctx = init_graphics_sprite(prompt);
	draw_fill(myctx, rgba(0,0,0,0));

	/* Draw rounded rectangle */
	draw_rounded_rectangle(myctx, 10, 10, prompt->width - 20, prompt->height - 20, 10, rgba(0,0,0,200));
	blur_context_box(myctx, 10);
	blur_context_box(myctx, 10);
	draw_rounded_rectangle(myctx, 10, 10, prompt->width - 20, prompt->height - 20, 10, rgb(239,238,232));

	/* Draw prompt messages */
	draw_sdf_string(myctx, 30, 30, "Authentication Required", FONT_SIZE_TITLE, FONT_COLOR, SDF_FONT_THIN);
	draw_sdf_string(myctx, 30, 54, "Authentication is required to run the application", FONT_SIZE_MAIN, FONT_COLOR, SDF_FONT_THIN);
	draw_sdf_string(myctx, 30, 72, argv[1], FONT_SIZE_MAIN, FONT_COLOR, SDF_FONT_THIN);

	char prompt_message[512];
	sprintf(prompt_message, "Enter password for '%s'", username);
	draw_sdf_string(myctx, 30, 100, prompt_message, FONT_SIZE_MAIN, FONT_COLOR, SDF_FONT_THIN);

	if (fails) {
		sprintf(prompt_message, "Try again. %d failures.", fails);
		draw_sdf_string(myctx, 30, 146, prompt_message, FONT_SIZE_MAIN, FONT_RED, SDF_FONT_THIN);
	}

	struct gradient_definition edge = {30, 114, rgb(0,120,220), rgb(0,120,220)};
	draw_rounded_rectangle_pattern(myctx, 30, 120, prompt->width - 70, 26, 4, gfx_vertical_gradient_pattern, &edge);
	draw_rounded_rectangle(myctx, 32, 122, prompt->width - 74, 22, 3, rgb(250,250,250));

	char password_circles[512] = {0};;
	strcpy(password_circles, "");
	for (unsigned int i = 0; i < strlen(password) && i < 512/4; ++i) {
		strcat(password_circles, "\007");
	}
	draw_sdf_string(myctx, 33, 118, password_circles, FONT_SIZE_PASSWD, FONT_COLOR, SDF_FONT_THIN);

	draw_fill(ctx, rgba(0,0,0,200));
	draw_sprite(ctx, prompt, (ctx->width - prompt->width) / 2, (ctx->height - prompt->height) / 2);

	_button_cancel.x = (410 - 2 * (BUTTON_WIDTH + BUTTON_PADDING)) + (ctx->width - prompt->width) / 2;
	_button_cancel.y = 260 + (ctx->height - prompt->height) / 2;
	_button_authenticate.x = (410 - (BUTTON_WIDTH + BUTTON_PADDING)) + (ctx->width - prompt->width) / 2;
	_button_authenticate.y = 260 + (ctx->height - prompt->height) / 2;
	ttk_button_draw(ctx, &_button_cancel);
	ttk_button_draw(ctx, &_button_authenticate);

	sprite_free(prompt);
	free(myctx);

	flip(ctx);
	yutani_flip(yctx, window);
}

static int graphical_callback(char * username, char * password, int fails, char * argv[]) {
	int i = 0;

	redraw(username, password, fails, argv);

	while (1) {

		yutani_msg_t * msg = yutani_poll(yctx);

		switch (msg->type) {
			case YUTANI_MSG_KEY_EVENT:
				{
					struct yutani_msg_key_event * ke = (void*)msg->data;
					if (ke->event.action == KEY_ACTION_DOWN) {
						if (ke->event.keycode == KEY_ESCAPE) {
							return 1;
						}
						if (ke->event.keycode == '\n') {
							return 0;
						} else  if (ke->event.key == 8) {
							if (i > 0) i--;
							password[i] = '\0';
						} else if (ke->event.key) {
							password[i] = ke->event.key;
							password[i+1] = '\0';
							i++;
						}
						redraw(username, password, fails, argv);
					}
				}
				break;
			case YUTANI_MSG_WINDOW_MOUSE_EVENT:
				{
					struct yutani_msg_window_mouse_event * me = (void*)msg->data;
					int r = 0;
					if (me->wid == window->wid) {
						if (me->command == YUTANI_MOUSE_EVENT_DOWN) {
							if (in_button(&_button_cancel, me)) {
								r |= set_hilight(&_button_cancel, 2);
								_down_button = &_button_cancel;
							} else if (in_button(&_button_authenticate, me)) {
								r |= set_hilight(&_button_authenticate, 2);
								_down_button = &_button_authenticate;
							}
						} else if (me->command == YUTANI_MOUSE_EVENT_RAISE || me->command == YUTANI_MOUSE_EVENT_CLICK) {
							if (_down_button) {
								if (in_button(_down_button, me)) {
									/* Handle button presses */
									if (_down_button == &_button_cancel) return 1;
									else if (_down_button == &_button_authenticate) return 0;
									_down_button->hilight = 0;
								}
							}
							_down_button = NULL;
						}
						if (!me->buttons & YUTANI_MOUSE_BUTTON_LEFT) {
							if (in_button(&_button_cancel, me)) {
								r |= set_hilight(&_button_cancel, 1);
							} else if (in_button(&_button_authenticate, me)) {
								r |= set_hilight(&_button_authenticate, 1);
							} else {
								r |= set_hilight(NULL, 0);
							}
						} else if (_down_button) {
							if (in_button(_down_button, me)) {
								r |= set_hilight(_down_button, 2);
							} else {
								r |= set_hilight(NULL, 0);
							}
						}
					}
					if (r) redraw(username, password, fails, argv);
				}
				break;
			case YUTANI_MSG_WINDOW_CLOSE:
			case YUTANI_MSG_SESSION_END:
				return 1;
		}
	}
}

int main(int argc, char ** argv) {

	if (argc < 2) {
		return 1;
	}

	yctx = yutani_init();

	if (!yctx) {
		fprintf(stderr, "%s: could not connect to compositor\n", argv[0]);
		return 1;
	}

	int width = yctx->display_width;
	int height = yctx->display_height;

	window = yutani_window_create(yctx, width, height);
	yutani_window_move(yctx, window, 0, 0);
	yutani_window_advertise(yctx, window, "gsudo");

	ctx = init_graphics_yutani_double_buffer(window);

	return sudo_loop(graphical_callback, argv);
}
