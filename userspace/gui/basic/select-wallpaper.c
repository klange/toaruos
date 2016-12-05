/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2015 Kevin Lange
 *
 * Desktop Background Selection Tool
 *
 */
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <syscall.h>
#include <signal.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "lib/yutani.h"
#include "lib/graphics.h"
#include "lib/decorations.h"
#include "gui/ttk/ttk.h"

#include "lib/hashmap.h"
#include "lib/confreader.h"

#include "lib/list.h"

#include "lib/trace.h"
#define TRACE_APP_NAME "select-wallpaper"
#define LINE_LEN 4096

#define DEFAULT_WALLPAPER "/usr/share/wallpapers/default"

static yutani_t * yctx;

static yutani_window_t * win;
static gfx_context_t * ctx;

static cairo_surface_t * surface;

static cairo_t * cr;
static int loading = 1;

static sprite_t * wallpaper_sprite;
static list_t * wallpapers;
struct wallpaper {
	char * path;
	sprite_t * sprite;
};

static int wallpaper_pid = 0;

static node_t * selected_wallpaper = NULL;
static char * selected_path = NULL;

static int should_exit = 0;

static int center_x(int x) {
	return (yctx->display_width - x) / 2;
}

static int center_y(int y) {
	return (yctx->display_height - y) / 2;
}

static int center_win_x(int x) {
	return (win->width - x) / 2;
}

#define BUTTON_HEIGHT 32
#define BUTTON_WIDTH 100

struct button {
	int left, top, width, height;
	int hover;
	const char * label;
	void (*callback)(struct button *);
};

static list_t * buttons_list = NULL;

static int draw_buttons(void) {
	foreach(node, buttons_list) {
		struct button * this = (struct button *)node->value;

		if (this->hover == 2) {
			_ttk_draw_button_select(cr, this->left, this->top, this->width, this->height, (char *)this->label);
		} else if (this->hover == 1) {
			_ttk_draw_button_hover(cr, this->left, this->top, this->width, this->height, (char *)this->label);
		} else {
			_ttk_draw_button(cr, this->left, this->top, this->width, this->height, (char *)this->label);
		}
	}
}

static void redraw(void) {
	draw_fill(ctx, rgb(TTK_BACKGROUND_DEFAULT));

	/* Draw the current tutorial frame */
	render_decorations(win, ctx, "Select Desktop Background");

	set_font_face(FONT_SANS_SERIF);
	set_font_size(12);

	if (loading) {
		char * label = "Loading...";
		int y = 200;
		int x = center_win_x(draw_string_width(label));
		draw_string(ctx, x, y, rgb(0,0,0), label);
	} else {
		draw_sprite(ctx, wallpaper_sprite, center_win_x(wallpaper_sprite->width), 80);
		int y = 80-20;
		int x = center_win_x(draw_string_width(selected_path));
		draw_string(ctx, x, y, rgb(0,0,0), selected_path);
	}

	draw_buttons();

	flip(ctx);
	yutani_flip(yctx, win);
}

static int find_wallpaper_pid(void) {

	DIR * dirp = opendir("/proc");
	int out_pid = 0;

	/* Read the entries in the directory */
	list_t * ents_list = list_create();

	struct dirent * ent = readdir(dirp);
	while (ent != NULL) {
		if (ent->d_name[0] >= '0' && ent->d_name[0] <= '9') {
			char tmp[256], buf[4096], name[128];
			FILE * f;
			int read = 1;
			char line[LINE_LEN];

			snprintf(tmp, 256, "/proc/%s/status", ent->d_name);
			f = fopen(tmp, "r");

			while (fgets(line, LINE_LEN, f) != NULL) {
				if (strstr(line, "Name:") == line) {
					sscanf(line, "%s %s", &buf, &name);
					if (!strcmp(name, "wallpaper")) {
						out_pid = atoi(ent->d_name);
						break;
					}
				}
			}

			fclose(f);

			if (out_pid) break;
		}

		ent = readdir(dirp);
	}
	closedir(dirp);

	return out_pid;

}

