/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2014 Kevin Lange
 *
 *
 * Yutani Panel
 *
 * Provides a window list and clock as well some simple session management.
 *
 * Future goals:
 * - Applications menu
 * - More session management
 * - Pluggable indicators
 *
 */
#include <stdlib.h>
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include "lib/pthread.h"
#include "lib/yutani.h"
#include "lib/graphics.h"
#include "lib/shmemfonts.h"
#include "lib/hashmap.h"
#include "lib/spinlock.h"
#include "lib/sound.h"

#define PANEL_HEIGHT 28
#define FONT_SIZE 14
#define TIME_LEFT 108
#define DATE_WIDTH 70

#define ICON_SIZE 24
#define GRADIENT_HEIGHT 24
#define APP_OFFSET 140
#define TEXT_Y_OFFSET 18
#define ICON_PADDING 2
#define MAX_TEXT_WIDTH 120
#define MIN_TEXT_WIDTH 50

#define HILIGHT_COLOR rgb(142,216,255)
#define FOCUS_COLOR   rgb(255,255,255)
#define TEXT_COLOR    rgb(230,230,230)

#define GRADIENT_AT(y) premultiply(rgba(72, 167, 255, ((24-(y))*160)/24))

#define ALTTAB_WIDTH  250
#define ALTTAB_HEIGHT 70
#define ALTTAB_BACKGROUND premultiply(rgba(0,0,0,150))
#define ALTTAB_OFFSET 10

#define MAX_WINDOW_COUNT 100

#define TOTAL_CELL_WIDTH (ICON_SIZE + ICON_PADDING * 2 + title_width)
#define LEFT_BOUND (width - TIME_LEFT - DATE_WIDTH - ICON_PADDING - widgets_width)

#define APPMENU_WIDTH  200
#define APPMENU_PAD_RIGHT 1
#define APPMENU_PAD_BOTTOM 1
#define APPMENU_BACKGROUND premultiply(rgba(255,255,255,240))
#define APPMENU_HIGHLIGHT rgb(50,50,200)
#define APPMENU_ITEM_HEIGHT 24

#define WIDGET_WIDTH 24
#define WIDGET_RIGHT (width - TIME_LEFT - DATE_WIDTH)
#define WIDGET_POSITION(i) (WIDGET_RIGHT - WIDGET_WIDTH * (i+1))

static yutani_t * yctx;

static gfx_context_t * ctx = NULL;
static yutani_window_t * panel = NULL;

static gfx_context_t * actx = NULL;
static yutani_window_t * alttab = NULL;

static gfx_context_t * bctx = NULL;
static yutani_window_t * appmenu = NULL;

static int appmenu_item = -1;

static list_t * window_list = NULL;
static volatile int lock = 0;
static volatile int drawlock = 0;

static hashmap_t * icon_cache;

static size_t bg_size;
static char * bg_blob;

static int width;
static int height;

static int widgets_width = 0;
static int widgets_volume_enabled = 0;

static sprite_t * sprite_panel;
static sprite_t * sprite_logout;

static sprite_t * sprite_volume_mute;
static sprite_t * sprite_volume_low;
static sprite_t * sprite_volume_med;
static sprite_t * sprite_volume_high;

static int center_x(int x) {
	return (width - x) / 2;
}

static int center_y(int y) {
	return (height - y) / 2;
}

static int center_x_a(int x) {
	return (ALTTAB_WIDTH - x) / 2;
}

static int center_y_a(int y) {
	return (ALTTAB_HEIGHT - y) / 2;
}

static void redraw(void);

static volatile int _continue = 1;

struct window_ad {
	yutani_wid_t wid;
	uint32_t flags;
	char * name;
	char * icon;
	char * strings;
	int left;
};

typedef struct {
	char * icon;
	char * appname;
	char * title;
} application_t;

static int appmenu_items_count = 0;
static application_t * applications = NULL;

/* Windows, indexed by list order */
static struct window_ad * ads_by_l[MAX_WINDOW_COUNT+1] = {NULL};
/* Windows, indexed by z-order */
static struct window_ad * ads_by_z[MAX_WINDOW_COUNT+1] = {NULL};

static int focused_app = -1;
static int active_window = -1;
static int was_tabbing = 0;
static int new_focused = -1;

static int title_width = 0;

