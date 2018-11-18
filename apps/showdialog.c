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

struct button {
	int x;
	int y;
	int width;
	int height;
	char * title;
	int hilight;
};

struct gradient_definition {
	int height;
	int y;
	uint32_t top;
	uint32_t bottom;
};

static uint32_t gradient_pattern(int32_t x, int32_t y, double alpha, void * extra) {
	struct gradient_definition * gradient = extra;
	int base_r = _RED(gradient->top), base_g = _GRE(gradient->top), base_b = _BLU(gradient->top);
	int last_r = _RED(gradient->bottom), last_g = _GRE(gradient->bottom), last_b = _GRE(gradient->bottom);
	double gradpoint = (double)(y - (gradient->y)) / (double)gradient->height;

	return premultiply(rgba(
		base_r * (1.0 - gradpoint) + last_r * (gradpoint),
		base_g * (1.0 - gradpoint) + last_g * (gradpoint),
		base_b * (1.0 - gradpoint) + last_b * (gradpoint),
		alpha * 255));
}

static void draw_button(struct button * button) {
	if (button->width == 0) {
		fprintf(stderr, "not drawing button, width is 0\n");
		return;
	}
	/* Dark edge */
	struct gradient_definition edge = {button->height, button->y, rgb(166,166,166), rgb(136,136,136)};
	draw_rounded_rectangle_pattern(ctx, button->x, button->y, button->width, button->height, 4, gradient_pattern, &edge);
	/* Sheen */
	draw_rounded_rectangle(ctx, button->x + 1, button->y + 1, button->width - 2, button->height - 2, 3, rgb(238,238,238));
	/* Button face - this should normally be a gradient */
	if (button->hilight) {
		struct gradient_definition face = {button->height-3, button->y + 2, rgb(240,240,240), rgb(230,230,230)};
		draw_rounded_rectangle_pattern(ctx, button->x + 2, button->y + 2, button->width - 4, button->height - 3, 2, gradient_pattern, &face);
	} else {
		struct gradient_definition face = {button->height-3, button->y + 2, rgb(219,219,219), rgb(204,204,204)};
		draw_rounded_rectangle_pattern(ctx, button->x + 2, button->y + 2, button->width - 4, button->height - 3, 2, gradient_pattern, &face);
	}

	int label_width = draw_sdf_string_width(button->title, 16, SDF_FONT_THIN);
	int centered = (button->width - label_width) / 2;

	int centered_y = (button->height - 16) / 2;
	draw_sdf_string(ctx, button->x + centered, button->y + centered_y, button->title, 16, rgb(0,0,0), SDF_FONT_THIN);

}

struct button _ok = {0};
struct button _cancel = {0};

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

	draw_button(&_ok);
	draw_button(&_cancel);

	window->decorator_flags |= DECOR_FLAG_NO_MAXIMIZE;
	render_decorations(window, ctx, title_str);

	flip(ctx);
	yutani_flip(yctx, window);
}

static void init_default(void) {
	title_str = "Dialog Prompt";
	icon_path = "/usr/share/icons/48/folder.bmp";

	copyright_str[0] = "This is a demonstration of a dialog box.";
	copyright_str[1] = "You can press \"Okay\" or \"Cancel\" or close the window.";
}

int in_button(struct button * button, struct yutani_msg_window_mouse_event * me) {
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

	window = yutani_window_create(yctx, width + bounds.width, height + bounds.height);
	req_center_x = yctx->display_width / 2;
	req_center_y = yctx->display_height / 2;

	if (argc < 2) {
		init_default();
	} else if (argc < 5) {
		fprintf(stderr, "Invalid arguments.\n");
		return 1;
	} else {
		title_str = argv[1];
		icon_path = argv[2];

		int i = 0;
		char * me = argv[4], * end;
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
	logo.alpha = ALPHA_EMBEDDED;
	redraw();

	int playing = 1;
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
						if (ke->event.action == KEY_ACTION_DOWN && ke->event.keycode == 'q') {
							playing = 0;
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
								if (in_button(&_ok, me)) {
									if (!_ok.hilight) {
										_ok.hilight = 1;
										_cancel.hilight = 0;
										redraw();
									}
								} else if (in_button(&_cancel, me)) {
									if (!_cancel.hilight) {
										_cancel.hilight = 1;
										_ok.hilight = 0;
										redraw();
									}
								} else {
									if (_ok.hilight || _cancel.hilight) {
										_cancel.hilight = 0;
										_ok.hilight = 0;
										redraw();
									}
								}
							}
						}
					}
					break;
				case YUTANI_MSG_WINDOW_CLOSE:
				case YUTANI_MSG_SESSION_END:
					playing = 0;
					break;
				default:
					break;
			}
			free(m);
			m = yutani_poll_async(yctx);
		}
	}

	yutani_close(yctx, window);

	return 0;
}