sprite_t * load_wallpaper(char * file) {
	sprite_t * o_wallpaper = NULL;

	sprite_t * wallpaper_tmp = calloc(1,sizeof(sprite_t));

	load_sprite_png(wallpaper_tmp, file);

	int width = 500;
	int height = 300;

	float x = (float)width  / (float)wallpaper_tmp->width;
	float y = (float)height / (float)wallpaper_tmp->height;

	int nh = (int)(x * (float)wallpaper_tmp->height);
	int nw = (int)(y * (float)wallpaper_tmp->width);;

	o_wallpaper = create_sprite(width, height, ALPHA_OPAQUE);

	gfx_context_t * tmp = init_graphics_sprite(o_wallpaper);

	if (nw > width) {
		draw_sprite_scaled(tmp, wallpaper_tmp, (width - nw) / 2, 0, nw, height);
	} else {
		draw_sprite_scaled(tmp, wallpaper_tmp, 0, (height - nh) / 2, width, nh);
	}

	free(tmp);

	sprite_free(wallpaper_tmp);

	return o_wallpaper;
}

sprite_t * load_current(void) {
	char f_name[512];

	sprintf(f_name, "%s/.desktop.conf", getenv("HOME"));

	confreader_t * conf = confreader_load(f_name);
	char * file = confreader_getd(conf, "", "wallpaper", DEFAULT_WALLPAPER);

	selected_path = strdup(file);

	sprite_t * out = load_wallpaper(file);

	confreader_free(conf);

	return out;
}

static struct button * focused_button = NULL;
static int previous_buttons = 0;
static void do_mouse_stuff(struct yutani_msg_window_mouse_event * me) {
	if (focused_button) {
		/* See if we released and are still inside. */
		if (me->command == YUTANI_MOUSE_EVENT_RAISE || me->command == YUTANI_MOUSE_EVENT_CLICK) {
			if (!(me->buttons & YUTANI_MOUSE_BUTTON_LEFT)) {
				struct button * this = focused_button;
				if (me->new_x > this->left 
					&& me->new_x < this->left + this->width
					&& me->new_y > this->top
					&& me->new_y < this->top + this->height) {
					this->hover = 1;
					this->callback(this);
					focused_button = NULL;
					redraw();
				} else {
					this->hover = 0;
					focused_button = NULL;
					redraw();
				}
			}
		}
	} else {
		foreach(node, buttons_list) {
			struct button * this = (struct button *)node->value;
			if (me->new_x > this->left 
				&& me->new_x < this->left + this->width
				&& me->new_y > this->top
				&& me->new_y < this->top + this->height) {
				if (!this->hover) {
					this->hover = 1;
					redraw();
				}
				if (me->command == YUTANI_MOUSE_EVENT_DOWN && (me->buttons & YUTANI_MOUSE_BUTTON_LEFT)) {
					this->hover = 2;
					focused_button = this;
					redraw();
				}
			} else {
				if (this->hover) {
					this->hover = 0;
					redraw();
				}
			}
		}
	}
	previous_buttons = me->buttons;
}

static void add_button(int x, int y, int width, int height, const char * label, void (*callback)(struct button *)) {
	struct button * this = malloc(sizeof(struct button));

	this->left = x;
	this->top = y;
	this->width = width;
	this->height = height;

	this->label = label;
	this->callback = callback;

	list_insert(buttons_list, this);
}

static void button_ok(struct button * this) {
	char f_name[512];
	sprintf(f_name, "%s/.desktop.conf", getenv("HOME"));

	TRACE("Okay button pressed");
	FILE * f = fopen(f_name, "w");
	fprintf(f, "wallpaper=%s\n", selected_path);
	fclose(f);

	if (wallpaper_pid) {
		kill(wallpaper_pid, SIGUSR1);
	}

}

static void button_cancel(struct button * this) {
	should_exit = 1;
}

static void button_prev(struct button * this) {
	TRACE("prev");
	if (!selected_wallpaper) {
		selected_wallpaper = wallpapers->head;
		if (!selected_wallpaper) return;
	} else {
		selected_wallpaper = selected_wallpaper->prev;
		if (!selected_wallpaper) {
			selected_wallpaper = wallpapers->head;
		}
	}

	wallpaper_sprite = ((struct wallpaper *)selected_wallpaper->value)->sprite;
	selected_path = ((struct wallpaper *)selected_wallpaper->value)->path;
	redraw();
}

