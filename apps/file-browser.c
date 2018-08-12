#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>

#include <sys/stat.h>
#include <sys/time.h>

#include <toaru/yutani.h>
#include <toaru/graphics.h>
#include <toaru/decorations.h>
#include <toaru/menu.h>
#include <toaru/icon_cache.h>
#include <toaru/list.h>
#include <toaru/sdf.h>

#define APPLICATION_TITLE "File Browser"

static yutani_t * yctx;
static yutani_window_t * main_window;
static gfx_context_t * ctx;

static int application_running = 1;
static int show_hidden = 1;

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

struct File {
	char name[256];
	char icon[256];
	char date[256];
};

static gfx_context_t * contents = NULL;
static sprite_t * contents_sprite = NULL;
static list_t * file_list = NULL;

static void redraw_files(void) {
	int i = 0;
	foreach(node, file_list) {
		struct File * f = node->value;
		draw_sdf_string(contents, 30, i * 24, f->name, 16, rgb(0,0,0), SDF_FONT_THIN);
		sprite_t * icon = icon_get_16(f->icon);
		draw_sprite(contents, icon, 2, i * 24 + 2);


		i++;
	}
}

static void load_directory(char * path) {
	if (file_list) {
		list_destroy(file_list);
		free(file_list);
	}

	DIR * dirp = opendir(path);

	if (!dirp) {
		/* Failed to open directory. Throw up a warning? */
		file_list = NULL;
		return;
	}

	/* Get the current time */
#if 0
	struct tm * timeinfo;
	struct timeval now;
	gettimeofday(&now, NULL); //time(NULL);
	timeinfo = localtime((time_t *)&now.tv_sec);
	int this_year = timeinfo->tm_year;
#endif

	file_list = list_create();

	struct dirent * ent = readdir(dirp);
	while (ent != NULL) {
		if (show_hidden || (ent->d_name[0] != '.')) {
			struct File * f = malloc(sizeof(struct File));
			sprintf(f->name, "%s", ent->d_name); /* snprintf? copy min()? */

			struct stat statbuf;
			//struct stat statbufl;
			//char * link;

			char tmp[strlen(path)+strlen(ent->d_name)+2];
			sprintf(tmp, "%s/%s", path, ent->d_name);
			lstat(tmp, &statbuf);
#if 0
			if (S_ISLNK(statbuf.st_mode)) {
				stat(tmp, &statbufl);
				f->link = malloc(4096);
				readlink(tmp, f->link, 4096);
			}
#endif

			if (S_ISDIR(statbuf.st_mode)) {
				sprintf(f->icon, "folder");
			} else {
				sprintf(f->icon, "file");
			}

			list_insert(file_list, f);
		}
		ent = readdir(dirp);
	}
	closedir(dirp);
}

static void reinitialize_contents(void) {
	if (contents) {
		free(contents);
	}

	if (contents_sprite) {
		sprite_free(contents_sprite);
	}

	/* Calculate height for current directory */
	int calculated_height = file_list->length * 24;

	contents_sprite = create_sprite(main_window->width - decor_width(), calculated_height, ALPHA_EMBEDDED);
	contents = init_graphics_sprite(contents_sprite);

	draw_fill(contents, rgb(255,255,255));

	/* Draw file entries */
	redraw_files();
}

static void redraw_window(void) {
	draw_fill(ctx, rgb(255,255,255));

	render_decorations(main_window, ctx, APPLICATION_TITLE);

	menu_bar.x = decor_left_width;
	menu_bar.y = decor_top_height;
	menu_bar.width = ctx->width - decor_width();
	menu_bar.window = main_window;
	menu_bar_render(&menu_bar, ctx);

	gfx_clear_clip(ctx);
	gfx_add_clip(ctx, decor_left_width, decor_top_height + MENU_BAR_HEIGHT, ctx->width - decor_width(), ctx->height - MENU_BAR_HEIGHT - decor_height());
	draw_sprite(ctx, contents_sprite, decor_left_width, decor_top_height + MENU_BAR_HEIGHT);
	gfx_clear_clip(ctx);
	gfx_add_clip(ctx, 0, 0, ctx->width, ctx->height);


	flip(ctx);
	yutani_flip(yctx, main_window);
}

