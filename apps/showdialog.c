/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 *
 * showdialog - show a window with a dialog prompt with buttons
 */
#include <toaru/yutani.h>
#include <toaru/graphics.h>
#include <toaru/decorations.h>
#include <toaru/sdf.h>
#include <toaru/menu.h>
#include <toaru/button.h>

#include <sys/utsname.h>

#define BUTTON_HEIGHT 28
#define BUTTON_WIDTH 86
#define BUTTON_PADDING 14

static yutani_t * yctx;
static yutani_window_t * window = NULL;
static gfx_context_t * ctx = NULL;
static sprite_t logo;

static int32_t width = 600;
static int32_t height = 150;

static char * icon_path;
static char * title_str;
static char * copyright_str[20] = {NULL};

static void draw_string(int y, const char * string, int font, uint32_t color) {

	struct decor_bounds bounds;
	decor_get_bounds(window, &bounds);

	draw_sdf_string(ctx, bounds.left_width + 80, bounds.top_height + 30 + y, string, 16, color, font);
}

struct TTKButton _ok = {0};
struct TTKButton _cancel = {0};

static void redraw(void) {

	struct decor_bounds bounds;
	decor_get_bounds(window, &bounds);

	draw_fill(ctx, rgb(204,204,204));
	draw_sprite(ctx, &logo, bounds.left_width + 20, bounds.top_height + 20);
	int offset = 0;

	for (char ** copy_str = copyright_str; *copy_str; ++copy_str) {
		if (**copy_str == '-') {
			offset += 10;
		} else if (**copy_str == '%') {
			draw_string(offset, *copy_str+1, SDF_FONT_THIN, rgb(0,0,255));
			offset += 20;
		} else {
			draw_string(offset, *copy_str, SDF_FONT_THIN, rgb(0,0,0));
			offset += 20;
		}
	}

	ttk_button_draw(ctx, &_ok);
	ttk_button_draw(ctx, &_cancel);

	window->decorator_flags |= DECOR_FLAG_NO_MAXIMIZE;
	render_decorations(window, ctx, title_str);

	flip(ctx);
	yutani_flip(yctx, window);
}

static void init_default(void) {
	title_str = "Dialog Prompt";
	icon_path = "/usr/share/icons/48/folder.png";

	copyright_str[0] = "This is a demonstration of a dialog box.";
	copyright_str[1] = "You can press \"Okay\" or \"Cancel\" or close the window.";
}

int in_button(struct TTKButton * button, struct yutani_msg_window_mouse_event * me) {
	if (me->new_y >= button->y && me->new_y < button->y  + button->height) {
		if (me->new_x >= button->x && me->new_x < button->x + button->width) {
			return 1;
		}
	}
	return 0;
}

void setup_buttons(void) {
	struct decor_bounds bounds;
	decor_get_bounds(window, &bounds);

	_ok.title = "Okay";
	_ok.width = BUTTON_WIDTH;
	_ok.height = BUTTON_HEIGHT;
	_ok.x = ctx->width - bounds.right_width - BUTTON_WIDTH - BUTTON_PADDING;
	_ok.y = ctx->height - bounds.bottom_height - BUTTON_HEIGHT - BUTTON_PADDING;

	_cancel.title = "Cancel";
	_cancel.width = BUTTON_WIDTH;
	_cancel.height = BUTTON_HEIGHT;
	_cancel.x = ctx->width - bounds.right_width - BUTTON_WIDTH * 2 - BUTTON_PADDING * 2;
	_cancel.y = ctx->height - bounds.bottom_height - BUTTON_HEIGHT - BUTTON_PADDING;
}

void resize_finish(int w, int h) {
	yutani_window_resize_accept(yctx, window, w, h);
	reinit_graphics_yutani(ctx, window);
	width  = w;
	height = h;
	setup_buttons();
	redraw();
	yutani_window_resize_done(yctx, window);
}

void set_hilight(struct TTKButton * button, int hilight) {
	if (!button && (_ok.hilight || _cancel.hilight)) {
		_ok.hilight = 0;
		_cancel.hilight = 0;
		redraw();
	} else if (button && (button->hilight != hilight)) {
		_ok.hilight = 0;
		_cancel.hilight = 0;
		button->hilight = hilight;
		redraw();
	}
}


