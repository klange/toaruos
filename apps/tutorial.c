/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2019 K. Lange
 *
 * tutorial - A recreation of the original wizard.py, explaining
 *            the functionality of ToaruOS and how to use the WM.
 */
#include <time.h>

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

static yutani_window_t * background = NULL;
static gfx_context_t * background_ctx = NULL;

static int32_t width = 640;
static int32_t height = 480;

static char * title_str;
static char * body_text[20] = {NULL};

static sprite_t * icon = NULL;
static sprite_t terminal;
static sprite_t folder;
static sprite_t package;
static sprite_t logo;
static sprite_t mouse_drag;

static int page = 0;

static int center(int x, int width) {
	return (width - x) / 2;
}

static void draw_string(int y, const char * string, int font, uint32_t color, int size) {

	struct decor_bounds bounds;
	decor_get_bounds(window, &bounds);

	int text_width = draw_sdf_string_width(string, size, font);

	draw_sdf_string(ctx, center(text_width, width), bounds.top_height + 30 + y, string, size, color, font);
}

struct TTKButton _next_button = {0};
struct TTKButton _prev_button = {0};

static int _prev_enabled = 0;

static void redraw(void) {

	struct decor_bounds bounds;
	decor_get_bounds(window, &bounds);

	draw_fill(ctx, rgb(204,204,204));
	int offset = 0;

	if (icon) {
		offset = icon->height + 20;
		draw_sprite(ctx, icon, center(icon->width, width), bounds.top_height + 15);
	}

	for (char ** copy_str = body_text; *copy_str; ++copy_str) {
		if (**copy_str == '-') {
			offset += 10;
		} else if (**copy_str == '%') {
			draw_string(offset, *copy_str+1, SDF_FONT_THIN, rgb(0,0,255), 16);
			offset += 20;
		} else if (**copy_str == '#') {
			draw_string(offset, *copy_str+1, SDF_FONT_BOLD, rgb(0,0,0), 23);
			offset += 20;
		} else {
			draw_string(offset, *copy_str, SDF_FONT_THIN, rgb(0,0,0), 16);
			offset += 20;
		}
	}

	ttk_button_draw(ctx, &_next_button);
	if (!_prev_enabled) {
		int _tmp = _prev_button.hilight;
		_prev_button.hilight = (1 << 8);
		ttk_button_draw(ctx, &_prev_button);
		_prev_button.hilight = _tmp;
	} else {
		ttk_button_draw(ctx, &_prev_button);
	}

	render_decorations(window, ctx, title_str);

	flip(ctx);
	yutani_flip(yctx, window);
}

static void reset_background(void) {
	draw_fill(background_ctx, rgba(0,0,0,200));
}

static void invert_background_alpha(void) {
	for (unsigned int y = 0; y < background->height; ++y) {
		for (unsigned int x = 0; x < background->width; ++x) {
			uint32_t c = GFX(background_ctx, x, y);
			int r = _RED(c);
			int g = _GRE(c);
			int b = _BLU(c);
			int a = _ALP(c);
			a = 255 - a;
			GFX(background_ctx, x, y) = rgba(r,g,b,a);
		}
	}
}

static void circle(int x, int y, int r) {
	draw_fill(background_ctx, rgba(0,0,0,255-200));
	draw_rounded_rectangle(background_ctx, x - r, y - r, r * 2, r * 2, r, rgb(0,0,0));
	invert_background_alpha();
}

static char * randomly_select_begging(void) {
	char * options[] = {
		"You can help support ToaruOS by donating:",
		"Your donation helps us continue developing ToaruOS:",
		"You can sponsor ToaruOS development on Github:",
		"Please give me money:",
	};

	return options[rand() % (sizeof(options) / sizeof(*options))];

}