static void toggle_hide_panel(void) {
	static int panel_hidden = 0;

	if (panel_hidden) {
		/* Unhide the panel */
		for (int i = PANEL_HEIGHT-1; i >= 0; i--) {
			yutani_window_move(yctx, panel, 0, -i);
			usleep(10000);
		}
		panel_hidden = 0;
	} else {
		/* Hide the panel */
		for (int i = 1; i <= PANEL_HEIGHT-1; i++) {
			yutani_window_move(yctx, panel, 0, -i);
			usleep(10000);
		}
		panel_hidden = 1;
	}
}

static sprite_t * icon_get(char * name);
static void redraw_appmenu(int item);

/* Handle SIGINT by telling other threads (clock) to shut down */
static void sig_int(int sig) {
	printf("Received shutdown signal in panel!\n");
	_continue = 0;
}

static void launch_application(char * app) {
	if (!fork()) {
		printf("Starting %s\n", app);
		char * args[] = {"/bin/sh", "-c", app, NULL};
		execvp(args[0], args);
		exit(1);
	}
}

/* Update the hover-focus window */
static void set_focused(int i) {
	if (focused_app != i) {
		focused_app = i;
		redraw();
	}
}

#define VOLUME_DEVICE_ID 0
#define VOLUME_KNOB_ID   0
static uint32_t volume_level = 0;
static int mixer = -1;
static void update_volume_level(void) {
	if (mixer == -1) {
		mixer = open("/dev/mixer", O_RDONLY);
	}

	snd_knob_value_t value = {0};
	value.device = VOLUME_DEVICE_ID; /* TODO configure this somewhere */
	value.id     = VOLUME_KNOB_ID;   /* TODO this too */

	ioctl(mixer, SND_MIXER_READ_KNOB, &value);
	volume_level = value.val;
}
static void volume_raise(void) {
	if (volume_level > 0xE0000000) volume_level = 0xF0000000;
	else volume_level += 0x10000000;

	snd_knob_value_t value = {0};
	value.device = VOLUME_DEVICE_ID; /* TODO configure this somewhere */
	value.id     = VOLUME_KNOB_ID;   /* TODO this too */
	value.val    = volume_level;

	ioctl(mixer, SND_MIXER_WRITE_KNOB, &value);
	redraw();
}
static void volume_lower(void) {
	if (volume_level < 0x20000000) volume_level = 0x0;
	else volume_level -= 0x10000000;

	snd_knob_value_t value = {0};
	value.device = VOLUME_DEVICE_ID; /* TODO configure this somewhere */
	value.id     = VOLUME_KNOB_ID;   /* TODO this too */
	value.val    = volume_level;

	ioctl(mixer, SND_MIXER_WRITE_KNOB, &value);
	redraw();
}

