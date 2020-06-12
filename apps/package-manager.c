/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 *
 * package-manager - Graphical interface to msk
 */
#include <stdio.h>
#include <unistd.h>
#include <math.h>

#include <sys/time.h>
#include <sys/stat.h>

#include <toaru/yutani.h>
#include <toaru/graphics.h>
#include <toaru/decorations.h>
#include <toaru/menu.h>
#include <toaru/sdf.h>
#include <toaru/confreader.h>
#include <toaru/icon_cache.h>

#define APPLICATION_TITLE "Package Manager"
#define SCROLL_AMOUNT 120
#define VAR_PATH "/var/msk"

static yutani_t * yctx;
static yutani_window_t * main_window;
static gfx_context_t * ctx;

static int application_running = 1;

static gfx_context_t * contents = NULL;
static sprite_t * contents_sprite = NULL;

static int available_height = 0; /* How much space is available in the main window for the icon view */
static int scroll_offset = 0; /* How far the icon view should be scrolled */
static int hilighted_offset = -1; /* Which file is hovered by the mouse */
static uint64_t last_click = 0; /* For double click */
static int last_click_offset = -1; /* So that clicking two different things quickly doesn't count as a double click */

struct Package {
	char name[256];
	char friendly_name[256];
	char description[1024];
	char version[256]; /* Really doesn't need to be that long */
	int selected;
	int installed;
};

static struct Package ** pkg_pointers = NULL; /* List of package pointers */
static ssize_t pkg_pointers_len = 0; /* How many packages are in the current list */

static struct menu_bar menu_bar = {0};
static struct menu_bar_entries menu_entries[] = {
	{"File", "file"},
	{"Index", "index"},
	{"Help", "help"},
	{NULL, NULL},
};

static void _menu_action_exit(struct MenuEntry * entry) {
	application_running = 0;
}

/**
 * Accurate time comparison.
 *
 * These methods were taken from the compositor and
 * allow us to time double-clicks accurately.
 */
static uint64_t precise_current_time(void) {
	struct timeval t;
	gettimeofday(&t, NULL);

	time_t sec_diff = t.tv_sec;
	suseconds_t usec_diff = t.tv_usec;

	return (uint64_t)((uint64_t)sec_diff * 1000LL + usec_diff / 1000);
}

static uint64_t precise_time_since(uint64_t start_time) {

	uint64_t now = precise_current_time();
	uint64_t diff = now - start_time; /* Milliseconds */

	return diff;
}

static int _close_enough(struct yutani_msg_window_mouse_event * me) {
	if (me->command == YUTANI_MOUSE_EVENT_RAISE && sqrt(pow(me->new_x - me->old_x, 2) + pow(me->new_y - me->old_y, 2)) < 10) {
		return 1;
	}
	return 0;
}

#define BUTTON_HEIGHT 28
#define BUTTON_WIDTH 86
#define BUTTON_PADDING 14
#define HILIGHT_BORDER_TOP rgb(54,128,205)
#define HILIGHT_GRADIENT_TOP rgb(93,163,236)
#define HILIGHT_GRADIENT_BOTTOM rgb(56,137,220)
#define HILIGHT_BORDER_BOTTOM rgb(47,106,167)