static void load_page(int page) {

	int i = 0;
	_prev_enabled = 1;
	_next_button.title = "Next";
	reset_background();

	switch (page) {
		case 0:
			_prev_enabled = 0;
			title_str = "Welcome to ToaruOS!";
			icon = &logo;
			body_text[i++] = "#Welcome to ToaruOS!";
			body_text[i++] = "";
			body_text[i++] = "This tutorial will guide you through the features of the operating";
			body_text[i++] = "system, as well as give you a feel for the UI and design principles.";
			body_text[i++] = "";
			body_text[i++] = "When you're ready to continue, press \"Next\".";
			body_text[i++] = "";
			body_text[i++] = "%https://github.com/klange/toaruos - https://toaruos.org";
			body_text[i++] = "";
			body_text[i++] = "ToaruOS is free software, released under the terms of the";
			body_text[i++] = "NCSA/University of Illinois license.";
			body_text[i++] = "";
			body_text[i++] = randomly_select_begging();
			body_text[i++] = "%https://github.com/sponsors/klange";
			body_text[i++] = NULL;
			break;
		case 1:
			icon = &logo;
			body_text[i++] = "ToaruOS is a hobby project. The entire contents of this Live CD";
			body_text[i++] = "were written by the ToaruOS development team over the course of";
			body_text[i++] = "many years, but that development team is very small. Some features";
			body_text[i++] = "may be missing, incomplete, or unstable. Contributions in the form";
			body_text[i++] = "of bug-fixes and new software are welcome. You can join our community";
			body_text[i++] = "through IRC by joining the #toaruos channel on Freenode.";
			body_text[i++] = "";
			body_text[i++] = "You can help support ToaruOS by donating:";
			body_text[i++] = "%https://github.com/sponsors/klange";
			body_text[i++] = NULL;
			break;
		case 2:
			icon = &folder;
			circle(70, 90, 60);
			body_text[i++] = "You can explore the file system using the File Browser.";
			body_text[i++] = "Application shortcuts on the desktop, as well as files in the file browser";
			body_text[i++] = "are opened with a double click. You can also find more applications in";
			body_text[i++] = "the Applications menu in the upper left.";
			body_text[i++] = NULL;
			break;
		case 3:
			icon = &terminal;
			circle(70, 170, 60);
			body_text[i++] = "ToaruOS aims to provide a Unix-like environment. You can find";
			body_text[i++] = "familiar command-line tools by opening a terminal. ToaruOS's";
			body_text[i++] = "shell provides command history, syntax highlighting, and tab";
			body_text[i++] = "completion. There is also a growing suite of Unix utilities";
			body_text[i++] = "and a featureful text editor (bim).";
			body_text[i++] = NULL;
			break;
		case 4:
			icon = &package;
			circle(70, 250, 60);
			body_text[i++] = "Many third-party software packages have been ported to ToaruOS";
			body_text[i++] = "and are available from our package repositories. You can use the";
			body_text[i++] = "Package Manager to install GCC, Python, Bochs, Quake, and more.";
			body_text[i++] = "";
			body_text[i++] = "The Package Manager will prompt you to authenticate. The default";
			body_text[i++] = "user is 'local' with the password 'local'. There is also a 'root'";
			body_text[i++] = "user with the password 'toor'.";
			body_text[i++] = NULL;
			break;
		case 5:
			icon = &mouse_drag;
			body_text[i++] = "With ToaruOS's window manager, you can drag most windows by";
			body_text[i++] = "holding Alt, or by using the title bar. You can also resize";
			body_text[i++] = "windows by dragging from their edges or using Alt + Middle Click.";
			body_text[i++] = "";
			body_text[i++] = "If you are running ToaruOS in VirtualBox, be sure to select a Host";
			body_text[i++] = "key configuration that does not conflict with these key bindings.";
			body_text[i++] = NULL;
			break;
		case 6:
			icon = NULL;
			_next_button.title = "Exit";
			body_text[i++] = "#That's it!";
			body_text[i++] = "";
			body_text[i++] = "The tutorial is over.";
			body_text[i++] = "";
			body_text[i++] = "Press \"Exit\" to close this window and start exploring ToaruOS.";
			body_text[i++] = NULL;
			break;
		default:
			exit(0);
			break;
	}

	flip(background_ctx);
	yutani_flip(yctx, background);
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

	_next_button.title = "Next";
	_next_button.width = BUTTON_WIDTH;
	_next_button.height = BUTTON_HEIGHT;
	_next_button.x = ctx->width - bounds.right_width - BUTTON_WIDTH - BUTTON_PADDING;
	_next_button.y = ctx->height - bounds.bottom_height - BUTTON_HEIGHT - BUTTON_PADDING;

	_prev_button.title = "Back";
	_prev_button.width = BUTTON_WIDTH;
	_prev_button.height = BUTTON_HEIGHT;
	_prev_button.x = ctx->width - bounds.right_width - BUTTON_WIDTH * 2 - BUTTON_PADDING * 2;
	_prev_button.y = ctx->height - bounds.bottom_height - BUTTON_HEIGHT - BUTTON_PADDING;
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

void resize_finish_bg(int w, int h) {
	yutani_window_resize_accept(yctx, background, w, h);
	reinit_graphics_yutani(background_ctx, background);
	load_page(page);
	yutani_window_resize_done(yctx, background);
}

void set_hilight(struct TTKButton * button, int hilight) {
	if (!button && (_next_button.hilight || _prev_button.hilight)) {
		_next_button.hilight = 0;
		_prev_button.hilight = 0;
		redraw();
	} else if (button && (button->hilight != hilight)) {
		_next_button.hilight = 0;
		_prev_button.hilight = 0;
		button->hilight = hilight;
		redraw();
	}
}


int main(int argc, char * argv[]) {
	srand(time(NULL));
	int req_center_x, req_center_y;
	yctx = yutani_init();
	if (!yctx) {
		fprintf(stderr, "%s: failed to connect to compositor\n", argv[0]);
		return 1;
	}
	init_decorations();

	background = yutani_window_create_flags(yctx, yctx->display_width, yctx->display_height,
			YUTANI_WINDOW_FLAG_DISALLOW_RESIZE | YUTANI_WINDOW_FLAG_DISALLOW_DRAG |
			YUTANI_WINDOW_FLAG_ALT_ANIMATION | YUTANI_WINDOW_FLAG_NO_STEAL_FOCUS);
	yutani_window_move(yctx, background, 0, 0);
	yutani_window_update_shape(yctx, background, 2);
	background_ctx = init_graphics_yutani_double_buffer(background);
	reset_background();
	flip(background_ctx);
	yutani_flip(yctx, background);

	struct decor_bounds bounds;
	decor_get_bounds(NULL, &bounds);

	window = yutani_window_create(yctx, width + bounds.width, height + bounds.height);

	/* Load icons */
	load_sprite(&logo, "/usr/share/logo_login.png");
	load_sprite(&terminal, "/usr/share/icons/48/utilities-terminal.png");
	load_sprite(&folder, "/usr/share/icons/48/folder.png");
	load_sprite(&package, "/usr/share/icons/48/package.png");
	load_sprite(&mouse_drag, "/usr/share/cursor/drag.png");

	load_page(0);

	req_center_x = yctx->display_width / 2;
	req_center_y = yctx->display_height / 2;
	yutani_window_move(yctx, window, req_center_x - window->width / 2, req_center_y - window->height / 2);
	yutani_window_advertise_icon(yctx, window, title_str, "star");

	ctx = init_graphics_yutani_double_buffer(window);
	setup_buttons();
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
							page++;
							load_page(page);
							redraw();
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
						if (wf->wid == background->wid) {
							yutani_focus_window(yctx, window->wid);
						} else if (win) {
							win->focused = wf->focused;
							redraw();
						}
					}
					break;
				case YUTANI_MSG_WELCOME:
					yutani_window_resize_offer(yctx, background, yctx->display_width, yctx->display_height);
					req_center_x = yctx->display_width / 2;
					req_center_y = yctx->display_height / 2;
					yutani_window_move(yctx, window, req_center_x - window->width / 2, req_center_y - window->height / 2);
					break;
				case YUTANI_MSG_RESIZE_OFFER:
					{
						struct yutani_msg_window_resize * wr = (void*)m->data;
						if (wr->wid == window->wid) {
							resize_finish(wr->width, wr->height);
						} else if (wr->wid == background->wid) {
							resize_finish_bg(wr->width, wr->height);
						}
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
									if (in_button(&_next_button, me)) {
										set_hilight(&_next_button, 2);
										_down_button = &_next_button;
									} else if (in_button(&_prev_button, me)) {
										set_hilight(&_prev_button, 2);
										_down_button = &_prev_button;
									}
								} else if (me->command == YUTANI_MOUSE_EVENT_RAISE || me->command == YUTANI_MOUSE_EVENT_CLICK) {
									if (_down_button) {
										if (in_button(_down_button, me)) {
											if (_down_button == &_prev_button) {
												if (page > 0) {
													page--;
													load_page(page);
												}
											} else if (_down_button == &_next_button) {
												page++;
												load_page(page);
											}
											_down_button->hilight = 0;
										}
									}
									_down_button = NULL;
								}

								if (!me->buttons & YUTANI_MOUSE_BUTTON_LEFT) {
									if (in_button(&_next_button, me)) {
										set_hilight(&_next_button, 1);
									} else if (in_button(&_prev_button, me)) {
										set_hilight(&_prev_button, 1);
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