static void button_next(struct button * this) {

	TRACE("next");

	if (!selected_wallpaper) {
		selected_wallpaper = wallpapers->head;
		if (!selected_wallpaper) return;
	} else {
		selected_wallpaper = selected_wallpaper->next;
		if (!selected_wallpaper) {
			selected_wallpaper = wallpapers->head;
		}
	}

	wallpaper_sprite = ((struct wallpaper *)selected_wallpaper->value)->sprite;
	selected_path = ((struct wallpaper *)selected_wallpaper->value)->path;
	redraw();
}

static void discover_directory(char * dir) {
	DIR * dirp = opendir(dir);
	if (!dirp) return;
	struct dirent * ent = readdir(dirp);
	while (ent != NULL) {
		if (ent->d_name[0] != '.' && strcmp(ent->d_name, "default")) {
			char tmp[256];
			snprintf(tmp, 256, "%s/%s", dir, ent->d_name);

			struct wallpaper * this = malloc(sizeof(struct wallpaper));

			this->path = strdup(tmp);
			this->sprite = load_wallpaper(this->path);

			list_insert(wallpapers, this);
		}
		ent = readdir(dirp);
	}
	closedir(dirp);
}

static void discover_wallpapers(void) {
	wallpapers = list_create();

	discover_directory("/usr/share/wallpapers");
	discover_directory("/tmp/wallpapers");

	TRACE("Found %d wallpaper%s.", wallpapers->length, wallpapers->length == 1 ? "" : "s");

}

int main(int argc, char * argv[]) {

	TRACE("Launching wallpaper selection...");


	wallpaper_pid = find_wallpaper_pid();
	TRACE("Wallpaper PID is %d", wallpaper_pid);

	yctx = yutani_init();

	init_decorations();

	buttons_list = list_create();
	add_button(410, 430, BUTTON_WIDTH, BUTTON_HEIGHT, "Apply", &button_ok);
	add_button(520, 430, BUTTON_WIDTH, BUTTON_HEIGHT, "Exit", &button_cancel);
	add_button(20, 200, 32, 100, "<", &button_prev);
	add_button(640-20-32, 200, 32, 100, ">", &button_next);

	win = yutani_window_create(yctx, 640, 480);
	yutani_window_move(yctx, win, center_x(640), center_y(480));
	ctx = init_graphics_yutani_double_buffer(win);

	int stride;

	stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, win->width);
	surface = cairo_image_surface_create_for_data(ctx->backbuffer, CAIRO_FORMAT_ARGB32, win->width, win->height, stride);
	cr = cairo_create(surface);

	yutani_window_advertise_icon(yctx, win, "Desktop Background", "select-wallpaper");

	redraw();

	wallpaper_sprite = load_current();
	discover_wallpapers();
	loading = 0;

	redraw();

	yutani_focus_window(yctx, win->wid);

	while (!should_exit) {
		yutani_msg_t * m = yutani_poll(yctx);

		if (m) {
			switch (m->type) {
				case YUTANI_MSG_KEY_EVENT:
					{
						struct yutani_msg_key_event * ke = (void*)m->data;
						if (ke->event.key == 'q' && ke->event.action == KEY_ACTION_DOWN) {
							should_exit = 1;
						}
					}
					break;
				case YUTANI_MSG_WINDOW_FOCUS_CHANGE:
					{
						struct yutani_msg_window_focus_change * wf = (void*)m->data;
						if (wf->wid == win->wid) {
							win->focused = wf->focused;
							redraw();
						}
					}
					break;
				case YUTANI_MSG_WINDOW_MOUSE_EVENT:
					{
						struct yutani_msg_window_mouse_event * me = (void*)m->data;
						if (me->wid != win->wid) break;
						int result = decor_handle_event(yctx, m);
						switch (result) {
							case DECOR_CLOSE:
								should_exit = 1;
								break;
							default:
								/* Other actions */
								do_mouse_stuff(me);
								break;
						}
					}
					break;
				case YUTANI_MSG_SESSION_END:
					should_exit = 1;
					break;
				default:
					break;
			}
			free(m);
		}
	}

	return 0;
}
