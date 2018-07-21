#include <stdio.h>
#include <unistd.h>

#include <toaru/yutani.h>
#include <toaru/graphics.h>
#include <toaru/decorations.h>
#include <toaru/menu.h>

#define APPLICATION_TITLE "Help Browser"

static yutani_t * yctx;
static yutani_window_t * main_window;
static gfx_context_t * ctx;

static int application_running = 1;

static struct menu_bar menu_bar = {0};
static struct menu_bar_entries menu_entries[] = {
	{"File", "file"},
	{"Go", "go"},
	{"Help", "help"},
	{NULL, NULL},
};

static void _menu_action_exit(struct MenuEntry * entry) {
	application_running = 0;
}

static void redraw_window(void) {
	draw_fill(ctx, rgb(255,255,255));

	render_decorations(main_window, ctx, APPLICATION_TITLE);

	menu_bar.x = decor_left_width;
	menu_bar.y = decor_top_height;
	menu_bar.width = ctx->width - decor_width();
	menu_bar.window = main_window;
	menu_bar_render(&menu_bar, ctx);

	flip(ctx);
	yutani_flip(yctx, main_window);
}

static void resize_finish(int w, int h) {
	yutani_window_resize_accept(yctx, main_window, w, h);
	reinit_graphics_yutani(ctx, main_window);

	redraw_window();
	yutani_window_resize_done(yctx, main_window);

	yutani_flip(yctx, main_window);
}

static void _menu_action_navigate(struct MenuEntry * entry) {
	/* go to entry->action */
}

static void _menu_action_back(struct MenuEntry * entry) {
	/* go back */
}

static void _menu_action_forward(struct MenuEntry * entry) {
	/* go forward */
}

static void _menu_action_about(struct MenuEntry * entry) {
	/* Show About dialog */
	if (!fork()) {
		char about_cmd[1024] = "\0";
		strcat(about_cmd, "about \"About Help Browser\" /usr/share/icons/48/help.bmp \"ToaruOS Help Browser\" \"(C) 2018 K. Lange\n-\nPart of ToaruOS, which is free software\nreleased under the NCSA/University of Illinois\nlicense.\n-\n%https://toaruos.org\n%https://gitlab.com/toarus\" ");
		char coords[100];
		sprintf(coords, "%d %d", (int)main_window->x + (int)main_window->width / 2, (int)main_window->y + (int)main_window->height / 2);
		strcat(about_cmd, coords);
		system(about_cmd);
		exit(0);
	}
	redraw_window();
}

int main(int argc, char * argv[]) {

	yctx = yutani_init();
	init_decorations();
	main_window = yutani_window_create(yctx, 640, 480);
	yutani_window_move(yctx, main_window, yctx->display_width / 2 - main_window->width / 2, yctx->display_height / 2 - main_window->height / 2);
	ctx = init_graphics_yutani_double_buffer(main_window);

	yutani_window_advertise_icon(yctx, main_window, APPLICATION_TITLE, "help");

	menu_bar.entries = menu_entries;
	menu_bar.redraw_callback = redraw_window;

	menu_bar.set = menu_set_create();

	struct MenuList * m = menu_create(); /* File */
	menu_insert(m, menu_create_normal("exit",NULL,"Exit", _menu_action_exit));
	menu_set_insert(menu_bar.set, "file", m);

	m = menu_create(); /* Go */
	menu_insert(m, menu_create_normal("home","0_index.trt","Home",_menu_action_navigate));
	menu_insert(m, menu_create_normal("bookmark","special:contents","Topics",_menu_action_navigate));
	menu_insert(m, menu_create_separator());
	menu_insert(m, menu_create_normal("back",NULL,"Back",_menu_action_back));
	menu_insert(m, menu_create_normal("forward",NULL,"Forward",_menu_action_forward));
	menu_set_insert(menu_bar.set, "go", m);

	m = menu_create();
	menu_insert(m, menu_create_normal("help","help_browser.trt","Contents",_menu_action_navigate));
	menu_insert(m, menu_create_separator());
	menu_insert(m, menu_create_normal("star",NULL,"About " APPLICATION_TITLE,_menu_action_about));
	menu_set_insert(menu_bar.set, "help", m);

	redraw_window();

	while (application_running) {
		yutani_msg_t * m = yutani_poll(yctx);
		while (m) {
			if (menu_process_event(yctx, m)) {
				main_window->focused = 0;
				redraw_window();
			}
			switch (m->type) {
				case YUTANI_MSG_KEY_EVENT:
					{
						struct yutani_msg_key_event * ke = (void*)m->data;
						if (ke->event.action == KEY_ACTION_DOWN && ke->event.keycode == 'q') {
							_menu_action_exit(NULL);
						}
					}
					break;
				case YUTANI_MSG_WINDOW_FOCUS_CHANGE:
					{
						struct yutani_msg_window_focus_change * wf = (void*)m->data;
						yutani_window_t * win = hashmap_get(yctx->windows, (void*)wf->wid);
						if (win == main_window) {
							if (!hashmap_is_empty(menu_get_windows_hash())) {
								win->focused = 1;
								redraw_window();
							} else {
								win->focused = wf->focused;
								redraw_window();
							}
						}
					}
					break;
				case YUTANI_MSG_RESIZE_OFFER:
					{
						struct yutani_msg_window_resize * wr = (void*)m->data;
						if (wr->wid == main_window->wid) {
							resize_finish(wr->width, wr->height);
						}
					}
					break;
				case YUTANI_MSG_WINDOW_MOUSE_EVENT:
					{
						struct yutani_msg_window_mouse_event * me = (void*)m->data;
						yutani_window_t * win = hashmap_get(yctx->windows, (void*)me->wid);

						if (win == main_window) {
							int result = decor_handle_event(yctx, m);
							switch (result) {
								case DECOR_CLOSE:
									_menu_action_exit(NULL);
									break;
								case DECOR_RIGHT:
									/* right click in decoration, show appropriate menu */
									decor_show_default_menu(main_window, main_window->x + me->new_x, main_window->y + me->new_y);
									break;
								default:
									/* Other actions */
									break;
							}

							/* Menu bar */
							menu_bar_mouse_event(yctx, main_window, &menu_bar, me, me->new_x, me->new_y);
						}
					}
					break;
				case YUTANI_MSG_WINDOW_CLOSE:
				case YUTANI_MSG_SESSION_END:
					_menu_action_exit(NULL);
					break;
				default:
					break;
			}
			free(m);
			m = yutani_poll_async(yctx);
		}
	}
}
