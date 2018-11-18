/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 *
 * file-browser - Show directory listings.
 *
 * This is a basic graphical file navigator. It's based somewhat
 * on the original Python implementation. There's still a lot
 * of work to do here presentation-wise.
 */
#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>
#include <math.h>
#include <libgen.h>

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

uint64_t precise_current_time(void) {
	struct timeval t;
	gettimeofday(&t, NULL);

	time_t sec_diff = t.tv_sec;
	suseconds_t usec_diff = t.tv_usec;

	return (uint64_t)(sec_diff * 1000 + usec_diff / 1000);
}

uint64_t precise_time_since(uint64_t start_time) {

	uint32_t now = precise_current_time();
	uint32_t diff = now - start_time; /* Milliseconds */

	return diff;
}

char title[512];

static yutani_t * yctx;
static yutani_window_t * main_window;
static gfx_context_t * ctx;

#define SCROLL_AMOUNT 20
static int application_running = 1;
static int show_hidden = 1;
static int scroll_offset = 0;
static int available_height = 0;

static struct menu_bar menu_bar = {0};
static struct menu_bar_entries menu_entries[] = {
	{"File", "file"},
	{"Go", "go"},
	{"Help", "help"},
	{NULL, NULL},
};

static struct MenuList * context_menu = NULL;

static void _menu_action_exit(struct MenuEntry * entry) {
	application_running = 0;
}

struct File {
	char name[256];
	char icon[256];
	char date[256];
	int type;
	int selected;
};

static int FILE_HEIGHT = 80; /* Not a constant */
static int FILE_WIDTH = 160;
static int FILE_PTR_WIDTH = 1;
static gfx_context_t * contents = NULL;
static sprite_t * contents_sprite = NULL;
static char * last_directory = NULL;
static int hilighted_offset = -1;
static struct File ** file_pointers = NULL;
static ssize_t file_pointers_len = 0;

static int _close_enough(struct yutani_msg_window_mouse_event * me) {
	if (me->command == YUTANI_MOUSE_EVENT_RAISE && sqrt(pow(me->new_x - me->old_x, 2) + pow(me->new_y - me->old_y, 2)) < 10) {
		return 1;
	}
	return 0;
}

static void clear_offset(int offset) {
	int offset_y = offset / FILE_PTR_WIDTH;
	int offset_x = offset % FILE_PTR_WIDTH;
	draw_rectangle(contents, offset_x * FILE_WIDTH, offset_y * FILE_HEIGHT, FILE_WIDTH, FILE_HEIGHT, rgb(255,255,255));
}

static void draw_file(struct File * f, int offset) {
	int offset_y = offset / FILE_PTR_WIDTH;
	int offset_x = offset % FILE_PTR_WIDTH;
	int x = offset_x * FILE_WIDTH;
	int y = offset_y * FILE_HEIGHT;
	sprite_t * icon = icon_get_48(f->icon);

	char * name = strdup(f->name);
	int len = strlen(name);
	int name_width;
	while ((name_width = draw_sdf_string_width(name, 16, SDF_FONT_THIN)) > FILE_WIDTH - 8 /* Padding */) {
		name[len-1] = '\0';
		len--;
	}

	int center_x_icon = (FILE_WIDTH - icon->width) / 2;
	int center_x_text = (FILE_WIDTH - name_width) / 2;
	draw_sprite(contents, icon, center_x_icon + x, y + 2);
	if (f->selected) {
		draw_sprite_alpha_paint(contents, icon, center_x_icon + x, y + 2, 0.5, rgb(72,167,255));
		draw_rounded_rectangle(contents, center_x_text + x - 2, y + 54, name_width + 6, 20, 3, rgb(72,167,255));
		draw_sdf_string(contents, center_x_text + x, y + 54, name, 16, rgb(255,255,255), SDF_FONT_THIN);
	} else {
		draw_sdf_string(contents, center_x_text + x, y + 54, name, 16, rgb(0,0,0), SDF_FONT_THIN);
	}

	if (offset == hilighted_offset) {
		draw_sprite_alpha_paint(contents, icon, center_x_icon + x, y + 2, 0.3, rgb(255,255,255));
	}

	free(name);
}

static struct File * get_file_at_offset(int offset) {
	int offset_y = offset / FILE_PTR_WIDTH;
	int offset_x = offset % FILE_PTR_WIDTH;
	if (offset_y >= 0 && offset_x >= 0) {
		int off_calc = offset_y * FILE_PTR_WIDTH + offset_x;
		if (off_calc < file_pointers_len) {
			return file_pointers[off_calc];
		}
	}
	return NULL;
}