/* Callback for mouse events */
static void panel_check_click(struct yutani_msg_window_mouse_event * evt) {
	if (evt->wid == panel->wid) {
		if (evt->command == YUTANI_MOUSE_EVENT_CLICK) {
			/* Up-down click */
			if (evt->new_x >= width - 24 ) {
				yutani_session_end(yctx);
				_continue = 0;
			} else if (evt->new_x < APP_OFFSET) {
				if (!appmenu) {
					appmenu = yutani_window_create(yctx, APPMENU_WIDTH + APPMENU_PAD_RIGHT, APPMENU_ITEM_HEIGHT * appmenu_items_count + APPMENU_PAD_BOTTOM);
					yutani_window_move(yctx, appmenu, 0, PANEL_HEIGHT);
					bctx = init_graphics_yutani_double_buffer(appmenu);
					redraw_appmenu(-1);
					yutani_focus_window(yctx, appmenu->wid);
				} else {
					/* ??? */
				}
			} else if (evt->new_x > WIDGET_POSITION(1) && evt->new_x < WIDGET_POSITION(0)) {
				/* TODO: More generic widget click handling */
				/* TODO: Show the volume manager */
			} else if (evt->new_x >= APP_OFFSET && evt->new_x < LEFT_BOUND) {
				for (int i = 0; i < MAX_WINDOW_COUNT; ++i) {
					if (ads_by_l[i] == NULL) break;
					if (evt->new_x >= ads_by_l[i]->left && evt->new_x < ads_by_l[i]->left + TOTAL_CELL_WIDTH) {
						yutani_focus_window(yctx, ads_by_l[i]->wid);
						break;
					}
				}
			}
		} else if (evt->command == YUTANI_MOUSE_EVENT_MOVE || evt->command == YUTANI_MOUSE_EVENT_ENTER) {
			/* Movement, or mouse entered window */
			if (evt->new_y < PANEL_HEIGHT) {
				for (int i = 0; i < MAX_WINDOW_COUNT; ++i) {
					if (ads_by_l[i] == NULL) {
						set_focused(-1);
						break;
					}
					if (evt->new_x >= ads_by_l[i]->left && evt->new_x < ads_by_l[i]->left + TOTAL_CELL_WIDTH) {
						set_focused(i);
						break;
					}
				}
			} else {
				set_focused(-1);
			}

			int scroll_direction = 0;
			if (evt->buttons & YUTANI_MOUSE_SCROLL_UP) scroll_direction = -1;
			else if (evt->buttons & YUTANI_MOUSE_SCROLL_DOWN) scroll_direction = 1;

			if (scroll_direction) {
				if (evt->new_x > WIDGET_POSITION(1) && evt->new_y < WIDGET_POSITION(0)) {
					if (scroll_direction == 1) {
						volume_lower();
					} else if (scroll_direction == -1) {
						volume_raise();
					}
				} else if (evt->new_x >= APP_OFFSET && evt->new_x < LEFT_BOUND) {
					if (scroll_direction != 0) {
						struct window_ad * last = window_list->tail ? window_list->tail->value : NULL;
						int focus_next = 0;
						foreach(node, window_list) {
							struct window_ad * ad = node->value;
							if (focus_next) {
								yutani_focus_window(yctx, ad->wid);
								return;
							}
							if (ad->flags & 1) {
								if (scroll_direction == -1) {
									yutani_focus_window(yctx, last->wid);
									return;
								}
								if (scroll_direction == 1) {
									focus_next = 1;
								}
							}
							last = ad;
						}
						if (focus_next && window_list->head) {
							struct window_ad * ad = window_list->head->value;
							yutani_focus_window(yctx, ad->wid);
							return;
						}
					}
				}
			}
		} else if (evt->command == YUTANI_MOUSE_EVENT_LEAVE) {
			/* Mouse left panel window */
			set_focused(-1);
		}
	} else {
		if (appmenu && evt->wid == appmenu->wid) {
			/* Do stuff */
			if (evt->command == YUTANI_MOUSE_EVENT_CLICK) {
				if (evt->new_x >= 0 && evt->new_x < appmenu->width && evt->new_y >= 0 && evt->new_y < appmenu->height) {
					int item = evt->new_y / APPMENU_ITEM_HEIGHT;
					launch_application(applications[item].appname);
					yutani_close(yctx, appmenu);
					appmenu = NULL;
					free(bctx->backbuffer);
					free(bctx);
				}
			} else if (evt->command == YUTANI_MOUSE_EVENT_MOVE || evt->command == YUTANI_MOUSE_EVENT_ENTER) {
				if (evt->new_x >= 0 && evt->new_x < appmenu->width && evt->new_y >= 0 && evt->new_y < appmenu->height) {
					int item = evt->new_y / APPMENU_ITEM_HEIGHT;
					if (item != appmenu_item) {
						appmenu_item = item;
						redraw_appmenu(appmenu_item);
					}
				}
			} else if (evt->command == YUTANI_MOUSE_EVENT_LEAVE) {
				if (-1 != appmenu_item) {
					appmenu_item = -1;
					redraw_appmenu(appmenu_item);
				}
			}
		}
	}
}

static void handle_focus_event(struct yutani_msg_window_focus_change * wf) {

	if (appmenu && wf->wid == appmenu->wid  && wf->focused == 0) {
		/* Close */
		yutani_close(yctx, appmenu);
		appmenu = NULL;
		free(bctx->backbuffer);
		free(bctx);
	}

}

static void redraw_alttab(void) {
	/* Draw the background, right now just a dark semi-transparent box */
	draw_fill(actx, ALTTAB_BACKGROUND);

	if (ads_by_z[new_focused]) {
		struct window_ad * ad = ads_by_z[new_focused];

		sprite_t * icon = icon_get(ad->icon);

		/* Draw it, scaled if necessary */
		if (icon->width == 24) {
			draw_sprite(actx, icon, center_x_a(24), ALTTAB_OFFSET);
		} else {
			draw_sprite_scaled(actx, icon, center_x_a(24), ALTTAB_OFFSET, 24, 24);
		}

		set_font_face(FONT_SANS_SERIF_BOLD);
		set_font_size(14);
		int t = draw_string_width(ad->name);

		draw_string(actx, center_x_a(t), 24+ALTTAB_OFFSET+16, rgb(255,255,255), ad->name);
	}

	flip(actx);
	yutani_flip(yctx, alttab);
}

