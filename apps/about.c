#include <toaru/yutani.h>
#include <toaru/graphics.h>
#include <toaru/decorations.h>
#include <toaru/sdf.h>
#include <toaru/menu.h>

#include <sys/utsname.h>

static yutani_t * yctx;
static yutani_window_t * window = NULL;
static gfx_context_t * ctx = NULL;
static sprite_t logo;

static int32_t width = 350;
static int32_t height = 250;
static char * version_str;

static char * icon_path;
static char * title_str;
static char * version_str;
static char * copyright_str[20] = {NULL};

static int center_x(int x) {
	return (width - x) / 2;
}

static void draw_string(int y, const char * string, int font, uint32_t color) {
	draw_sdf_string(ctx, decor_left_width + center_x(draw_sdf_string_width(string, 16, font)), decor_top_height + 10 + logo.height + 10 + y, string, 16, color, font);
}

static void redraw(void) {

	draw_fill(ctx, rgb(204,204,204));
	draw_sprite(ctx, &logo, decor_left_width + center_x(logo.width), decor_top_height + 10);

	draw_string(0, version_str, SDF_FONT_BOLD, rgb(0,0,0));

	int offset = 20;

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

	window->decorator_flags |= DECOR_FLAG_NO_MAXIMIZE;
	render_decorations(window, ctx, title_str);

	flip(ctx);
	yutani_flip(yctx, window);
}

static void init_default(void) {
	title_str = "About ToaruOS-NIH";
	icon_path = "/usr/share/logo_login.bmp";

	{
		version_str = malloc(100);
		struct utsname u;
		uname(&u);
		char * tmp = strstr(u.release, "-");
		if (tmp) {
			*tmp = '\0';
		}
		sprintf(version_str, "ToaruOS-NIH %s", u.release);
	}

	copyright_str[0] = "(C) 2011-2018 K. Lange, et al.";
	copyright_str[1] = "-";
	copyright_str[2] = "ToaruOS is free software released under the";
	copyright_str[3] = "NCSA/University of Illinois license.";
	copyright_str[4] = "-";
	copyright_str[5] = "%https://toaruos.org";
	copyright_str[6] = "%https://gitlab.com/toaruos";

}

int main(int argc, char * argv[]) {
	int req_center_x, req_center_y;
	yctx = yutani_init();
	init_decorations();

	window = yutani_window_create(yctx, width + decor_width(), height + decor_height());
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
		version_str = argv[3];

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
#if 0
				case YUTANI_MSG_RESIZE_OFFER:
					{
						struct yutani_msg_window_resize * wr = (void*)m->data;
						resize_finish(wr->width, wr->height);
					}
					break;
#endif
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