int main(int argc, char * argv[]) {
	int req_center_x, req_center_y;
	yctx = yutani_init();
	if (!yctx) {
		fprintf(stderr, "%s: failed to connect to compositor\n", argv[0]);
		return 1;
	}
	init_decorations();

	struct decor_bounds bounds;
	decor_get_bounds(NULL, &bounds);

	window = yutani_window_create_flags(yctx, width + bounds.width, height + bounds.height, YUTANI_WINDOW_FLAG_DIALOG_ANIMATION);
	req_center_x = yctx->display_width / 2;
	req_center_y = yctx->display_height / 2;

	if (argc < 2) {
		init_default();
	} else if (argc < 4) {
		fprintf(stderr, "Invalid arguments.\n");
		return 1;
	} else {
		title_str = argv[1];
		icon_path = argv[2];

		int i = 0;
		char * me = argv[3], * end;
		do {
			copyright_str[i] = me;
			i++;
			end = strchr(me,'\n');
			if (end) {
				*end = '\0';
				me = end+1;
			}
		} while (end);

		if (argc > 6) {
			req_center_x = atoi(argv[5]);
			req_center_y = atoi(argv[6]);
		}
	}

	yutani_window_move(yctx, window, req_center_x - window->width / 2, req_center_y - window->height / 2);

	yutani_window_advertise_icon(yctx, window, title_str, "star");

	ctx = init_graphics_yutani_double_buffer(window);
	setup_buttons();
	load_sprite(&logo, icon_path);
	redraw();

	struct TTKButton * _down_button = NULL;

	int playing = 1;
	int status = 0;
	while (playing) {
		yutani_msg_t * m = yutani_poll(yctx);
		while (m) {
			if (menu_process_event(yctx, m)) {
				redraw();
			}
			switch (m->type) {
				case YUTANI_MSG_KEY_EVENT:
					{
						struct yutani_msg_key_event * ke = (void*)m->data;
						if (ke->event.action == KEY_ACTION_DOWN && ke->event.keycode == '\n') {
							playing = 0;
							status = 0;
						} else if (ke->event.action == KEY_ACTION_DOWN && ke->event.keycode == KEY_ESCAPE) {
							playing = 0;
							status = 2;
						}
					}
					break;
				case YUTANI_MSG_WINDOW_FOCUS_CHANGE:
					{
						struct yutani_msg_window_focus_change * wf = (void*)m->data;
						yutani_window_t * win = hashmap_get(yctx->windows, (void*)wf->wid);
						if (win) {
							win->focused = wf->focused;
							redraw();
						}
					}
					break;
				case YUTANI_MSG_RESIZE_OFFER:
					{
						struct yutani_msg_window_resize * wr = (void*)m->data;
						resize_finish(wr->width, wr->height);
					}
					break;
				case YUTANI_MSG_WINDOW_MOUSE_EVENT:
					{
						struct yutani_msg_window_mouse_event * me = (void*)m->data;
						if (me->wid == window->wid) {
							int result = decor_handle_event(yctx, m);
							switch (result) {
								case DECOR_CLOSE:
									playing = 0;
									status = 2;
									break;
								case DECOR_RIGHT:
									/* right click in decoration, show appropriate menu */
									decor_show_default_menu(window, window->x + me->new_x, window->y + me->new_y);
									break;
								default:
									/* Other actions */
									break;
							}

							struct decor_bounds bounds;
							decor_get_bounds(window, &bounds);
							if (me->new_y > bounds.top_height) {

								if (me->command == YUTANI_MOUSE_EVENT_DOWN) {
									if (in_button(&_ok, me)) {
										set_hilight(&_ok, 2);
										_down_button = &_ok;
									} else if (in_button(&_cancel, me)) {
										set_hilight(&_cancel, 2);
										_down_button = &_cancel;
									}
								} else if (me->command == YUTANI_MOUSE_EVENT_RAISE || me->command == YUTANI_MOUSE_EVENT_CLICK) {
									if (_down_button) {
										if (in_button(_down_button, me)) {
											if (_down_button == &_cancel) {
												playing = 0;
												status  = 1;
												break;
											} else if (_down_button == &_ok) {
												playing = 0;
												status = 0;
												break;
											}
											_down_button->hilight = 0;
										}
									}
									_down_button = NULL;
								}

								if (!me->buttons & YUTANI_MOUSE_BUTTON_LEFT) {
									if (in_button(&_ok, me)) {
										set_hilight(&_ok, 1);
									} else if (in_button(&_cancel, me)) {
										set_hilight(&_cancel, 1);
									} else {
										set_hilight(NULL,0);
									}
								} else if (_down_button) {
									if (in_button(_down_button, me)) {
										set_hilight(_down_button, 2);
									} else {
										set_hilight(NULL, 0);
									}
								}
							}
						}
					}
					break;
				case YUTANI_MSG_WINDOW_CLOSE:
				case YUTANI_MSG_SESSION_END:
					playing = 0;
					status = 2;
					break;
				default:
					break;
			}
			free(m);
			m = yutani_poll_async(yctx);
		}
	}

	yutani_close(yctx, window);

	return status;
}
