/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2018 K. Lange
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
#include <syscall.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include <toaru/yutani.h>
#include <toaru/graphics.h>
#include <toaru/hashmap.h>
#include <toaru/spinlock.h>
#include <toaru/sdf.h>
#include <toaru/icon_cache.h>
#include <toaru/menu.h>
#include <kernel/mod/sound.h>

#define PANEL_HEIGHT 28
#define FONT_SIZE 14
#define TIME_LEFT 108
#define DATE_WIDTH 70

#define GRADIENT_HEIGHT 24
#define APP_OFFSET 140
#define TEXT_Y_OFFSET 2
#define ICON_PADDING 2
#define MAX_TEXT_WIDTH 180
#define MIN_TEXT_WIDTH 50

#define HILIGHT_COLOR rgb(142,216,255)
#define FOCUS_COLOR   rgb(255,255,255)
#define TEXT_COLOR    rgb(230,230,230)
#define ICON_COLOR    rgb(230,230,230)

#define GRADIENT_AT(y) premultiply(rgba(72, 167, 255, ((24-(y))*160)/24))

#define ALTTAB_WIDTH  250
#define ALTTAB_HEIGHT 100
#define ALTTAB_BACKGROUND premultiply(rgba(0,0,0,150))
#define ALTTAB_OFFSET 10

#define ALTF2_WIDTH 400
#define ALTF2_HEIGHT 200

#define MAX_WINDOW_COUNT 100

#define TOTAL_CELL_WIDTH (title_width)
#define LEFT_BOUND (width - TIME_LEFT - DATE_WIDTH - ICON_PADDING - widgets_width)

#define WIDGET_WIDTH 24
#define WIDGET_RIGHT (width - TIME_LEFT - DATE_WIDTH)
#define WIDGET_POSITION(i) (WIDGET_RIGHT - WIDGET_WIDTH * (i+1))

static yutani_t * yctx;

static gfx_context_t * ctx = NULL;
static yutani_window_t * panel = NULL;

static gfx_context_t * actx = NULL;
static yutani_window_t * alttab = NULL;

static gfx_context_t * a2ctx = NULL;
static yutani_window_t * alt_f2 = NULL;

static list_t * window_list = NULL;
static volatile int lock = 0;
static volatile int drawlock = 0;

static size_t bg_size;
static char * bg_blob;

static int width;
static int height;

static int widgets_width = 0;
static int widgets_volume_enabled = 0;
static int widgets_network_enabled = 0;

static int network_status = 0;

static sprite_t * sprite_panel;
static sprite_t * sprite_logout;

static sprite_t * sprite_volume_mute;
static sprite_t * sprite_volume_low;
static sprite_t * sprite_volume_med;
static sprite_t * sprite_volume_high;

static sprite_t * sprite_net_active;
static sprite_t * sprite_net_disabled;

struct MenuList * appmenu;
struct MenuList * window_menu;
struct MenuList * logout_menu;
static yutani_wid_t _window_menu_wid = 0;

static int center_x(int x) {
	return (width - x) / 2;
}

static int center_y(int y) {
	return (height - y) / 2;
}

static int center_x_a(int x) {
	return (ALTTAB_WIDTH - x) / 2;
}