static void handle_key_event(struct yutani_msg_key_event * ke) {
	if ((ke->event.modifiers & KEY_MOD_LEFT_CTRL) &&
		(ke->event.modifiers & KEY_MOD_LEFT_ALT) &&
		(ke->event.keycode == 't') &&
		(ke->event.action == KEY_ACTION_DOWN)) {

		launch_application("terminal");
	}

	if ((ke->event.modifiers & KEY_MOD_LEFT_CTRL) &&
		(ke->event.keycode == KEY_F11) &&
		(ke->event.action == KEY_ACTION_DOWN)) {
		fprintf(stderr, "[panel] Toggling visibility.\n");
		toggle_hide_panel();
	}

	if ((was_tabbing) && (ke->event.keycode == 0 || ke->event.keycode == KEY_LEFT_ALT) &&
		(ke->event.modifiers == 0) && (ke->event.action == KEY_ACTION_UP)) {

		fprintf(stderr, "[panel] Stopping focus new_focused = %d\n", new_focused);

		struct window_ad * ad = ads_by_z[new_focused];

		if (!ad) return;

		yutani_focus_window(yctx, ad->wid);
		was_tabbing = 0;
		new_focused = -1;

		free(actx->backbuffer);
		free(actx);

		yutani_close(yctx, alttab);

		return;
	}

	if ((ke->event.modifiers & KEY_MOD_LEFT_ALT) &&
		(ke->event.keycode == '\t') &&
		(ke->event.action == KEY_ACTION_DOWN)) {

		int direction = (ke->event.modifiers & KEY_MOD_LEFT_SHIFT) ? 1 : -1;

		if (window_list->length < 1) return;

		if (was_tabbing) {
			new_focused = new_focused + direction;
		} else {
			new_focused = active_window + direction;
			/* Create tab window */
			alttab = yutani_window_create(yctx, ALTTAB_WIDTH, ALTTAB_HEIGHT);

			/* Center window */
			yutani_window_move(yctx, alttab, center_x(ALTTAB_WIDTH), center_y(ALTTAB_HEIGHT));

			/* Initialize graphics context against the window */
			actx = init_graphics_yutani_double_buffer(alttab);
		}

		if (new_focused < 0) {
			new_focused = 0;
			for (int i = 0; i < MAX_WINDOW_COUNT; i++) {
				if (ads_by_z[i+1] == NULL) {
					new_focused = i;
					break;
				}
			}
		} else if (ads_by_z[new_focused] == NULL) {
			new_focused = 0;
		}

		was_tabbing = 1;

		redraw_alttab();
	}

}

/* Default search paths for icons, in order of preference */
static char * icon_directories[] = {
	"/usr/share/icons/24",
	"/usr/share/icons/external/24",
	"/usr/share/icons/48",
	"/usr/share/icons/external/48",
	"/usr/share/icons",
	"/usr/share/icons/external",
	NULL
};

/*
 * Get an icon from the cache, or if it is not in the cache,
 * load it - or cache the generic icon if we can not find an
 * appropriate matching icon on the filesystem.
 */
static sprite_t * icon_get(char * name) {

	if (!strcmp(name,"")) {
		/* If a window doesn't have an icon set, return the generic icon */
		return hashmap_get(icon_cache, "generic");
	}

	/* Check the icon cache */
	sprite_t * icon = hashmap_get(icon_cache, name);

	if (!icon) {
		/* We don't have an icon cached for this identifier, try search */
		int i = 0;
		char path[100];
		while (icon_directories[i]) {
			/* Check each path... */
			sprintf(path, "%s/%s.png", icon_directories[i], name);
			if (access(path, R_OK) == 0) {
				/* And if we find one, cache it */
				icon = malloc(sizeof(sprite_t));
				load_sprite_png(icon, path);
				hashmap_set(icon_cache, name, icon);
				return icon;
			}
			i++;
		}

		/* If we've exhausted our search paths, just return the generic icon */
		icon = hashmap_get(icon_cache, "generic");
		hashmap_set(icon_cache, name, icon);
	}

	/* We have an icon, return it */
	return icon;
}

