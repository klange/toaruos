/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2019 K. Lange
 *
 * wallpaper-picker - select wallpapers from available options
 */
#include <signal.h>
#include <dirent.h>
#include <toaru/yutani.h>
#include <toaru/graphics.h>
#include <toaru/decorations.h>
#include <toaru/sdf.h>
#include <toaru/menu.h>
#include <toaru/button.h>
#include <toaru/list.h>

#include <sys/utsname.h>

#define BUTTON_HEIGHT 28
#define BUTTON_WIDTH 86
#define BUTTON_PADDING 14

static yutani_t * yctx;
static yutani_window_t * window = NULL;
static gfx_context_t * ctx = NULL;
static sprite_t wallpaper = { 0 };

static int32_t width = 640;
static int32_t height = 300;
static char * title_str = "Wallpaper Picker";
#define DEFAULT_PATH "/usr/share/wallpaper.jpg"
#define WALLPAPERS_PATH "/usr/share/wallpapers"

static char * wallpaper_path;

static struct TTKButton _set = {0};
static struct TTKButton _close = {0};
static struct TTKButton _left = {0};
static struct TTKButton _right = {0};

static list_t * wallpapers = NULL;
static node_t * current_wallpaper = NULL;

static void redraw(void) {

	struct decor_bounds bounds;
	decor_get_bounds(window, &bounds);

	/* Clear to black */
	draw_fill(ctx, rgb(0,0,0));

	/* Calculate fit */
	int max_width = window->width - bounds.width;
	int max_height = window->height - bounds.height;
	/* Calculate the appropriate scaled size to fit the screen. */
	float x = (float)max_width / (float)wallpaper.width;
	float y = (float)max_height / (float)wallpaper.height;

	int nh = (int)(x * (float)wallpaper.height);
	int nw = (int)(y * (float)wallpaper.width);

	/* Scale the wallpaper into the buffer. */
	if (nw <= width) {
		/* Scaled wallpaper is wider, height should match. */
		draw_sprite_scaled(ctx, &wallpaper, bounds.left_width + ((int)max_width - nw) / 2, bounds.top_height, nw+2, max_height);
	} else {
		/* Scaled wallpaper is taller, width should match. */
		draw_sprite_scaled(ctx, &wallpaper, bounds.left_width, bounds.top_height + ((int)max_height - nh) / 2, max_width+2, nh);
	}

	/* Draws the path for the selected wallpaper in white, centered, with a drop shadow */
	int str_width = draw_sdf_string_width(wallpaper_path, 16, SDF_FONT_THIN);
	int center_x_text = (window->width - bounds.width - str_width) / 2;
	draw_sdf_string_stroke(ctx, center_x_text + 1, bounds.top_height + 10 + 1, wallpaper_path, 16, rgba(0,0,0,120), SDF_FONT_THIN, 1.7, 0.5);
	draw_sdf_string(ctx, center_x_text, bounds.top_height + 10, wallpaper_path, 16, rgb(255,255,255), SDF_FONT_THIN);

	/* Draw the buttons */
	ttk_button_draw(ctx, &_set);
	ttk_button_draw(ctx, &_close);
	ttk_button_draw(ctx, &_left);
	ttk_button_draw(ctx, &_right);

	/* Draw window decorations */
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

	_set.title = "Set";
	_set.width = BUTTON_WIDTH;
	_set.height = BUTTON_HEIGHT;
	_set.x = ctx->width - bounds.right_width - BUTTON_WIDTH - BUTTON_PADDING * 2 - BUTTON_HEIGHT;
	_set.y = ctx->height - bounds.bottom_height - BUTTON_HEIGHT - BUTTON_PADDING;

	_close.title = "Close";
	_close.width = BUTTON_WIDTH;
	_close.height = BUTTON_HEIGHT;
	_close.x = ctx->width - bounds.right_width - BUTTON_WIDTH * 2 - BUTTON_PADDING * 3 - BUTTON_HEIGHT;
	_close.y = ctx->height - bounds.bottom_height - BUTTON_HEIGHT - BUTTON_PADDING;

	_left.title = "<";
	_left.width = BUTTON_HEIGHT;
	_left.height = BUTTON_WIDTH;
	_left.x = bounds.left_width + BUTTON_PADDING;
	_left.y = bounds.top_height + (ctx->height - BUTTON_WIDTH) / 2;

	_right.title = ">";
	_right.width = BUTTON_HEIGHT;
	_right.height = BUTTON_WIDTH;
	_right.x = ctx->width - bounds.right_width - BUTTON_HEIGHT - BUTTON_PADDING;
	_right.y = bounds.top_height + (ctx->height - BUTTON_WIDTH) / 2;
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
	if (!button && (_set.hilight || _close.hilight || _left.hilight || _right.hilight)) {
		_set.hilight = 0;
		_close.hilight = 0;
		_left.hilight = 0;
		_right.hilight = 0;
		redraw();
	} else if (button && (button->hilight != hilight)) {
		_set.hilight = 0;
		_close.hilight = 0;
		_left.hilight = 0;
		_right.hilight = 0;
		button->hilight = hilight;
		redraw();
	}
}

void load_wallpaper(void) {
	if (wallpaper.bitmap) free(wallpaper.bitmap);
	wallpaper.bitmap = NULL;
	/* load wallpaper */
	load_sprite(&wallpaper, wallpaper_path);
	/* Ensures we render correctly when scaling */
	wallpaper.alpha = ALPHA_EMBEDDED;
}