static int center_x_a2(int x) {
	return (ALTF2_WIDTH - x) / 2;
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

static void _window_menu_start_move(struct MenuEntry * self) {
	if (!_window_menu_wid)
		return;
	yutani_focus_window(yctx, _window_menu_wid);
	yutani_window_drag_start_wid(yctx, _window_menu_wid);
}

static void _window_menu_start_maximize(struct MenuEntry * self) {
	if (!_window_menu_wid)
		return;
	yutani_special_request_wid(yctx, _window_menu_wid, YUTANI_SPECIAL_REQUEST_MAXIMIZE);
	yutani_focus_window(yctx, _window_menu_wid);
}

static void _window_menu_close(struct MenuEntry * self) {
	if (!_window_menu_wid)
		return;
	yutani_focus_window(yctx, _window_menu_wid);
	yutani_special_request_wid(yctx, _window_menu_wid, YUTANI_SPECIAL_REQUEST_PLEASE_CLOSE);
}

static void window_show_menu(yutani_wid_t wid, int y, int x) {
	if (window_menu->window) return;
	_window_menu_wid = wid;
	menu_show(window_menu, yctx);
	yutani_window_move(yctx, window_menu->window, y, x);
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

static void update_network_status(void) {
	FILE * net = fopen("/proc/netif","r");

	char line[256];

	do {
		memset(line, 0, 256);
		fgets(line, 256, net);
		if (!*line) break;
		if (strstr(line,"no network") != NULL) {
			network_status = 0;
			break;
		} else if (strstr(line,"ip:") != NULL) {
			network_status = 1;
			break;
		}
	} while (1);

	fclose(net);
}

static void show_logout_menu(void) {
	if (!logout_menu->window) {
		menu_show(logout_menu, yctx);
		if (logout_menu->window) {
			yutani_window_move(yctx, logout_menu->window, width - logout_menu->window->width, PANEL_HEIGHT);
		}
	}
}

static void show_app_menu(void) {
	if (!appmenu->window) {
		menu_show(appmenu, yctx);
		if (appmenu->window) {
			yutani_window_move(yctx, appmenu->window, 0, PANEL_HEIGHT);
		}
	}
}

/* Callback for mouse events */
static void panel_check_click(struct yutani_msg_window_mouse_event * evt) {
	if (evt->wid == panel->wid) {
		if (evt->command == YUTANI_MOUSE_EVENT_CLICK) {
			/* Up-down click */
			if (evt->new_x >= width - 24 ) {
				show_logout_menu();
			} else if (evt->new_x < APP_OFFSET) {
				show_app_menu();
			} else if (evt->new_x >= APP_OFFSET && evt->new_x < LEFT_BOUND) {
				for (int i = 0; i < MAX_WINDOW_COUNT; ++i) {
					if (ads_by_l[i] == NULL) break;
					if (evt->new_x >= ads_by_l[i]->left && evt->new_x < ads_by_l[i]->left + TOTAL_CELL_WIDTH) {
						yutani_focus_window(yctx, ads_by_l[i]->wid);
						break;
					}
				}
			}
			int widget = 0;
			if (widgets_network_enabled) {
				if (evt->new_x > WIDGET_POSITION(widget) && evt->new_x < WIDGET_POSITION(widget-1)) {
					/* TODO: Show the network status */
				}
				widget++;
			}
			if (widgets_volume_enabled) {
				if (evt->new_x > WIDGET_POSITION(widget) && evt->new_x < WIDGET_POSITION(widget-1)) {
					/* TODO: Show the volume manager */
				}
				widget++;
			}
		} else if (evt->buttons & YUTANI_MOUSE_BUTTON_RIGHT) {
			if (evt->new_x >= APP_OFFSET && evt->new_x < LEFT_BOUND) {
				for (int i = 0; i < MAX_WINDOW_COUNT; ++i) {
					if (ads_by_l[i] == NULL) break;
					if (evt->new_x >= ads_by_l[i]->left && evt->new_x < ads_by_l[i]->left + TOTAL_CELL_WIDTH) {
						window_show_menu(ads_by_l[i]->wid, ads_by_l[i]->left, PANEL_HEIGHT);
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
				int widget = 0;
				if (widgets_network_enabled) {
					if (evt->new_x > WIDGET_POSITION(widget) && evt->new_x < WIDGET_POSITION(widget-1)) {
						/* Ignore */
					}
					widget++;
				}
				if (widgets_volume_enabled) {
					if (evt->new_x > WIDGET_POSITION(widget) && evt->new_x < WIDGET_POSITION(widget-1)) {
						if (scroll_direction == 1) {
							volume_lower();
						} else if (scroll_direction == -1) {
							volume_raise();
						}
					}
					widget++;
				}
				if (evt->new_x >= APP_OFFSET && evt->new_x < LEFT_BOUND) {
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
	}
}

static char altf2_buffer[1024] = {0};
static unsigned int altf2_collected = 0;

#if 0
static list_t * altf2_apps = NULL;

struct altf2_app {
	char * name;
	sprite_t * icon;
};

static sprite_t * find_icon(char * name) {
	struct {
		char * name;
		char * icon;
	} special[] = {
		{"about", "star"},
		{"help-browser", "help"},
		{"terminal", "utilities-terminal"},
		{NULL,NULL},
	};

	int i = 0;
	while (special[i].name) {
		if (!strcmp(special[i].name, name)) {
			return icon_get_48(special[i].icon);
		}
		i++;
	}

	return icon_get_48(name);
}
#endif

static void close_altf2(void) {
	free(a2ctx->backbuffer);
	free(a2ctx);

	altf2_buffer[0] = 0;
	altf2_collected = 0;

	yutani_close(yctx, alt_f2);
	alt_f2 = NULL;
}

static void redraw_altf2(void) {

#if 0
	if (!altf2_apps) {
		/* initialize */

	}
#endif

	draw_fill(a2ctx, 0);
	draw_rounded_rectangle(a2ctx,0,0, ALTF2_WIDTH, ALTF2_HEIGHT, 10, ALTTAB_BACKGROUND);

	int t = draw_sdf_string_width(altf2_buffer, 18, SDF_FONT_THIN);
	draw_sdf_string(a2ctx, center_x_a2(t), 60, altf2_buffer, 18, rgb(255,255,255), SDF_FONT_THIN);

	flip(a2ctx);
	yutani_flip(yctx, alt_f2);
}

static void redraw_alttab(void) {
	/* Draw the background, right now just a dark semi-transparent box */
	draw_fill(actx, 0);
	draw_rounded_rectangle(actx,0,0, ALTTAB_WIDTH, ALTTAB_HEIGHT, 10, ALTTAB_BACKGROUND);

	if (ads_by_z[new_focused]) {
		struct window_ad * ad = ads_by_z[new_focused];

		sprite_t * icon = icon_get_48(ad->icon);

		/* Draw it, scaled if necessary */
		if (icon->width == 48) {
			draw_sprite(actx, icon, center_x_a(48), ALTTAB_OFFSET);
		} else {
			draw_sprite_scaled(actx, icon, center_x_a(48), ALTTAB_OFFSET, 48, 48);
		}

		int t = draw_sdf_string_width(ad->name, 22, SDF_FONT_THIN);

		draw_sdf_string(actx, center_x_a(t), 12+ALTTAB_OFFSET+40, ad->name, 22, rgb(255,255,255), SDF_FONT_THIN);
	}

	flip(actx);
	yutani_flip(yctx, alttab);
}

static void launch_application_menu(struct MenuEntry * self) {
	struct MenuEntry_Normal * _self = (void *)self;

	if (!strcmp((char *)_self->action,"log-out")) {
		yutani_session_end(yctx);
		_continue = 0;
	} else {
		launch_application((char *)_self->action);
	}
}

static void handle_key_event(struct yutani_msg_key_event * ke) {
	if (alt_f2 && ke->wid == alt_f2->wid) {
		if (ke->event.action == KEY_ACTION_DOWN) {
			if (ke->event.keycode == KEY_ESCAPE) {
				close_altf2();
				return;
			}
			if (ke->event.key == '\b') {
				if (altf2_collected) {
					altf2_buffer[altf2_collected-1] = '\0';
					altf2_collected--;
					redraw_altf2();
				}
				return;
			}
			if (ke->event.key == '\n') {
				/* execute */
				launch_application(altf2_buffer);
				close_altf2();
				return;
			}
			if (!ke->event.key) {
				return;
			}

			/* Try to add it */
			if (altf2_collected < sizeof(altf2_buffer) - 1) {
				altf2_buffer[altf2_collected] = ke->event.key;
				altf2_collected++;
				altf2_buffer[altf2_collected] = 0;
				redraw_altf2();
			}
		}
	}

	if ((ke->event.modifiers & KEY_MOD_LEFT_CTRL) &&
		(ke->event.modifiers & KEY_MOD_LEFT_ALT) &&
		(ke->event.keycode == 't') &&
		(ke->event.action == KEY_ACTION_DOWN)) {

		launch_application("terminal");
		return;
	}

	if ((ke->event.modifiers & KEY_MOD_LEFT_CTRL) &&
		(ke->event.keycode == KEY_F11) &&
		(ke->event.action == KEY_ACTION_DOWN)) {

		fprintf(stderr, "[panel] Toggling visibility.\n");
		toggle_hide_panel();
		return;
	}

	if ((ke->event.modifiers & KEY_MOD_LEFT_ALT) &&
		(ke->event.keycode == KEY_F1) &&
		(ke->event.action == KEY_ACTION_DOWN)) {
		/* show menu */
		show_app_menu();
	}

	if ((ke->event.modifiers & KEY_MOD_LEFT_ALT) &&
		(ke->event.keycode == KEY_F2) &&
		(ke->event.action == KEY_ACTION_DOWN)) {
		/* show menu */
		if (!alt_f2) {
			alt_f2 = yutani_window_create(yctx, ALTF2_WIDTH, ALTF2_HEIGHT);
			yutani_window_move(yctx, alt_f2, center_x(ALTF2_WIDTH), center_y(ALTF2_HEIGHT));
			a2ctx = init_graphics_yutani_double_buffer(alt_f2);
			redraw_altf2();
		}
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

static void redraw(void) {
	spin_lock(&drawlock);

	struct timeval now;
	struct tm * timeinfo;
	char   buffer[80];

	uint32_t txt_color = TEXT_COLOR;
	int t = 0;

	/* Redraw the background */
	memcpy(ctx->backbuffer, bg_blob, bg_size);

	/* Get the current time for the clock */
	gettimeofday(&now, NULL);
	timeinfo = localtime((time_t *)&now.tv_sec);

	/* Hours : Minutes : Seconds */
	strftime(buffer, 80, "%H:%M:%S", timeinfo);
	draw_sdf_string(ctx, width - TIME_LEFT, 3, buffer, 20, txt_color, SDF_FONT_THIN);

	/* Day-of-week */
	strftime(buffer, 80, "%A", timeinfo);
	t = draw_sdf_string_width(buffer, 12, SDF_FONT_THIN);
	t = (DATE_WIDTH - t) / 2;
	draw_sdf_string(ctx, width - TIME_LEFT - DATE_WIDTH + t, 2, buffer, 12, txt_color, SDF_FONT_THIN);

	/* Month Day */
	strftime(buffer, 80, "%h %e", timeinfo);
	t = draw_sdf_string_width(buffer, 12, SDF_FONT_BOLD);
	t = (DATE_WIDTH - t) / 2;
	draw_sdf_string(ctx, width - TIME_LEFT - DATE_WIDTH + t, 12, buffer, 12, txt_color, SDF_FONT_BOLD);

	/* Applications menu */
	draw_sdf_string(ctx, 8, 3, "Applications", 20, appmenu->window ? HILIGHT_COLOR : txt_color, SDF_FONT_THIN);

	/* Draw each widget */
	/* - Volume */
	/* TODO: Get actual volume levels, and cache them somewhere */
	int widget = 0;
	if (widgets_network_enabled) {
		if (network_status == 1) {
			draw_sprite_alpha_paint(ctx, sprite_net_active, WIDGET_POSITION(widget), 0, 1.0, ICON_COLOR);
		} else {
			draw_sprite_alpha_paint(ctx, sprite_net_disabled, WIDGET_POSITION(widget), 0, 1.0, ICON_COLOR);
		}
		widget++;
	}
	if (widgets_volume_enabled) {
		if (volume_level < 10) {
			draw_sprite_alpha_paint(ctx, sprite_volume_mute, WIDGET_POSITION(widget), 0, 1.0, ICON_COLOR);
		} else if (volume_level < 0x547ae147) {
			draw_sprite_alpha_paint(ctx, sprite_volume_low, WIDGET_POSITION(widget), 0, 1.0, ICON_COLOR);
		} else if (volume_level < 0xa8f5c28e) {
			draw_sprite_alpha_paint(ctx, sprite_volume_med, WIDGET_POSITION(widget), 0, 1.0, ICON_COLOR);
		} else {
			draw_sprite_alpha_paint(ctx, sprite_volume_high, WIDGET_POSITION(widget), 0, 1.0, ICON_COLOR);
		}
		widget++;
	}

	/* Now draw the window list */
	int i = 0, j = 0;
	spin_lock(&lock);
	if (window_list) {
		foreach(node, window_list) {
			struct window_ad * ad = node->value;
			char * s = "";
			char tmp_title[50];
			int w = 0;

			if (APP_OFFSET + i + w > LEFT_BOUND) {
				break;
			}

			if (title_width > MIN_TEXT_WIDTH) {

				memset(tmp_title, 0x0, 50);
				int t_l = strlen(ad->name);
				if (t_l > 45) {
					t_l = 45;
				}
				for (int i = 0; i < t_l;  ++i) {
					tmp_title[i] = ad->name[i];
					if (!ad->name[i]) break;
				}

				while (draw_sdf_string_width(tmp_title, 16, SDF_FONT_THIN) > title_width - ICON_PADDING) {
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
			sprite_t * icon = icon_get_48(ad->icon);

			{
				sprite_t * _tmp_s = create_sprite(48, PANEL_HEIGHT-2, ALPHA_EMBEDDED);
				gfx_context_t * _tmp = init_graphics_sprite(_tmp_s);

				draw_fill(_tmp, rgba(0,0,0,0));
				/* Draw it, scaled if necessary */
				if (icon->width == 48) {
					draw_sprite(_tmp, icon, 0, 0);
				} else {
					draw_sprite_scaled(_tmp, icon, 0, 0, 48, 48);
				}

				free(_tmp);
				draw_sprite_alpha(ctx, _tmp_s, APP_OFFSET + i + w - 48 - 2, 0, 0.7);
				sprite_free(_tmp_s);
			}


			{
				sprite_t * _tmp_s = create_sprite(w, PANEL_HEIGHT, ALPHA_EMBEDDED);
				gfx_context_t * _tmp = init_graphics_sprite(_tmp_s);

				draw_fill(_tmp, rgba(0,0,0,0));
				draw_sdf_string(_tmp, 0, 0, s, 16, rgb(0,0,0), SDF_FONT_THIN);
				blur_context_box(_tmp, 4);

				free(_tmp);
				draw_sprite(ctx, _tmp_s, APP_OFFSET + i + 2, TEXT_Y_OFFSET + 2);
				sprite_free(_tmp_s);

			}

			if (title_width > MIN_TEXT_WIDTH) {
				/* Then draw the window title, with appropriate color */
				if (j == focused_app) {
					/* Current hilighted - title should be a light blue */
					draw_sdf_string(ctx, APP_OFFSET + i + 2, TEXT_Y_OFFSET + 2, s, 16, HILIGHT_COLOR, SDF_FONT_THIN);
				} else {
					if (ad->flags & 1) {
						/* Top window should be white */
						draw_sdf_string(ctx, APP_OFFSET + i + 2, TEXT_Y_OFFSET + 2, s, 16, FOCUS_COLOR, SDF_FONT_THIN);
					} else {
						/* Otherwise, off white */
						draw_sdf_string(ctx, APP_OFFSET + i + 2, TEXT_Y_OFFSET + 2, s, 16, txt_color, SDF_FONT_THIN);
					}
				}
			}

			/* XXX This keeps track of how far left each window list item is
			 * so we can map clicks up in the mouse callback. */
			if (j < MAX_WINDOW_COUNT) {
				if (ads_by_l[j]) {
					ads_by_l[j]->left = APP_OFFSET + i;
				}
			}
			j++;
			i += w;
		}
	}
	spin_unlock(&lock);

	/* Draw the logout button; XXX This should probably have some sort of focus hilight */
	draw_sprite_alpha_paint(ctx, sprite_logout, width - 23, 1, 1.0, ICON_COLOR); /* Logout button */

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

static void resize_finish(int xwidth, int xheight) {
	yutani_window_resize_accept(yctx, panel, xwidth, xheight);

	reinit_graphics_yutani(ctx, panel);
	yutani_window_resize_done(yctx, panel);

	width = xwidth;

	/* Draw the background */
	draw_fill(ctx, rgba(0,0,0,0));
	for (int i = 0; i < xwidth; i += sprite_panel->width) {
		draw_sprite(ctx, sprite_panel, i, 0);
	}

	/* Copy the prerendered background so we can redraw it quickly */
	bg_size = panel->width * panel->height * sizeof(uint32_t);
	bg_blob = realloc(bg_blob, bg_size);
	memcpy(bg_blob, ctx->backbuffer, bg_size);

	update_window_list();
	redraw();
}

static void sig_usr2(int sig) {
	yutani_set_stack(yctx, panel, YUTANI_ZORDER_TOP);
	yutani_flip(yctx, panel);
}

int main (int argc, char ** argv) {
	if (argc < 2 || strcmp(argv[1],"--really")) {
		fprintf(stderr,
				"%s: Desktop environment panel / dock\n"
				"\n"
				" Renders the application menu, window list, widgets,\n"
				" alt-tab window switcher, clock, etc.\n"
				" You probably don't want to run this directly - it is\n"
				" started automatically by the session manager.\n", argv[0]);
		return 1;
	}

	/* Connect to window server */
	yctx = yutani_init();

	/* For convenience, store the display size */
	width  = yctx->display_width;
	height = yctx->display_height;

	/* Create the panel window */
	panel = yutani_window_create_flags(yctx, width, PANEL_HEIGHT, YUTANI_WINDOW_FLAG_NO_STEAL_FOCUS);

	/* And move it to the top layer */
	yutani_set_stack(yctx, panel, YUTANI_ZORDER_TOP);

	/* Initialize graphics context against the window */
	ctx = init_graphics_yutani_double_buffer(panel);

	/* Clear it out (the compositor should initialize it cleared anyway */
	draw_fill(ctx, rgba(0,0,0,0));
	flip(ctx);
	yutani_flip(yctx, panel);

	/* Load textures for the background and logout button */
	sprite_panel  = malloc(sizeof(sprite_t));
	sprite_logout = malloc(sizeof(sprite_t));

	load_sprite(sprite_panel,  "/usr/share/panel.bmp");
	sprite_panel->alpha = ALPHA_EMBEDDED;
	load_sprite(sprite_logout, "/usr/share/icons/panel-shutdown.bmp");
	sprite_logout->alpha = ALPHA_FORCE_SLOW_EMBEDDED;

	struct stat stat_tmp;
	if (!stat("/dev/dsp",&stat_tmp)) {
		widgets_volume_enabled = 1;
		widgets_width += WIDGET_WIDTH;
		sprite_volume_mute = malloc(sizeof(sprite_t));
		sprite_volume_low  = malloc(sizeof(sprite_t));
		sprite_volume_med  = malloc(sizeof(sprite_t));
		sprite_volume_high = malloc(sizeof(sprite_t));
		load_sprite(sprite_volume_mute, "/usr/share/icons/24/volume-mute.bmp");
		sprite_volume_mute->alpha = ALPHA_FORCE_SLOW_EMBEDDED;
		load_sprite(sprite_volume_low,  "/usr/share/icons/24/volume-low.bmp");
		sprite_volume_low->alpha = ALPHA_FORCE_SLOW_EMBEDDED;
		load_sprite(sprite_volume_med,  "/usr/share/icons/24/volume-medium.bmp");
		sprite_volume_med->alpha = ALPHA_FORCE_SLOW_EMBEDDED;
		load_sprite(sprite_volume_high, "/usr/share/icons/24/volume-full.bmp");
		sprite_volume_high->alpha = ALPHA_FORCE_SLOW_EMBEDDED;
		/* XXX store current volume */
	}

	{
		widgets_network_enabled = 1;
		widgets_width += WIDGET_WIDTH;
		sprite_net_active = malloc(sizeof(sprite_t));
		load_sprite(sprite_net_active, "/usr/share/icons/24/net-active.bmp");
		sprite_net_active->alpha = ALPHA_FORCE_SLOW_EMBEDDED;
		sprite_net_disabled = malloc(sizeof(sprite_t));
		load_sprite(sprite_net_disabled, "/usr/share/icons/24/net-disconnected.bmp");
		sprite_net_disabled->alpha = ALPHA_FORCE_SLOW_EMBEDDED;
	}

	/* Draw the background */
	for (int i = 0; i < width; i += sprite_panel->width) {
		draw_sprite(ctx, sprite_panel, i, 0);
	}

	/* Copy the prerendered background so we can redraw it quickly */
	bg_size = panel->width * panel->height * sizeof(uint32_t);
	bg_blob = malloc(bg_size);
	memcpy(bg_blob, ctx->backbuffer, bg_size);

	/* Catch SIGINT */
	signal(SIGINT, sig_int);
	signal(SIGUSR2, sig_usr2);

	appmenu = menu_set_get_root(menu_set_from_description("/usr/share/demo.menu", launch_application_menu));

	window_menu = menu_create();
	menu_insert(window_menu, menu_create_normal(NULL, NULL, "Maximize", _window_menu_start_maximize));
	menu_insert(window_menu, menu_create_normal(NULL, NULL, "Move", _window_menu_start_move));
	menu_insert(window_menu, menu_create_separator());
	menu_insert(window_menu, menu_create_normal(NULL, NULL, "Close", _window_menu_close));

	logout_menu = menu_create();
	menu_insert(logout_menu, menu_create_normal("exit", "log-out", "Log Out", launch_application_menu));

	/* Subscribe to window updates */
	yutani_subscribe_windows(yctx);

	/* Ask compositor for window list */
	update_window_list();

	/* Key bindings */

	/* Cltr-Alt-T = launch terminal */
	yutani_key_bind(yctx, 't', KEY_MOD_LEFT_CTRL | KEY_MOD_LEFT_ALT, YUTANI_BIND_STEAL);

	/* Alt+Tab = app switcher*/
	yutani_key_bind(yctx, '\t', KEY_MOD_LEFT_ALT, YUTANI_BIND_STEAL);
	yutani_key_bind(yctx, '\t', KEY_MOD_LEFT_ALT | KEY_MOD_LEFT_SHIFT, YUTANI_BIND_STEAL);

	/* Ctrl-F11 = toggle panel visibility */
	yutani_key_bind(yctx, KEY_F11, KEY_MOD_LEFT_CTRL, YUTANI_BIND_STEAL);

	/* Alt+F1 = show menu */
	yutani_key_bind(yctx, KEY_F1, KEY_MOD_LEFT_ALT, YUTANI_BIND_STEAL);

	/* Alt+F2 = show app runner */
	yutani_key_bind(yctx, KEY_F2, KEY_MOD_LEFT_ALT, YUTANI_BIND_STEAL);

	/* This lets us receive all just-modifier key releases */
	yutani_key_bind(yctx, KEY_LEFT_ALT, 0, YUTANI_BIND_PASSTHROUGH);

	time_t last_tick = 0;

	int fds[1] = {fileno(yctx->sock)};

	while (_continue) {

		int index = syscall_fswait2(1,fds,200);

		if (index == 0) {
			/* Respond to Yutani events */
			yutani_msg_t * m = yutani_poll(yctx);
			while (m) {
				menu_process_event(yctx, m);
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
					case YUTANI_MSG_WELCOME:
						{
							struct yutani_msg_welcome * mw = (void*)m->data;
							width = mw->display_width;
							height = mw->display_height;
							yutani_window_resize(yctx, panel, mw->display_width, PANEL_HEIGHT);
						}
						break;
					case YUTANI_MSG_RESIZE_OFFER:
						{
							struct yutani_msg_window_resize * wr = (void*)m->data;
							resize_finish(wr->width, wr->height);
						}
						break;
					default:
						break;
				}
				free(m);
				m = yutani_poll_async(yctx);
			}
		} else {
			struct timeval now;
			gettimeofday(&now, NULL);
			if (now.tv_sec != last_tick) {
				last_tick = now.tv_sec;
				waitpid(-1, NULL, WNOHANG);
				update_volume_level();
				update_network_status();
				redraw();
			}
		}
	}

	/* Close the panel window */
	yutani_close(yctx, panel);

	/* Stop notifying us of window changes */
	yutani_unsubscribe_windows(yctx);

	return 0;
}