static void read_applications(FILE * f) {
	if (!f) {
		/* No applications? */
		applications = malloc(sizeof(application_t));
		applications[0].icon = NULL;
		return;
	}
	char line[2048];

	int count = 0;

	while (fgets(line, 2048, f) != NULL) {
		if (strstr(line, "#") == line) continue;

		char * icon = line;
		char * name = strstr(icon,","); name++;
		char * title = strstr(name, ","); title++;

		if (!name || !title) {
			continue; /* invalid */
		}

		count++;
	}

	fseek(f, 0, SEEK_SET);
	applications = malloc(sizeof(application_t) * (count + 1));
	memset(&applications[count], 0x00, sizeof(application_t));

	appmenu_items_count = count;

	int i = 0;
	while (fgets(line, 2048, f) != NULL) {
		if (strstr(line, "#") == line) continue;

		char * icon = line;
		char * name = strstr(icon,","); name++;
		char * title = strstr(name, ","); title++;

		if (!name || !title) {
			continue; /* invalid */
		}

		name[-1] = '\0';
		title[-1] = '\0';

		char * tmp = strstr(title, "\n");
		if (tmp) *tmp = '\0';

		applications[i].icon = strdup(icon);
		applications[i].appname = strdup(name);
		applications[i].title = strdup(title);

		i++;
	}

	fclose(f);
}

static void redraw_appmenu(int item) {
	draw_fill(bctx, APPMENU_BACKGROUND);
	if (item != -1) {
		for (int i = 0; i < APPMENU_ITEM_HEIGHT; ++i) {
			draw_line(bctx, 0, APPMENU_WIDTH, APPMENU_ITEM_HEIGHT * item + i, APPMENU_ITEM_HEIGHT * item + i, APPMENU_HIGHLIGHT);
		}
	}
	draw_line(bctx, APPMENU_WIDTH, APPMENU_WIDTH, 0, APPMENU_ITEM_HEIGHT * appmenu_items_count, rgb(0,0,0));
	draw_line(bctx, 0, APPMENU_WIDTH, APPMENU_ITEM_HEIGHT * appmenu_items_count, APPMENU_ITEM_HEIGHT * appmenu_items_count, rgb(0,0,0));
	spin_lock(&drawlock);
	for (int i = 0; i < appmenu_items_count; ++i) {
		set_font_face(FONT_SANS_SERIF);
		set_font_size(12);

		sprite_t * icon = icon_get(applications[i].icon);

		/* Draw it, scaled if necessary */
		if (icon->width == 24) {
			draw_sprite(bctx, icon, 2, APPMENU_ITEM_HEIGHT * i);
		} else {
			draw_sprite_scaled(bctx, icon, 2, APPMENU_ITEM_HEIGHT * i, 24, 24);
		}

		uint32_t color = (i == item) ? rgb(255,255,255) : rgb(0,0,0);

		draw_string(bctx, 30, 18 + APPMENU_ITEM_HEIGHT * i, color, applications[i].title);
	}
	spin_unlock(&drawlock);
	flip(bctx);
	yutani_flip(yctx, appmenu);
}

