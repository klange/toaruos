/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2015 Kevin Lange
 *
 * Live CD Welcome Program
 *
 */
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <syscall.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>

#include "lib/yutani.h"
#include "lib/graphics.h"
#include "lib/decorations.h"
#include "gui/ttk/ttk.h"

#include "lib/trace.h"
#define TRACE_APP_NAME "live-wizard"

static yutani_t * yctx;

static yutani_window_t * win_hints;
static gfx_context_t * ctx_hints;

static yutani_window_t * win_wizard;
static gfx_context_t * ctx_wizard;

static cairo_surface_t * surface_hints;
static cairo_surface_t * surface_wizard;

static cairo_t * cr_hints;
static cairo_t * cr_wizard;

static int should_exit = 0;
static int current_frame = 0;

static int center_x(int x) {
	return (yctx->display_width - x) / 2;
}

static int center_y(int y) {
	return (yctx->display_height - y) / 2;
}

static int center_win_x(int x) {
	return (win_wizard->width - x) / 2;
}

static int button_width = 100;
static int button_height = 32;
static int button_focused = 0;
static void draw_next_button(int is_exit) {
	if (button_focused == 1) {
		/* hover */
		_ttk_draw_button_hover(cr_wizard, center_win_x(button_width), 400, button_width, button_height, is_exit ? "Exit" : "Next");
	} else if (button_focused == 2) {
		/* Down */
		_ttk_draw_button_select(cr_wizard, center_win_x(button_width), 400, button_width, button_height, is_exit ? "Exit" : "Next");
	} else {
		/* Something else? */
		_ttk_draw_button(cr_wizard, center_win_x(button_width), 400, button_width, button_height, is_exit ? "Exit" : "Next");
	}
}

static void draw_centered_label(int y, int size, char * label) {
	set_font_face(FONT_SANS_SERIF);
	set_font_size(size);

	int x = center_win_x(draw_string_width(label));
	draw_string(ctx_wizard, x, y, rgb(0,0,0), label);
}

static char * LOGO = "/usr/share/logo_login.png";
static void draw_logo(void) {
	sprite_t logo;
	load_sprite_png(&logo, LOGO);
	draw_sprite(ctx_wizard, &logo, center_win_x(logo.width), 50);
}

static void redraw(void) {
	draw_fill(ctx_hints, premultiply(rgba(0,0,0,100)));
	draw_fill(ctx_wizard, rgb(TTK_BACKGROUND_DEFAULT));

	/* Draw the current tutorial frame */
	render_decorations(win_wizard, ctx_wizard, "Welcome to とあるOS");
	switch (current_frame) {
		case 0:
			/* Labels for Welcome to とあるOS! */
			draw_logo();
			draw_centered_label(100+70, 20, "Welcome to とあるOS!");
			draw_centered_label(100+88, 12, "This tutorial will guide you through the features");
			draw_centered_label(100+102,12, "of the operating system, as well as give you a feel");
			draw_centered_label(100+116,12, "for the UI and design principles.");
			draw_centered_label(100+180,12, "When you're ready to continue, press \"Next\".");
			draw_centered_label(120+200,12, "https://github.com/klange/toaruos - http://toaruos.org");
			draw_centered_label(120+220,12, "とあるOS is free software, released under the terms");
			draw_centered_label(120+234,12, "of the NCSA/University of Illinois license.");
			draw_next_button(0);
			break;
		case 1:
			draw_logo();
			draw_centered_label(100+70,12,"If you wish to exit the tutorial at any time, you can");
			draw_centered_label(100+84,12,"click the × in the upper right corner of this window.");
			draw_next_button(0);
			break;
		case 2:
			draw_logo();
			draw_centered_label(100+70, 12,"As a reminder, とあるOS is a hobby project with few developers.");
			draw_centered_label(100+84, 12,"As such, do not expect things to work perfectly, or in some cases,");
			draw_centered_label(100+98, 12,"at all, as the kernel and drivers are very much \"work-in-progress\".");
			draw_next_button(0);
			break;
		case 3:
			draw_logo();
			draw_centered_label(100+70, 12,"This tutorial itself is still a work-in-progress,");
			draw_centered_label(100+84, 12,"so there's nothing else to see.");
			draw_next_button(0);
			break;
		case 4:
			draw_logo();
			draw_centered_label(100+70,12,"Congratulations!");
			draw_centered_label(100+88,12,"You've finished the tutorial!");
			draw_next_button(1);
			break;
		default:
			exit(0);
			break;
	}

	flip(ctx_hints);
	flip(ctx_wizard);
	yutani_flip(yctx, win_hints);
	yutani_flip(yctx, win_wizard);
}