void get_default_wallpaper(void) {
	char * home = getenv("HOME");
	if (!home) {
		/* That should not happen... */
		wallpaper_path = strdup(DEFAULT_PATH);
		return;
	}

	char path[512];
	sprintf(path, "%s/.wallpaper.conf", home);
	FILE * conf = fopen(path,"r");
	if (!conf) {
		wallpaper_path = strdup(DEFAULT_PATH);
		return;
	}

	if (conf) {
		char line[1024];
		while (!feof(conf)) {
			fgets(line, 1024, conf);
			char * nl = strchr(line, '\n');
			if (nl) *nl = '\0';
			if (line[0] == ';') {
				continue;
			}
			if (strstr(line, "wallpaper=") == line) {
				wallpaper_path = strdup(line+strlen("wallpaper="));
				break;
			}
		}
		fclose(conf);
	}

}

void set_wallpaper(void) {
	/* get the PID of the destkop file-browser */
	FILE * f = fopen("/var/run/.wallpaper.pid","r");
	if (!f) {
		/* TODO show an error dialog */
		fprintf(stderr, "Failed to read wallpaper PID\n");
		return;
	}
	char data[30];
	fgets(data, 30, f);
	fclose(f);
	int pid = atoi(data);

	/* write the config file */
	char * home = getenv("HOME");
	if (!home) {
		/* That should not happen... */
		fprintf(stderr, "Failed to read HOME envvar\n");
		return;
	}
	char path[512];
	sprintf(path, "%s/.wallpaper.conf", home);
	FILE * conf = fopen(path,"w");
	fprintf(conf,"wallpaper=%s\n", wallpaper_path);
	fprintf(stderr, "Setting wallpaper to %s\n", wallpaper_path);
	fclose(conf);

	/* signal the desktop */
	kill(pid, SIGUSR1);
}

void read_wallpapers(void) {
	wallpapers = list_create();

	/* Open wallpapers directory */
	DIR * dirp = opendir(WALLPAPERS_PATH);

	if (!dirp) {
		return; /* No wallpapers? */
	}

	struct dirent * ent = readdir(dirp);
	while (ent != NULL) {
		if (!strcmp(ent->d_name,".") || !strcmp(ent->d_name,"..")) {
			ent = readdir(dirp);
			continue;
		}
		char tmp[strlen(WALLPAPERS_PATH)+strlen(ent->d_name)+2];
		sprintf(tmp, "%s/%s", WALLPAPERS_PATH, ent->d_name);

		list_insert(wallpapers, strdup(tmp));

		ent = readdir(dirp);
	}
	closedir(dirp);
}

void pick_wallpaper(int dir) {
	if (current_wallpaper) {
		if (dir == 1) {
			current_wallpaper = current_wallpaper->next;
		} else {
			current_wallpaper = current_wallpaper->prev;
		}
	}
	if (!current_wallpaper) {
		if (dir == 1) {
			current_wallpaper = wallpapers->head;
		} else {
			current_wallpaper = wallpapers->tail;
		}
		if (!current_wallpaper) return; /* No wallpapers */
	}

	free(wallpaper_path);
	wallpaper_path = strdup(current_wallpaper->value);
	load_wallpaper();
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

	get_default_wallpaper();
	read_wallpapers();

	yutani_window_move(yctx, window, req_center_x - window->width / 2, req_center_y - window->height / 2);

	yutani_window_advertise_icon(yctx, window, title_str, "wallpaper-picker");

	ctx = init_graphics_yutani_double_buffer(window);
	setup_buttons();
	load_wallpaper();
	redraw();

	struct TTKButton * _down_button = NULL;

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
						if (ke->event.action == KEY_ACTION_DOWN && ke->event.keycode == '\n') {
							playing = 0;
						} else if (ke->event.action == KEY_ACTION_DOWN && ke->event.keycode == KEY_ESCAPE) {
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

								if (me->command == YUTANI_MOUSE_EVENT_DOWN) {
									if (in_button(&_set, me)) {
										set_hilight(&_set, 2);
										_down_button = &_set;
									} else if (in_button(&_close, me)) {
										set_hilight(&_close, 2);
										_down_button = &_close;
									} else if (in_button(&_left, me)) {
										set_hilight(&_left, 2);
										_down_button = &_left;
									} else if (in_button(&_right, me)) {
										set_hilight(&_right, 2);
										_down_button = &_right;
									}
								} else if (me->command == YUTANI_MOUSE_EVENT_RAISE || me->command == YUTANI_MOUSE_EVENT_CLICK) {
									if (_down_button) {
										if (in_button(_down_button, me)) {
											if (_down_button == &_close) {
												playing = 0;
												break;
											} else if (_down_button == &_set) {
												/* Set wallpaper */
												set_wallpaper();
											} else if (_down_button == &_left) {
												/* Previous wallpaper */
												pick_wallpaper(-1);
												redraw();
											} else if (_down_button == &_right) {
												/* Next wallpaper */
												pick_wallpaper(1);
												redraw();
											}
											_down_button->hilight = 0;
										}
									}
									_down_button = NULL;
								}

								if (!me->buttons & YUTANI_MOUSE_BUTTON_LEFT) {
									if (in_button(&_set, me)) {
										set_hilight(&_set, 1);
									} else if (in_button(&_close, me)) {
										set_hilight(&_close, 1);
									} else if (in_button(&_left, me)) {
										set_hilight(&_left, 1);
									} else if (in_button(&_right, me)) {
										set_hilight(&_right, 1);
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