static void redraw_files(void) {
	for (int i = 0; i < file_pointers_len; ++i) {
		draw_file(file_pointers[i], i);
	}
}

static void set_title(char * directory) {
	if (directory) {
		if (!strcmp(directory, "/")) {
			directory = "File System";
		}
		sprintf(title, "%s - " APPLICATION_TITLE, directory);
	} else {
		sprintf(title, APPLICATION_TITLE);
	}
	yutani_window_advertise_icon(yctx, main_window, title, "folder");
}

static void load_directory(const char * path) {
	if (file_pointers) {
		for (int i = 0; i < file_pointers_len; ++i) {
			free(file_pointers[i]);
		}
		free(file_pointers);
	}

	DIR * dirp = opendir(path);

	if (!dirp) {
		/* Failed to open directory. Throw up a warning? */
		file_pointers = NULL;
		file_pointers_len = 0;
		return;
	}

	if (last_directory) {
		free(last_directory);
	}

	char * tmp = strdup(path);
	char * base = basename(tmp);
	set_title(base);
	free(tmp);

	last_directory = strdup(path);

	/* Get the current time */
#if 0
	struct tm * timeinfo;
	struct timeval now;
	gettimeofday(&now, NULL); //time(NULL);
	timeinfo = localtime((time_t *)&now.tv_sec);
	int this_year = timeinfo->tm_year;
#endif

	list_t * file_list = list_create();

	struct dirent * ent = readdir(dirp);
	while (ent != NULL) {
		if (ent->d_name[0] == '.' &&
			(ent->d_name[1] == '\0' || 
			 (ent->d_name[1] == '.' &&
			  ent->d_name[2] == '\0'))) {
			/* skip . and .. */
			ent = readdir(dirp);
			continue;
		}
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
				f->type = 1;
			} else {
				sprintf(f->icon, "file");
				f->type = 0;
			}

			f->selected = 0;

			list_insert(file_list, f);
		}
		ent = readdir(dirp);
	}
	closedir(dirp);

	/* create a an array to hold the files */
	file_pointers = malloc(sizeof(struct File *) * file_list->length);
	file_pointers_len = file_list->length;
	int i = 0;
	foreach (node, file_list) {
		file_pointers[i] = node->value;
		i++;
	}


	list_free(file_list);
	free(file_list);

	/* Sort files */
	int comparator(const void * c1, const void * c2) {
		const struct File * f1 = *(const struct File **)(c1);
		const struct File * f2 = *(const struct File **)(c2);
		if (f1->type == 1 && f2->type == 0) return -1;
		if (f1->type == 0 && f2->type == 1) return 1;
		return strcmp(f1->name, f2->name);
	}
	qsort(file_pointers, file_pointers_len, sizeof(struct File *), comparator);

	scroll_offset = 0;
}

static void reinitialize_contents(void) {
	if (contents) {
		free(contents);
	}

	if (contents_sprite) {
		sprite_free(contents_sprite);
	}

	struct decor_bounds bounds;
	decor_get_bounds(main_window, &bounds);

	FILE_PTR_WIDTH = (ctx->width - bounds.width) / FILE_WIDTH;
	int calculated_height = (file_pointers_len / FILE_PTR_WIDTH + 1) * FILE_HEIGHT;

	contents_sprite = create_sprite(main_window->width - bounds.width, calculated_height, ALPHA_EMBEDDED);
	contents = init_graphics_sprite(contents_sprite);

	draw_fill(contents, rgb(255,255,255));

	/* Draw file entries */
	redraw_files();
}

static void redraw_window(void) {
	draw_fill(ctx, rgb(255,255,255));

	render_decorations(main_window, ctx, title);

	struct decor_bounds bounds;
	decor_get_bounds(main_window, &bounds);

	menu_bar.x = bounds.left_width;
	menu_bar.y = bounds.top_height;
	menu_bar.width = ctx->width - bounds.width;
	menu_bar.window = main_window;
	menu_bar_render(&menu_bar, ctx);

	gfx_clear_clip(ctx);
	gfx_add_clip(ctx, bounds.left_width, bounds.top_height + MENU_BAR_HEIGHT, ctx->width - bounds.width, available_height);
	draw_sprite(ctx, contents_sprite, bounds.left_width, bounds.top_height + MENU_BAR_HEIGHT - scroll_offset);
	gfx_clear_clip(ctx);
	gfx_add_clip(ctx, 0, 0, ctx->width, ctx->height);


	flip(ctx);
	yutani_flip(yctx, main_window);
}