#define PKG_HEIGHT 70
static void draw_package(struct Package * package, int index) {
	int offset_y = index * PKG_HEIGHT;

	if (package->selected) {
		if (main_window->focused) {
			draw_rectangle_solid(contents, 0, offset_y, contents->width, PKG_HEIGHT, rgb(93,163,236));
			draw_line(contents, 0, contents->width, offset_y, offset_y, HILIGHT_BORDER_TOP);
			draw_line(contents, 0, contents->width, offset_y + PKG_HEIGHT-1, offset_y + PKG_HEIGHT-1, HILIGHT_BORDER_BOTTOM);
			for (int i = 1; i < PKG_HEIGHT - 2; ++i) {
				int thing = ((i - 1) * 256) / (PKG_HEIGHT - 3);
				if (thing > 255) thing = 255;
				if (thing < 0) thing = 0;
				uint32_t c = interp_colors(HILIGHT_GRADIENT_TOP, HILIGHT_GRADIENT_BOTTOM, thing);
				draw_line(contents, 0, contents->width, offset_y + i, offset_y + i, c);
			}
		} else {
			draw_rectangle_solid(contents, 0, offset_y, contents->width, PKG_HEIGHT, rgb(180,180,180));
		}
	}

	sprite_t * icon = package->installed ? icon_get_48("package") : icon_get_48("package-uninstalled");
	draw_sprite(contents, icon, 8, offset_y + 11);

	uint32_t text_color = package->selected ? rgb(255,255,255) : rgb(0,0,0);

	char tmp[2048];
	sprintf(tmp, "%s - %s", package->friendly_name, package->version);
	draw_sdf_string(contents, 64, offset_y + 4, tmp, 20, text_color, SDF_FONT_BOLD);
	sprintf(tmp, "%s - %s", package->name, package->description);
	int x = draw_sdf_string(contents, 65, offset_y + 24, package->name, 16, rgb(150,150,150), SDF_FONT_THIN);
	draw_sdf_string(contents, 64 + x + 4, offset_y + 24, package->description, 16, text_color, SDF_FONT_THIN);

}

static void redraw_packages(void) {
	draw_fill(contents, rgba(0,0,0,0));

	for (int i = 0; i < pkg_pointers_len; ++i) {
		draw_package(pkg_pointers[i], i);
	}
}

static void load_manifest(void) {
	if (pkg_pointers) {
		for (int i = 0; i < pkg_pointers_len; ++i) {
			free(pkg_pointers[i]);
		}
		free(pkg_pointers);
		pkg_pointers_len = 0;
	}

	confreader_t * conf = confreader_load(VAR_PATH "/manifest");
	if (conf) {
		hashmap_t * msk_installed = hashmap_create(10);

		FILE * installed = fopen(VAR_PATH "/installed", "r");
		if (installed) {
			while (!feof(installed)) {
				char tmp[128] = {0};
				if (!fgets(tmp, 128, installed)) break;
				char * nl = strstr(tmp, "\n");
				if (nl) *nl = '\0';

				char * eqeq = strstr(tmp, "==");
				if (!eqeq) {
					/* show error */
					break;
				}

				*eqeq = '\0';
				char * version = eqeq+2;

				hashmap_set(msk_installed, tmp, strdup(version));
			}
		}

		list_t * package_list = list_create();

		/* Go through sections */
		list_t * packages = hashmap_keys(conf->sections);
		foreach(node, packages) {
			char * name = node->value;
			if (!strlen(name)) continue; /* skip empty section */
			char * desc = confreader_get(conf, name, "description");
			char * version = confreader_get(conf, name, "version");
			char * friendly_name = confreader_get(conf, name, "friendly-name");

			struct Package * p = malloc(sizeof(struct Package));
			sprintf(p->name, name);
			sprintf(p->friendly_name, friendly_name);
			sprintf(p->description, desc);
			sprintf(p->version, version);
			p->selected = 0;
			p->installed = hashmap_has(msk_installed, name);

			list_insert(package_list, p);
		}
		list_free(packages);
		free(packages);

		hashmap_free(msk_installed);
		free(msk_installed);

		pkg_pointers = malloc(sizeof(struct Package *) * package_list->length);
		pkg_pointers_len = package_list->length;
		int i = 0;
		foreach (node, package_list) {
			pkg_pointers[i] = node->value;
			i++;
		}

		list_free(package_list);
		free(package_list);

		int comparator(const void * c1, const void * c2) {
			const struct Package * f1 = *(const struct Package **)(c1);
			const struct Package * f2 = *(const struct Package **)(c2);
			return strcmp(f1->name, f2->name);
		}
		qsort(pkg_pointers, pkg_pointers_len, sizeof(struct Package *), comparator);
	}
}

static struct Package * get_package_at_offset(int offset) {
	if (offset >= 0 && offset < pkg_pointers_len) {
		return pkg_pointers[offset];
	}
	return NULL;
}

static void clear_offset(int offset) {
	draw_rectangle_solid(contents, 0, offset * PKG_HEIGHT, contents->width, PKG_HEIGHT, rgba(0,0,0,0));
}

