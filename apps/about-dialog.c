/**
 * @brief about-dialog - Show an "About <Application>" dialog.
 *
 * By default, shows "About ToaruOS", suitable for use as an application
 * menu entry. Optionally, takes arguments specifying another application
 * to describe, suitable for the "Help > About" menu bar entry.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018-2026 K. Lange
 */
#include <getopt.h>
#include <toaru/yutani.h>
#include <toaru/graphics.h>
#include <toaru/decorations.h>
#include <toaru/menu.h>
#include <toaru/text.h>
#include <toaru/markup_text.h>
#include <toaru/icon_cache.h>

#include <sys/utsname.h>

static yutani_t * yctx;
static yutani_window_t * window = NULL;
static gfx_context_t * ctx = NULL;
static sprite_t * logo = NULL;

static int32_t width = 350;
static int32_t height = 250;

static char * logo_path = "generic";
static char * icon_name = "generic";
static char * title_str = "About";
static char * version_str = "";
static char ** body_text = NULL;

static int center_x(int x) {
	return (width - x) / 2;
}

static void draw_string(int y, const char * string, int mode, uint32_t color) {
	struct decor_bounds bounds;
	decor_get_bounds(window, &bounds);
	struct MarkupState * renderer = markup_setup_renderer(NULL, 0, 0, 0, 1);
	markup_set_base_font_size(renderer, 13);
	markup_set_base_state(renderer, mode);
	markup_push_string(renderer, string);
	int calcwidth = markup_finish_renderer(renderer);

	renderer = markup_setup_renderer(ctx, bounds.left_width + center_x(calcwidth), bounds.top_height + 10 + logo->height + 10 + y + 13, color, 0);
	markup_set_base_font_size(renderer, 13);
	markup_set_base_state(renderer, mode);
	markup_push_string(renderer, string);
	markup_finish_renderer(renderer);
}

static void redraw(void) {

	struct decor_bounds bounds;
	decor_get_bounds(window, &bounds);

	draw_fill(ctx, rgb(204,204,204));
	draw_sprite(ctx, logo, bounds.left_width + center_x(logo->width), bounds.top_height + 10);

	draw_string(0, version_str, MARKUP_TEXT_STATE_BOLD, rgb(0,0,0));

	int offset = 20;

	for (char ** str = body_text; *str; ++str) {
		if (**str == '-') {
			offset += 10;
		} else if (**str == '%') {
			draw_string(offset, *str+1, 0, rgb(0,0,255));
			offset += 20;
		} else {
			draw_string(offset, *str, 0, rgb(0,0,0));
			offset += 20;
		}
	}

	window->decorator_flags |= DECOR_FLAG_NO_MAXIMIZE;
	render_decorations(window, ctx, title_str);

	flip(ctx);
	yutani_flip(yctx, window);
}

void resize_finish(int w, int h) {
	yutani_window_resize_accept(yctx, window, w, h);
	reinit_graphics_yutani(ctx, window);
	struct decor_bounds bounds;
	decor_get_bounds(NULL, &bounds);
	width  = w - bounds.width;
	height = h - bounds.height;
	redraw();
	yutani_window_resize_done(yctx, window);
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

	return icon_get_48(path);
}

static int usage(char * argv[]) {
	fprintf(stderr, "usage: %s [-t title] [-i icon] [-l logo] [-n name] [-a x,y] text...\n", argv[0]);
	return 1;
}

int main(int argc, char * argv[]) {
	int req_center_x = 0;
	int req_center_y = 0;
	yutani_wid_t parent_window = 0;

	struct option long_opts[] = {
		{"title",required_argument,0,'t'},
		{"title-about",required_argument,0,'T'},
		{"at",required_argument,0,'a'},
		{"icon",required_argument,0,'i'},
		{"logo",required_argument,0,'l'},
		{"name",required_argument,0,'n'},
		{"parent",required_argument,0,'w'},
		{"help",no_argument,0,'h'},
		{0,0,0,0},
	};

	int opt;

	while ((opt = getopt_long(argc, argv, "t:T:a:i:l:w:n:h", long_opts, NULL)) != -1) {
		switch (opt) {
			case 't': /* --title */
				title_str = optarg;
				break;
			case 'T':
				asprintf(&title_str, "About %s", optarg);
				break;
			case 'n':
				version_str = optarg;
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
				icon_name = optarg;
				break;
			case 'l':
				logo_path = optarg;
				break;
			case 'h': /* --help */
				printf("%s: try 'man about-dialog'\n", argv[0]);
				return 0;
			case '?':
				return usage(argv);
		}
	}

	if (optind == argc) return usage(argv);
	body_text = &argv[optind];

	yctx = yutani_init();
	if (!yctx) {
		fprintf(stderr, "%s: failed to connect to compositor\n", argv[0]);
		return 1;
	}

	init_decorations();
	markup_text_init();

	struct decor_bounds bounds;
	decor_get_bounds(NULL, &bounds);

	logo = image_or_icon(logo_path);

	int text_offset = 0;
	for (char ** str = body_text; *str; ++str) text_offset += (**str == '-') ? 10 : 20;
	height = 50 + logo->height + text_offset;

	if (parent_window) {
		window = yutani_window_create_flags(yctx, width + bounds.width, height + bounds.height, YUTANI_WINDOW_FLAG_DIALOG_ANIMATION | YUTANI_WINDOW_FLAG_PARENT_WID, (yutani_window_t*)&parent_window);
		yutani_window_move_relative(yctx, window, (yutani_window_t*)&parent_window, req_center_x - window->width / 2, req_center_y - window->height / 2);
	} else {
		window = yutani_window_create_flags(yctx, width + bounds.width, height + bounds.height, YUTANI_WINDOW_FLAG_DIALOG_ANIMATION);
		if (!req_center_x) req_center_x = yctx->display_width / 2;
		if (!req_center_y) req_center_y = yctx->display_height / 2;
		yutani_window_move(yctx, window, req_center_x - window->width / 2, req_center_y - window->height / 2);
	}

	yutani_window_advertise_icon(yctx, window, title_str, icon_name);
	ctx = init_graphics_yutani_double_buffer(window);

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