static void resize_finish(int w, int h) {
	int width_changed = (main_window->width != (unsigned int)w);

	yutani_window_resize_accept(yctx, main_window, w, h);
	reinit_graphics_yutani(ctx, main_window);

	struct decor_bounds bounds;
	decor_get_bounds(main_window, &bounds);

	available_height = ctx->height - MENU_BAR_HEIGHT - bounds.height;

	if (width_changed) {
		reinitialize_contents();
	}

	if (available_height > contents->height) {
		scroll_offset = 0;
	} else {
		if (scroll_offset > contents->height - available_height) {
			scroll_offset = contents->height - available_height;
		}
	}

	redraw_window();
	yutani_window_resize_done(yctx, main_window);

	yutani_flip(yctx, main_window);
}

/* TODO */
#if 0
static void _menu_action_input_path(struct MenuEntry * entry) {

}
#endif

static void _menu_action_navigate(struct MenuEntry * entry) {
	/* go to entry->action */
	struct MenuEntry_Normal * _entry = (void*)entry;
	load_directory(_entry->action);
	reinitialize_contents();
	redraw_window();
}

static void _menu_action_up(struct MenuEntry * entry) {
	/* go up */
	char * tmp = strdup(last_directory);
	char * dir = dirname(tmp);
	load_directory(dir);
	reinitialize_contents();
	redraw_window();
}

static void _menu_action_help(struct MenuEntry * entry) {
	/* show help documentation */
	system("help-browser file-browser.trt &");
	redraw_window();
}