static void reinitialize_contents(void) {
	if (contents) {
		free(contents);
	}

	if (contents_sprite) {
		sprite_free(contents_sprite);
	}

	/* Calculate height for current directory */
	int calculated_height = pkg_pointers_len * PKG_HEIGHT;

	struct decor_bounds bounds;
	decor_get_bounds(main_window, &bounds);

	contents_sprite = create_sprite(main_window->width - bounds.width, calculated_height, ALPHA_EMBEDDED);
	contents = init_graphics_sprite(contents_sprite);

	/* Draw packages */
	redraw_packages();
}

static void redraw_window(void) {
	draw_fill(ctx, rgb(255,255,255));

	render_decorations(main_window, ctx, APPLICATION_TITLE);

	struct decor_bounds bounds;
	decor_get_bounds(main_window, &bounds);

	menu_bar.x = bounds.left_width;
	menu_bar.y = bounds.top_height;
	menu_bar.width = ctx->width - bounds.width;
	menu_bar.window = main_window;
	menu_bar_render(&menu_bar, ctx);

	gfx_clear_clip(ctx);
	gfx_add_clip(ctx, bounds.left_width, bounds.top_height + MENU_BAR_HEIGHT, ctx->width - bounds.width, ctx->height - MENU_BAR_HEIGHT - bounds.height);
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

	/* Recalculate available size */
	available_height = ctx->height - MENU_BAR_HEIGHT - bounds.height;

	/* If the width changed, we need to rebuild the icon view */
	if (width_changed) {
		reinitialize_contents();
	}

	/* Make sure we're not scrolled weirdly after resizing */
	if (available_height > contents->height) {
		scroll_offset = 0;
	} else {
		if (scroll_offset > contents->height - available_height) {
			scroll_offset = contents->height - available_height;
		}
	}

	/* Redraw */
	redraw_window();
	yutani_window_resize_done(yctx, main_window);

	yutani_flip(yctx, main_window);
}

static void _menu_action_refresh(struct MenuEntry * entry) {
	/* refresh msk manifest */
	system("terminal msk update");

	load_manifest();
	reinitialize_contents();
	redraw_window();
}

static void install_package(struct Package * package) {
	if (package->installed) return;

	putenv("MSK_YES=1");
	char tmp[1024];
	sprintf(tmp, "terminal msk install %s", package->name);
	system(tmp);

	load_manifest();
	reinitialize_contents();
	redraw_window();
}

static void _menu_action_about(struct MenuEntry * entry) {
	/* Show About dialog */
	char about_cmd[1024] = "\0";
	strcat(about_cmd, "about \"About " APPLICATION_TITLE "\" /usr/share/icons/48/package.png \"ToaruOS " APPLICATION_TITLE "\" \"(C) 2018 K. Lange\n-\nPart of ToaruOS, which is free software\nreleased under the NCSA/University of Illinois\nlicense.\n-\n%https://toaruos.org\n%https://github.com/klange/toaruos\" ");
	char coords[100];
	sprintf(coords, "%d %d &", (int)main_window->x + (int)main_window->width / 2, (int)main_window->y + (int)main_window->height / 2);
	strcat(about_cmd, coords);
	system(about_cmd);
	redraw_window();
}

static void _menu_action_help(struct MenuEntry * entry) {
	/* show help documentation */
	system("help-browser package-manager.trt &");
	redraw_window();
}

static void toggle_selected(int hilighted_offset, int modifiers) {
	struct Package * f = get_package_at_offset(hilighted_offset);

	/* No file at this offset, do nothing. */
	if (!f) return;

	/* Toggle selection of the current file */
	f->selected = !f->selected;

	/* If Ctrl wasn't held, unselect everything else. */
	if (!(modifiers & KEY_MOD_LEFT_CTRL)) {
		for (int i = 0; i < pkg_pointers_len; ++i) {
			if (pkg_pointers[i] != f && pkg_pointers[i]->selected) {
				pkg_pointers[i]->selected = 0;
				clear_offset(i);
				draw_package(pkg_pointers[i], i);
			}
		}
	}

	/* Redraw the pkg */
	clear_offset(hilighted_offset);
	draw_package(f, hilighted_offset);

	/* And repaint the window */
	redraw_window();
}