static void resize_finish(int w, int h) {
	int height_changed = (main_window->width != (unsigned int)w);

	yutani_window_resize_accept(yctx, main_window, w, h);
	reinit_graphics_yutani(ctx, main_window);

	if (height_changed) {
		reinitialize_contents();
	}

	redraw_window();
	yutani_window_resize_done(yctx, main_window);

	yutani_flip(yctx, main_window);
}

static void _menu_action_input_path(struct MenuEntry * entry) {

}

static void _menu_action_navigate(struct MenuEntry * entry) {
	/* go to entry->action */
}

static void _menu_action_up(struct MenuEntry * entry) {
	/* go up */
}

static void _menu_action_help(struct MenuEntry * entry) {
	/* show help documentation */
	system("help-browser file-browser.trt &");
	redraw_window();
}

static void _menu_action_about(struct MenuEntry * entry) {
	/* Show About dialog */
	char about_cmd[1024] = "\0";
	strcat(about_cmd, "about \"About File Browser\" /usr/share/icons/48/folder.bmp \"ToaruOS File Browser\" \"(C) 2018 K. Lange\n-\nPart of ToaruOS, which is free software\nreleased under the NCSA/University of Illinois\nlicense.\n-\n%https://toaruos.org\n%https://gitlab.com/toaruos\" ");
	char coords[100];
	sprintf(coords, "%d %d &", (int)main_window->x + (int)main_window->width / 2, (int)main_window->y + (int)main_window->height / 2);
	strcat(about_cmd, coords);
	system(about_cmd);
	redraw_window();
}

int main(int argc, char * argv[]) {

	yctx = yutani_init();
	init_decorations();
	main_window = yutani_window_create(yctx, 800, 600);
	yutani_window_move(yctx, main_window, yctx->display_width / 2 - main_window->width / 2, yctx->display_height / 2 - main_window->height / 2);
	ctx = init_graphics_yutani_double_buffer(main_window);

	yutani_window_advertise_icon(yctx, main_window, APPLICATION_TITLE, "folder");

	menu_bar.entries = menu_entries;
	menu_bar.redraw_callback = redraw_window;

	menu_bar.set = menu_set_create();

	struct MenuList * m = menu_create(); /* File */
	menu_insert(m, menu_create_normal("exit",NULL,"Exit", _menu_action_exit));
	menu_set_insert(menu_bar.set, "file", m);

	m = menu_create(); /* Go */
	menu_insert(m, menu_create_normal("open",NULL,"Path...", _menu_action_input_path));
	menu_insert(m, menu_create_separator());
	menu_insert(m, menu_create_normal("home",getenv("HOME"),"Home",_menu_action_navigate));
	menu_insert(m, menu_create_normal(NULL,"/","File System",_menu_action_navigate));
	menu_insert(m, menu_create_normal("up",NULL,"Up",_menu_action_up));
	menu_set_insert(menu_bar.set, "go", m);

	m = menu_create();
	menu_insert(m, menu_create_normal("help",NULL,"Contents",_menu_action_help));
	menu_insert(m, menu_create_separator());
	menu_insert(m, menu_create_normal("star",NULL,"About " APPLICATION_TITLE,_menu_action_about));
	menu_set_insert(menu_bar.set, "help", m);

	load_directory("/usr/share");
	reinitialize_contents();
	redraw_window();

	while (application_running) {
		yutani_msg_t * m = yutani_poll(yctx);
		while (m) {
			if (menu_process_event(yctx, m)) {
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
							win->focused = wf->focused;
							redraw_window();
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