static void redraw(void) {
	spin_lock(&drawlock);

	struct timeval now;
	int last = 0;
	struct tm * timeinfo;
	char   buffer[80];

	uint32_t txt_color = TEXT_COLOR;
	int t = 0;

	/* Redraw the background */
	memcpy(ctx->backbuffer, bg_blob, bg_size);

	/* Get the current time for the clock */
	gettimeofday(&now, NULL);
	last = now.tv_sec;
	timeinfo = localtime((time_t *)&now.tv_sec);

	/* Hours : Minutes : Seconds */
	strftime(buffer, 80, "%H:%M:%S", timeinfo);
	set_font_face(FONT_SANS_SERIF_BOLD);
	set_font_size(16);
	draw_string(ctx, width - TIME_LEFT, 19, txt_color, buffer);

	/* Day-of-week */
	strftime(buffer, 80, "%A", timeinfo);
	set_font_face(FONT_SANS_SERIF);
	set_font_size(9);
	t = draw_string_width(buffer);
	t = (DATE_WIDTH - t) / 2;
	draw_string(ctx, width - TIME_LEFT - DATE_WIDTH + t, 11, txt_color, buffer);

	/* Month Day */
	strftime(buffer, 80, "%h %e", timeinfo);
	set_font_face(FONT_SANS_SERIF_BOLD);
	set_font_size(9);
	t = draw_string_width(buffer);
	t = (DATE_WIDTH - t) / 2;
	draw_string(ctx, width - TIME_LEFT - DATE_WIDTH + t, 21, txt_color, buffer);

	/* Applications menu */
	set_font_face(FONT_SANS_SERIF_BOLD);
	set_font_size(14);
	draw_string(ctx, 10, 18, appmenu ? HILIGHT_COLOR : txt_color, "Applications");

	/* Draw each widget */
	/* - Volume */
	/* TODO: Get actual volume levels, and cache them somewhere */
	if (widgets_volume_enabled) {
		if (volume_level < 10) {
			draw_sprite(ctx, sprite_volume_mute, WIDGET_POSITION(0), 0);
		} else if (volume_level < 0x547ae147) {
			draw_sprite(ctx, sprite_volume_low, WIDGET_POSITION(0), 0);
		} else if (volume_level < 0xa8f5c28e) {
			draw_sprite(ctx, sprite_volume_med, WIDGET_POSITION(0), 0);
		} else {
			draw_sprite(ctx, sprite_volume_high, WIDGET_POSITION(0), 0);
		}
	}

	/* Now draw the window list */
	int i = 0, j = 0;
	spin_lock(&lock);
	if (window_list) {
		foreach(node, window_list) {
			struct window_ad * ad = node->value;
			char * s = "";
			char tmp_title[50];
			int w = ICON_SIZE + ICON_PADDING * 2;

			if (APP_OFFSET + i + w > LEFT_BOUND) {
				break;
			}

			set_font_face(FONT_SANS_SERIF);
			set_font_size(13);

			if (title_width > MIN_TEXT_WIDTH) {

				memset(tmp_title, 0x0, 50);
				int t_l = strlen(ad->name);
				if (t_l > 45) {
					t_l = 45;
				}
				strncpy(tmp_title, ad->name, t_l);

				while (draw_string_width(tmp_title) > title_width - ICON_PADDING) {
					t_l--;
					tmp_title[t_l] = '.';
					tmp_title[t_l+1] = '.';
					tmp_title[t_l+2] = '.';
					tmp_title[t_l+3] = '\0';
				}
				w += title_width;

				s = tmp_title;
			}

			/* Hilight the focused window */
			if (ad->flags & 1) {
				/* This is the focused window */
				for (int y = 0; y < GRADIENT_HEIGHT; ++y) {
					for (int x = APP_OFFSET + i; x < APP_OFFSET + i + w; ++x) {
						GFX(ctx, x, y) = alpha_blend_rgba(GFX(ctx, x, y), GRADIENT_AT(y));
					}
				}
			}

			/* Get the icon for this window */
			sprite_t * icon = icon_get(ad->icon);

			/* Draw it, scaled if necessary */
			if (icon->width == ICON_SIZE) {
				draw_sprite(ctx, icon, APP_OFFSET + i + ICON_PADDING, 0);
			} else {
				draw_sprite_scaled(ctx, icon, APP_OFFSET + i + ICON_PADDING, 0, ICON_SIZE, ICON_SIZE);
			}

			if (title_width > MIN_TEXT_WIDTH) {
				/* Then draw the window title, with appropriate color */
				if (j == focused_app) {
					/* Current hilighted - title should be a light blue */
					draw_string(ctx, APP_OFFSET + i + ICON_SIZE + ICON_PADDING * 2, TEXT_Y_OFFSET, HILIGHT_COLOR, s);
				} else {
					if (ad->flags & 1) {
						/* Top window should be white */
						draw_string(ctx, APP_OFFSET + i + ICON_SIZE + ICON_PADDING * 2, TEXT_Y_OFFSET, FOCUS_COLOR, s);
					} else {
						/* Otherwise, off white */
						draw_string(ctx, APP_OFFSET + i + ICON_SIZE + ICON_PADDING * 2, TEXT_Y_OFFSET, txt_color, s);
					}
				}
			}

			/* XXX This keeps track of how far left each window list item is
			 * so we can map clicks up in the mouse callback. */
			if (j < MAX_WINDOW_COUNT) {
				ads_by_l[j]->left = APP_OFFSET + i;
			}
			j++;
			i += w;
		}
	}
	spin_unlock(&lock);

	/* Draw the logout button; XXX This should probably have some sort of focus hilight */
	draw_sprite(ctx, sprite_logout, width - 23, 1); /* Logout button */

	/* Flip */
	flip(ctx);
	yutani_flip(yctx, panel);

	spin_unlock(&drawlock);
}