static void _scroll_up(void) {
	scroll_offset -= SCROLL_AMOUNT;
	if (scroll_offset < 0) {
		scroll_offset = 0;
	}
}

static void _scroll_down(void) {
	if (available_height > contents->height) {
		scroll_offset = 0;
	} else {
		scroll_offset += SCROLL_AMOUNT;
		if (scroll_offset > contents->height - available_height) {
			scroll_offset = contents->height - available_height;
		}
	}
}

static void arrow_select(int y) {
	if (!pkg_pointers_len) return;

	/* Find first selected */
	int selected = -1;
	for (int i = 0; i <pkg_pointers_len; ++i) {
		if (pkg_pointers[i]->selected) {
			selected = i;
		}
		pkg_pointers[i]->selected = 0;
	}

	if (selected == -1) {
		selected = 0;
	} else {
		selected += y;
		if (selected >= pkg_pointers_len) selected = pkg_pointers_len - 1;
		if (selected < 0) selected = 0;
	}

	if (selected * PKG_HEIGHT < scroll_offset) {
		scroll_offset = selected * PKG_HEIGHT;
	}
	if (selected * PKG_HEIGHT + PKG_HEIGHT > scroll_offset + available_height) {
		scroll_offset = selected * PKG_HEIGHT + PKG_HEIGHT - available_height;
	}

	pkg_pointers[selected]->selected = 1;
	reinitialize_contents();
	redraw_window();
}

