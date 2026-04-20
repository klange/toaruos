/**
 * @brief showdialog - show a window with a dialog prompt with buttons
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 */
#include <getopt.h>
#include <toaru/yutani.h>
#include <toaru/graphics.h>
#include <toaru/decorations.h>
#include <toaru/menu.h>
#include <toaru/button.h>
#include <toaru/text.h>
#include <toaru/icon_cache.h>

#include <sys/utsname.h>

#define BUTTON_HEIGHT 28
#define BUTTON_WIDTH 86
#define BUTTON_PADDING 14

static yutani_t * yctx;
static yutani_window_t * window = NULL;
static gfx_context_t * ctx = NULL;
static sprite_t * icon = NULL;

static int32_t width = 600;
static int32_t height = 150;

static char * title_str = "Dialog Prompt";
static char * icon_path = "generic";
static char * ad_icon = "generic";
static char * okay_str = "Okay";
static char * cancel_str = "Cancel";
static char ** body_text = NULL;
static int disable_cancel = 0;

static struct TT_Font * _tt_font = NULL;

static void draw_string(int y, const char * string, uint32_t color) {

	struct decor_bounds bounds;
	decor_get_bounds(window, &bounds);

	tt_set_size(_tt_font, 13);
	tt_draw_string(ctx, _tt_font, bounds.left_width + 80, bounds.top_height + 30 + y + 13, string, color);
}

struct TTKButton _ok = {0};
struct TTKButton _cancel = {0};

static void redraw(void) {

	_cancel.hilight |= (disable_cancel ? 0x100 : 0);

	struct decor_bounds bounds;
	decor_get_bounds(window, &bounds);

	draw_fill(ctx, rgb(204,204,204));
	draw_sprite(ctx, icon, bounds.left_width + 20, bounds.top_height + 20);
	int offset = 0;

	for (char ** str = body_text; *str; ++str) {
		if (**str == '-') {
			offset += 10;
		} else if (**str == '%') {
			draw_string(offset, *str+1, rgb(0,0,255));
			offset += 20;
		} else {
			draw_string(offset, *str, rgb(0,0,0));
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

	_ok.title = okay_str;
	_ok.width = BUTTON_WIDTH;
	_ok.height = BUTTON_HEIGHT;
	_ok.x = ctx->width - bounds.right_width - BUTTON_WIDTH - BUTTON_PADDING;
	_ok.y = ctx->height - bounds.bottom_height - BUTTON_HEIGHT - BUTTON_PADDING;

	_cancel.title = cancel_str;
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

static sprite_t * image_or_icon(const char * path) {
	int is_path = 0;
	for (const char *p = path; *p; ++p) {
		if (*p == '/' || *p == '.') {
			is_path = 1;
			break;
		}
	}

	if (is_path) {
		sprite_t * maybe = malloc(sizeof(sprite_t));
		if (!load_sprite(maybe, path)) return maybe;
		free(maybe);
		return icon_get_48("generic");
	}

	ad_icon = (char*)path;
	return icon_get_48(path);
}

static int usage(char * argv[]) {
	fprintf(stderr, "usage: %s [-t title] [-i icon] [-a x,y] text...", argv[0]);
	return 1;
}


int main(int argc, char * argv[]) {
	int req_center_x = 0;
	int req_center_y = 0;
	yutani_wid_t parent_window = 0;
	yctx = yutani_init();
	if (!yctx) {
		fprintf(stderr, "%s: failed to connect to compositor\n", argv[0]);
		return 1;
	}

	struct option long_opts[] = {
		{"title",required_argument,0,'t'},
		{"at",required_argument,0,'a'},
		{"icon",required_argument,0,'i'},
		{"parent",required_argument,0,'w'},
		{"okay-label",required_argument,0,'o'},
		{"cancel-label",required_argument,0,'c'},
		{"disable-cancel",no_argument,0,1},
		{"help",no_argument,0,'h'},
		{0,0,0,0},
	};

	int opt;

	while ((opt = getopt_long(argc, argv, "t:a:i:w:c:o:h", long_opts, NULL)) != -1) {
		switch (opt) {
			case 't': /* --title */
				title_str = optarg;
				break;
			case 'a': { /* --at */
				char * comma = strchr(optarg,',');
				if (!comma) return fprintf(stderr, "%s: --at argument should be x,y\n", argv[0]), 1;
				*comma++ = '\0';
				req_center_x = strtol(optarg,NULL,10);
				req_center_y = strtol(comma,NULL,10);
				break;
			}
			case 'w':
				parent_window = strtol(optarg,NULL,10);
				break;
			case 'i': /* --icon */
				icon_path = optarg;
				break;
			case 'c':
				cancel_str = optarg;
				break;
			case 'o':
				okay_str = optarg;
				break;
			case 1:
				disable_cancel = 1;
				break;
			case 'h': /* --help */
				printf("%s: try 'man showdialog'\n", argv[0]);
				return 0;
			case '?':
				return usage(argv);
		}
	}

	if (optind == argc) return usage(argv);
	body_text = &argv[optind];

	init_decorations();

	struct decor_bounds bounds;
	decor_get_bounds(NULL, &bounds);

	_tt_font = tt_font_from_shm("sans-serif");

	if (argc - optind > 3) {
		height += 20 * (argc - optind - 3);
	}

	if (parent_window) {
		window = yutani_window_create_flags(yctx, width + bounds.width, height + bounds.height, YUTANI_WINDOW_FLAG_DIALOG_ANIMATION | YUTANI_WINDOW_FLAG_PARENT_WID, (yutani_window_t*)&parent_window);
		yutani_window_move_relative(yctx, window, (yutani_window_t*)&parent_window, req_center_x - window->width / 2, req_center_y - window->height / 2);
	} else {
		window = yutani_window_create_flags(yctx, width + bounds.width, height + bounds.height, YUTANI_WINDOW_FLAG_DIALOG_ANIMATION);
		if (!req_center_x) req_center_x = yctx->display_width / 2;
		if (!req_center_y) req_center_y = yctx->display_height / 2;
		yutani_window_move(yctx, window, req_center_x - window->width / 2, req_center_y - window->height / 2);
	}

	icon = image_or_icon(icon_path);
	yutani_window_advertise_icon(yctx, window, title_str, ad_icon);

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
						yutani_window_t * win = hashmap_get(yctx->windows, (void*)(uintptr_t)wf->wid);
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
									} else if (in_button(&_cancel, me) && !disable_cancel) {
										set_hilight(&_cancel, 2);
										_down_button = &_cancel;
									}
								} else if (me->command == YUTANI_MOUSE_EVENT_RAISE || me->command == YUTANI_MOUSE_EVENT_CLICK) {
									if (_down_button) {
										if (in_button(_down_button, me)) {
											if (_down_button == &_cancel && !disable_cancel) {
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
									} else if (in_button(&_cancel, me) && !disable_cancel) {
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