static void update_window_list(void) {
	yutani_query_windows(yctx);

	list_t * new_window_list = list_create();

	int i = 0;
	while (1) {
		/* We wait for a series of WINDOW_ADVERTISE messsages */
		yutani_msg_t * m = yutani_wait_for(yctx, YUTANI_MSG_WINDOW_ADVERTISE);
		struct yutani_msg_window_advertise * wa = (void*)m->data;

		if (wa->size == 0) {
			/* A sentinal at the end will have a size of 0 */
			free(m);
			break;
		}

		/* Store each window advertisement */
		struct window_ad * ad = malloc(sizeof(struct window_ad));

		char * s = malloc(wa->size);
		memcpy(s, wa->strings, wa->size);
		ad->name = &s[wa->offsets[0]];
		ad->icon = &s[wa->offsets[1]];
		ad->strings = s;
		ad->flags = wa->flags;
		ad->wid = wa->wid;

		ads_by_z[i] = ad;
		i++;
		ads_by_z[i] = NULL;

		node_t * next = NULL;

		/* And insert it, ordered by wid, into the window list */
		foreach(node, new_window_list) {
			struct window_ad * n = node->value;

			if (n->wid > ad->wid) {
				next = node;
				break;
			}
		}

		if (next) {
			list_insert_before(new_window_list, next, ad);
		} else {
			list_insert(new_window_list, ad);
		}
		free(m);
	}
	active_window = i-1;

	i = 0;
	/*
	 * Update each of the wid entries in our array so we can map
	 * clicks to window focus events for each window
	 */
	foreach(node, new_window_list) {
		struct window_ad * ad = node->value;
		if (i < MAX_WINDOW_COUNT) {
			ads_by_l[i] = ad;
			ads_by_l[i+1] = NULL;
		}
		i++;
	}

	/* Then free up the old list and replace it with the new list */
	spin_lock(&lock);

	if (new_window_list->length) {
		int tmp = LEFT_BOUND;
		tmp -= APP_OFFSET;
		tmp -= new_window_list->length * (ICON_SIZE + ICON_PADDING * 2);
		if (tmp < 0) {
			title_width = 0;
		} else {
			title_width = tmp / new_window_list->length;
			if (title_width > MAX_TEXT_WIDTH) {
				title_width = MAX_TEXT_WIDTH;
			}
			if (title_width < MIN_TEXT_WIDTH) {
				title_width = 0;
			}
		}
	} else {
		title_width = 0;
	}
	if (window_list) {
		foreach(node, window_list) {
			struct window_ad * ad = (void*)node->value;
			free(ad->strings);
			free(ad);
		}
		list_free(window_list);
		free(window_list);
	}
	window_list = new_window_list;
	spin_unlock(&lock);

	/* And redraw the panel */
	redraw();
}

static void * clock_thread(void * garbage) {
	/*
	 * This thread just calls redraw every so often so the clock
	 * continues to tick. We really shouldn't need this,
	 * but our current environment doens't provide timeouts,
	 * so we can't just bail out of a yutani poll and redraw...
	 */
	while (_continue) {
		waitpid(-1, NULL, WNOHANG);
		update_volume_level();
		redraw();
		usleep(500000);
	}
}