int main(int argc, char * argv[]) {

	if (geteuid() != 0) {
		char * args[] = {
			"showdialog",
			"Package Manager",
			"/usr/share/icons/48/package.png",
			"Only root can manage packages.",
			NULL,
		};
		return execvp("showdialog", args);
	}

	yctx = yutani_init();
	init_decorations();
	main_window = yutani_window_create(yctx, 640, 480);
	yutani_window_move(yctx, main_window, yctx->display_width / 2 - main_window->width / 2, yctx->display_height / 2 - main_window->height / 2);
	ctx = init_graphics_yutani_double_buffer(main_window);

	yutani_window_advertise_icon(yctx, main_window, APPLICATION_TITLE, "package");

	menu_bar.entries = menu_entries;
	menu_bar.redraw_callback = redraw_window;

	menu_bar.set = menu_set_create();

	struct MenuList * m = menu_create(); /* File */
	menu_insert(m, menu_create_normal("exit",NULL,"Exit", _menu_action_exit));
	menu_set_insert(menu_bar.set, "file", m);

	m = menu_create(); /* Go */
	menu_insert(m, menu_create_normal("refresh",NULL,"Refresh",_menu_action_refresh));
	menu_set_insert(menu_bar.set, "index", m);

	m = menu_create();
	menu_insert(m, menu_create_normal("help","help_browser.trt","Contents",_menu_action_help));
	menu_insert(m, menu_create_separator());
	menu_insert(m, menu_create_normal("star",NULL,"About " APPLICATION_TITLE,_menu_action_about));
	menu_set_insert(menu_bar.set, "help", m);

	struct decor_bounds bounds;
	decor_get_bounds(main_window, &bounds);
	available_height = ctx->height - MENU_BAR_HEIGHT - bounds.height;

	struct stat statbuf;
	if (!stat(VAR_PATH "/manifest",&statbuf)) {
		load_manifest();
		reinitialize_contents();
		redraw_window();
	} else {
		_menu_action_refresh(NULL);
		/* also redraws window */
	}

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
						if (ke->event.action == KEY_ACTION_DOWN && ke->wid == main_window->wid) {
							switch (ke->event.keycode) {
								case KEY_PAGE_UP:
									_scroll_up();
									redraw_window();
									break;
								case KEY_PAGE_DOWN:
									_scroll_down();
									redraw_window();
									break;
								case KEY_ARROW_DOWN:
									arrow_select(1);
									break;
								case KEY_ARROW_UP:
									arrow_select(-1);
									break;
								case '\n':
									for (int i = 0; i <pkg_pointers_len; ++i) {
										if (pkg_pointers[i]->selected) {
											install_package(pkg_pointers[i]);
										}
									}
									break;
								case 'f':
									if (ke->event.modifiers & YUTANI_KEY_MODIFIER_ALT) {
										menu_bar_show_menu(yctx,main_window,&menu_bar,-1,&menu_entries[0]);
									}
									break;
								case 'i':
									if (ke->event.modifiers & YUTANI_KEY_MODIFIER_ALT) {
										menu_bar_show_menu(yctx,main_window,&menu_bar,-1,&menu_entries[1]);
									}
									break;
								case 'h':
									if (ke->event.modifiers & YUTANI_KEY_MODIFIER_ALT) {
										menu_bar_show_menu(yctx,main_window,&menu_bar,-1,&menu_entries[2]);
									}
									break;
								case 'q':
									_menu_action_exit(NULL);
									break;
								default:
									break;
							}
						}
					}
					break;
				case YUTANI_MSG_WINDOW_FOCUS_CHANGE:
					{
						struct yutani_msg_window_focus_change * wf = (void*)m->data;
						yutani_window_t * win = hashmap_get(yctx->windows, (void*)wf->wid);
						if (win == main_window) {
							win->focused = wf->focused;
							redraw_packages();
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

							if (me->new_y > (int)(bounds.top_height + MENU_BAR_HEIGHT) &&
								me->new_y < (int)(main_window->height - bounds.bottom_height) &&
								me->new_x > (int)(bounds.left_width) &&
								me->new_x < (int)(main_window->width - bounds.right_width)) {
								if (me->buttons & YUTANI_MOUSE_SCROLL_UP) {
									_scroll_up();
									redraw_window();
								} else if (me->buttons & YUTANI_MOUSE_SCROLL_DOWN) {
									_scroll_down();
									redraw_window();
								}

								/* Get offset into contents */
								int y_into = me->new_y - bounds.top_height - MENU_BAR_HEIGHT + scroll_offset;
								int offset = (y_into / PKG_HEIGHT);
								if (offset != hilighted_offset) {
									int old_offset = hilighted_offset;
									hilighted_offset = offset;
									if (old_offset != -1) {
										clear_offset(old_offset);
										struct Package * f = get_package_at_offset(old_offset);
										if (f) {
											clear_offset(old_offset);
											draw_package(f, old_offset);
										}
									}
									struct Package * f = get_package_at_offset(hilighted_offset);
									if (f) {
										clear_offset(hilighted_offset);
										draw_package(f, hilighted_offset);
									}
									redraw_window();
								}

								if (me->command == YUTANI_MOUSE_EVENT_CLICK || _close_enough(me)) {
									struct Package * f = get_package_at_offset(hilighted_offset);
									if (f) {
										if (last_click_offset == hilighted_offset && precise_time_since(last_click) < 400) {
											install_package(f);
											//open_file(f);
											last_click = 0;
										} else {
											last_click = precise_current_time();
											last_click_offset = hilighted_offset;
											toggle_selected(hilighted_offset, me->modifiers);
										}
									} else {
										if (!(me->modifiers & YUTANI_KEY_MODIFIER_CTRL)) {
											for (int i = 0; i < pkg_pointers_len; ++i) {
												if (pkg_pointers[i]->selected) {
													pkg_pointers[i]->selected = 0;
													clear_offset(i);
													draw_package(pkg_pointers[i], i);
												}
											}
											redraw_window();
										}
									}
								} else if (me->buttons & YUTANI_MOUSE_BUTTON_RIGHT) {
#if 0
									if (!context_menu->window) {
										struct Package * f = get_package_at_offset(hilighted_offset);
										if (f && !f->selected) {
											toggle_selected(hilighted_offset, me->modifiers);
										}
										menu_show(context_menu, main_window->ctx);
										yutani_window_move(main_window->ctx, context_menu->window, me->new_x + main_window->x, me->new_y + main_window->y);
									}
#endif
								}

							} else {
								int old_offset = hilighted_offset;
								hilighted_offset = -1;
								if (old_offset != -1) {
									clear_offset(old_offset);
									struct Package * f = get_package_at_offset(old_offset);
									if (f) {
										clear_offset(old_offset);
										draw_package(f, old_offset);
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