static void do_click_callback(void) {
	current_frame += 1;
	redraw();
}

static int previous_buttons = 0;
static void do_mouse_stuff(struct yutani_msg_window_mouse_event * me) {
	if (button_focused == 2) {
		/* See if we released and are still inside. */
		if (me->command == YUTANI_MOUSE_EVENT_RAISE || me->command == YUTANI_MOUSE_EVENT_CLICK) {
			if (!(me->buttons & YUTANI_MOUSE_BUTTON_LEFT)) {
				if (me->new_x > center_win_x(button_width)
				    && me->new_x < center_win_x(button_width) + button_width
				    && me->new_y > 400
				    && me->new_y < 400 + button_height) {
					button_focused = 1;
					do_click_callback();
				} else {
					button_focused = 0;
					redraw();
				}
			}
		}
	} else {
		if (me->new_x > center_win_x(button_width)
			&& me->new_x < center_win_x(button_width) + button_width
			&& me->new_y > 400
			&& me->new_y < 400 + button_height) {
			if (!button_focused) {
				button_focused = 1;
				redraw();
			}
			if (me->command == YUTANI_MOUSE_EVENT_DOWN && (me->buttons & YUTANI_MOUSE_BUTTON_LEFT)) {
				button_focused = 2;
				redraw();
			}
		} else {
			if (button_focused) {
				button_focused = 0;
				redraw();
			}
		}
	}
	previous_buttons = me->buttons;
}

int main(int argc, char * argv[]) {

	TRACE("Opening some windows...");
	yctx = yutani_init();

	init_decorations();

	win_hints = yutani_window_create(yctx, yctx->display_width, yctx->display_height);
	yutani_window_move(yctx, win_hints, 0, 0);
	yutani_window_update_shape(yctx, win_hints, YUTANI_SHAPE_THRESHOLD_CLEAR);
	ctx_hints = init_graphics_yutani_double_buffer(win_hints);

	win_wizard = yutani_window_create(yctx, 640, 480);
	yutani_window_move(yctx, win_wizard, center_x(640), center_y(480));
	ctx_wizard = init_graphics_yutani_double_buffer(win_wizard);

	int stride;

	stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, win_hints->width);
	surface_hints = cairo_image_surface_create_for_data(ctx_hints->backbuffer, CAIRO_FORMAT_ARGB32, win_hints->width, win_hints->height, stride);
	cr_hints = cairo_create(surface_hints);

	stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, win_wizard->width);
	surface_wizard = cairo_image_surface_create_for_data(ctx_wizard->backbuffer, CAIRO_FORMAT_ARGB32, win_wizard->width, win_wizard->height, stride);
	cr_wizard = cairo_create(surface_wizard);

	yutani_window_advertise_icon(yctx, win_wizard, "Welcome Tutorial", "live-welcome");

	redraw();

	yutani_focus_window(yctx, win_wizard->wid);

	while (!should_exit) {
		yutani_msg_t * m = yutani_poll(yctx);

		if (m) {
			switch (m->type) {
				case YUTANI_MSG_KEY_EVENT:
					{
						struct yutani_msg_key_event * ke = (void*)m->data;
						if (ke->event.key == 'q' && ke->event.action == KEY_ACTION_DOWN) {
							should_exit = 1;
						}
					}
					break;
				case YUTANI_MSG_WINDOW_FOCUS_CHANGE:
					{
						struct yutani_msg_window_focus_change * wf = (void*)m->data;
						if (wf->wid == win_hints->wid) {
							yutani_focus_window(yctx, win_wizard->wid);
						} else if (wf->wid == win_wizard->wid) {
							win_wizard->focused = wf->focused;
							redraw();
						}
					}
					break;
				case YUTANI_MSG_WINDOW_MOVE:
					{
						struct yutani_msg_window_move * wm = (void*)m->data;
						if (wm->wid == win_hints->wid) {
							if (wm->x != 0 || wm->y != 0) {
								/* force us back to 0,0 */
								yutani_window_move(yctx, win_hints, 0, 0);
							}
						}
					}
					break;
				case YUTANI_MSG_WINDOW_MOUSE_EVENT:
					{
						struct yutani_msg_window_mouse_event * me = (void*)m->data;
						if (me->wid != win_wizard->wid) break;
						int result = decor_handle_event(yctx, m);
						switch (result) {
							case DECOR_CLOSE:
								should_exit = 1;
								break;
							default:
								/* Other actions */
								do_mouse_stuff(me);
								break;
						}
					}
					break;
				case YUTANI_MSG_SESSION_END:
					should_exit = 1;
					break;
				default:
					break;
			}
			free(m);
		}
	}

	return 0;
}