int main (int argc, char ** argv) {
	/* Connect to window server */
	yctx = yutani_init();

	/* For convenience, store the display size */
	width  = yctx->display_width;
	height = yctx->display_height;

	/* Initialize fonts. */
	init_shmemfonts();
	set_font_size(14);

	/* Create the panel window */
	panel = yutani_window_create(yctx, width, PANEL_HEIGHT);

	/* And move it to the top layer */
	yutani_set_stack(yctx, panel, YUTANI_ZORDER_TOP);

	/* Initialize graphics context against the window */
	ctx = init_graphics_yutani_double_buffer(panel);

	/* Clear it out (the compositor should initialize it cleared anyway */
	draw_fill(ctx, rgba(0,0,0,0));
	flip(ctx);
	yutani_flip(yctx, panel);

	/* Initialize hashmap for icon cache */
	icon_cache = hashmap_create(10);

	{
		char f_name[256];
		sprintf(f_name, "%s/.menu.desktop", getenv("HOME"));
		FILE * f = fopen(f_name, "r");
		if (!f) {
			f = fopen("/etc/menu.desktop", "r");
		}
		read_applications(f);
	}

	/* Preload some common icons */
	{ /* Generic fallback icon */
		sprite_t * app_icon = malloc(sizeof(sprite_t));
		load_sprite_png(app_icon, "/usr/share/icons/24/applications-generic.png");
		hashmap_set(icon_cache, "generic", app_icon);
	}

	{ /* Terminal */
		sprite_t * app_icon = malloc(sizeof(sprite_t));
		load_sprite_png(app_icon, "/usr/share/icons/24/utilities-terminal.png");
		hashmap_set(icon_cache, "utilities-terminal", app_icon);
	}

	{ /* Draw! icon */
		sprite_t * app_icon = malloc(sizeof(sprite_t));
		load_sprite_png(app_icon, "/usr/share/icons/24/applications-painting.png");
		hashmap_set(icon_cache, "applications-painting", app_icon);
	}

	/* Load textures for the background and logout button */
	sprite_panel  = malloc(sizeof(sprite_t));
	sprite_logout = malloc(sizeof(sprite_t));

	load_sprite_png(sprite_panel,  "/usr/share/panel.png");
	load_sprite_png(sprite_logout, "/usr/share/icons/panel-shutdown.png");

	struct stat stat_tmp;
	if (!stat("/dev/dsp",&stat_tmp)) {
		widgets_volume_enabled = 1;
		widgets_width += WIDGET_WIDTH;
		sprite_volume_mute = malloc(sizeof(sprite_t));
		sprite_volume_low  = malloc(sizeof(sprite_t));
		sprite_volume_med  = malloc(sizeof(sprite_t));
		sprite_volume_high = malloc(sizeof(sprite_t));
		load_sprite_png(sprite_volume_mute, "/usr/share/icons/24/volume-mute.png");
		load_sprite_png(sprite_volume_low,  "/usr/share/icons/24/volume-low.png");
		load_sprite_png(sprite_volume_med,  "/usr/share/icons/24/volume-medium.png");
		load_sprite_png(sprite_volume_high, "/usr/share/icons/24/volume-full.png");
		/* XXX store current volume */
	}

	/* Draw the background */
	for (uint32_t i = 0; i < width; i += sprite_panel->width) {
		draw_sprite(ctx, sprite_panel, i, 0);
	}

	/* Copy the prerendered background so we can redraw it quickly */
	bg_size = panel->width * panel->height * sizeof(uint32_t);
	bg_blob = malloc(bg_size);
	memcpy(bg_blob, ctx->backbuffer, bg_size);

	/* Catch SIGINT */
	signal(SIGINT, sig_int);

	/* Start clock thread XXX need timeouts in yutani calls */
	pthread_t _clock_thread;
	pthread_create(&_clock_thread, NULL, clock_thread, NULL);

	/* Subscribe to window updates */
	yutani_subscribe_windows(yctx);

	/* Ask compositor for window list */
	update_window_list();

	/* Key bindings */

	/* Launch terminal */
	yutani_key_bind(yctx, 't', KEY_MOD_LEFT_CTRL | KEY_MOD_LEFT_ALT, YUTANI_BIND_STEAL);

	/* Alt+Tab */
	yutani_key_bind(yctx, '\t', KEY_MOD_LEFT_ALT, YUTANI_BIND_STEAL);
	yutani_key_bind(yctx, '\t', KEY_MOD_LEFT_ALT | KEY_MOD_LEFT_SHIFT, YUTANI_BIND_STEAL);

	yutani_key_bind(yctx, KEY_F11, KEY_MOD_LEFT_CTRL, YUTANI_BIND_STEAL);

	/* This lets us receive all just-modifier key releases */
	yutani_key_bind(yctx, KEY_LEFT_ALT, 0, YUTANI_BIND_PASSTHROUGH);

	while (_continue) {
		/* Respond to Yutani events */
		yutani_msg_t * m = yutani_poll(yctx);
		if (m) {
			switch (m->type) {
				/* New window information is available */
				case YUTANI_MSG_NOTIFY:
					update_window_list();
					break;
				/* Mouse movement / click */
				case YUTANI_MSG_WINDOW_MOUSE_EVENT:
					panel_check_click((struct yutani_msg_window_mouse_event *)m->data);
					break;
				case YUTANI_MSG_KEY_EVENT:
					handle_key_event((struct yutani_msg_key_event *)m->data);
					break;
				case YUTANI_MSG_WINDOW_FOCUS_CHANGE:
					handle_focus_event((struct yutani_msg_window_focus_change *)m->data);
					break;
				default:
					break;
			}
			free(m);
		}
	}

	/* Close the panel window */
	yutani_close(yctx, panel);

	/* Stop notifying us of window changes */
	yutani_unsubscribe_windows(yctx);

	return 0;
}