static void _menu_action_copy(struct MenuEntry * entry) {
	size_t output_size = 0;

	int base_is_root = !strcmp(last_directory, "/"); /* avoid redundant slash */
	for (int i = 0; i < file_pointers_len; ++i) {
		if (file_pointers[i]->selected) {
			output_size += strlen(last_directory) + !base_is_root + strlen(file_pointers[i]->name) + 1; /* base / file \n */
		}
	}

	char * clipboard = malloc(output_size);
	clipboard[0] = '\0';
	for (int i = 0; i < file_pointers_len; ++i) {
		if (file_pointers[i]->selected) {
			strcat(clipboard, last_directory);
			if (!base_is_root) { strcat(clipboard, "/"); }
			strcat(clipboard, file_pointers[i]->name);
			strcat(clipboard, "\n");
		}
	}

	yutani_set_clipboard(yctx, clipboard);
	free(clipboard);
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

	struct decor_bounds bounds;
	decor_get_bounds(main_window, &bounds);

	set_title(NULL);

	menu_bar.entries = menu_entries;
	menu_bar.redraw_callback = redraw_window;

	menu_bar.set = menu_set_create();

	struct MenuList * m = menu_create(); /* File */
	menu_insert(m, menu_create_normal("exit",NULL,"Exit", _menu_action_exit));
	menu_set_insert(menu_bar.set, "file", m);

	m = menu_create(); /* Go */
	/* TODO implement input dialog for Path... */
#if 0
	menu_insert(m, menu_create_normal("open",NULL,"Path...", _menu_action_input_path));
	menu_insert(m, menu_create_separator());
#endif
	menu_insert(m, menu_create_normal("home",getenv("HOME"),"Home",_menu_action_navigate));
	menu_insert(m, menu_create_normal(NULL,"/","File System",_menu_action_navigate));
	menu_insert(m, menu_create_normal("up",NULL,"Up",_menu_action_up));
	menu_set_insert(menu_bar.set, "go", m);

	m = menu_create();
	menu_insert(m, menu_create_normal("help",NULL,"Contents",_menu_action_help));
	menu_insert(m, menu_create_separator());
	menu_insert(m, menu_create_normal("star",NULL,"About " APPLICATION_TITLE,_menu_action_about));
	menu_set_insert(menu_bar.set, "help", m);

	available_height = ctx->height - MENU_BAR_HEIGHT - bounds.height;

	context_menu = menu_create(); /* Right-click menu */
	menu_insert(context_menu, menu_create_normal("up",NULL,"Up",_menu_action_up));
	menu_insert(context_menu, menu_create_separator());
	menu_insert(context_menu, menu_create_normal(NULL,NULL,"Copy",_menu_action_copy));

	load_directory("/usr/share");
	reinitialize_contents();
	redraw_window();

	uint64_t last_click = 0; /* For double click */
	int last_click_offset = -1;
	int modifiers = 0; /* For shift-click */

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
						modifiers = ke->event.modifiers;
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
						struct decor_bounds bounds;
						decor_get_bounds(win, &bounds);

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

							if (me->new_y > (int)(bounds.top_height + MENU_BAR_HEIGHT) &&
								me->new_y < (int)(main_window->height - bounds.bottom_height) &&
								me->new_x > (int)(bounds.left_width) &&
								me->new_x < (int)(main_window->width - bounds.right_width)) {
								if (me->buttons & YUTANI_MOUSE_SCROLL_UP) {
									/* Scroll up */
									scroll_offset -= SCROLL_AMOUNT;
									if (scroll_offset < 0) {
										scroll_offset = 0;
									}
									redraw_window();
								} else if (me->buttons & YUTANI_MOUSE_SCROLL_DOWN) {
									if (available_height > contents->height) {
										scroll_offset = 0;
									} else {
										scroll_offset += SCROLL_AMOUNT;
										if (scroll_offset > contents->height - available_height) {
											scroll_offset = contents->height - available_height;
										}
									}
									redraw_window();
								}

								/* Get offset into contents */
								int y_into = me->new_y - bounds.top_height - MENU_BAR_HEIGHT + scroll_offset;
								int x_into = me->new_x - bounds.left_width;
								int offset = (y_into / FILE_HEIGHT) * FILE_PTR_WIDTH + x_into / FILE_WIDTH;
								if (x_into > FILE_PTR_WIDTH * FILE_WIDTH) {
									offset = -1;
								}
								if (offset != hilighted_offset) {
									int old_offset = hilighted_offset;
									hilighted_offset = offset;
									if (old_offset != -1) {
										clear_offset(old_offset);
										struct File * f = get_file_at_offset(old_offset);
										if (f) {
											clear_offset(old_offset);
											draw_file(f, old_offset);
										}
									}
									struct File * f = get_file_at_offset(hilighted_offset);
									if (f) {
										clear_offset(hilighted_offset);
										draw_file(f, hilighted_offset);
									}
									redraw_window();
								}

								if (me->command == YUTANI_MOUSE_EVENT_CLICK || _close_enough(me)) {
									struct File * f = get_file_at_offset(hilighted_offset);
									if (f) {
										if (last_click_offset == hilighted_offset && precise_time_since(last_click) < 400) {
											if (f->type == 1) {
												char tmp[1024];
												sprintf(tmp,"%s/%s", last_directory, f->name);
												load_directory(tmp);
												reinitialize_contents();
												redraw_window();
											}
											last_click = 0;
										} else {
											last_click = precise_current_time();
											last_click_offset = hilighted_offset;
											f->selected = !f->selected;
											if (!(modifiers & KEY_MOD_LEFT_SHIFT)) {
												for (int i = 0; i < file_pointers_len; ++i) {
													if (file_pointers[i] != f && file_pointers[i]->selected) {
														file_pointers[i]->selected = 0;
														clear_offset(i);
														draw_file(file_pointers[i], i);
													}
												}
											}
											clear_offset(hilighted_offset);
											draw_file(f, hilighted_offset);
											redraw_window();
										}
									} else {
										if (!(modifiers & KEY_MOD_LEFT_SHIFT)) {
											for (int i = 0; i < file_pointers_len; ++i) {
												if (file_pointers[i]->selected) {
													file_pointers[i]->selected = 0;
													clear_offset(i);
													draw_file(file_pointers[i], i);
												}
											}
											redraw_window();
										}
									}
								} else if (me->buttons & YUTANI_MOUSE_BUTTON_RIGHT) {
									if (!context_menu->window) {
										menu_show(context_menu, main_window->ctx);
										yutani_window_move(main_window->ctx, context_menu->window, me->new_x + main_window->x, me->new_y + main_window->y);
									}
								}

							} else {
								int old_offset = hilighted_offset;
								hilighted_offset = -1;
								if (old_offset != -1) {
									clear_offset(old_offset);
									struct File * f = get_file_at_offset(old_offset);
									if (f) {
										clear_offset(old_offset);
										draw_file(f, old_offset);
									}
									redraw_window();
								}
							}

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
