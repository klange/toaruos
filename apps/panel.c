/**
 * @file apps/panel.c
 * @brief Panel with widgets. Main desktop interface.
 *
 * Provides the panel shown at the top of the screen, which
 * presents application windows, useful widgets, and a menu
 * for launching new apps.
 *
 * Also provides Alt-Tab app switching and a few other goodies.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2021 K. Lange
 */
#include <stdlib.h>
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <dirent.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/fswait.h>
#include <sys/socket.h>
#include <sys/shm.h>
#include <net/if.h>
#include <arpa/inet.h>

#include <toaru/yutani.h>
#include <toaru/yutani-internal.h>
#include <toaru/graphics.h>
#include <toaru/hashmap.h>
#include <toaru/spinlock.h>
#include <toaru/icon_cache.h>
#include <toaru/menu.h>
#include <toaru/text.h>
#include <kernel/mod/sound.h>

#define PANEL_HEIGHT 36
#define DROPDOWN_OFFSET 34
#define FONT_SIZE 14
#define TIME_LEFT 116
#define X_PAD 4
#define Y_PAD 4
#define ICON_Y_PAD 5

#define GRADIENT_HEIGHT 24
#define APP_OFFSET 140
#define TEXT_Y_OFFSET 6
#define ICON_PADDING 2
#define MAX_TEXT_WIDTH 180
#define MIN_TEXT_WIDTH 50

#define HILIGHT_COLOR rgb(142,216,255)
#define FOCUS_COLOR   rgb(255,255,255)
#define TEXT_COLOR    rgb(230,230,230)
#define ICON_COLOR    rgb(230,230,230)
#define SPECIAL_COLOR rgb(93,163,236)

#define GRADIENT_AT(y) premultiply(rgba(72, 167, 255, ((24-(y))*160)/24))

#define ALTTAB_WIDTH  250
#define ALTTAB_HEIGHT 200
#define ALTTAB_BACKGROUND premultiply(rgba(0,0,0,150))
#define ALTTAB_OFFSET 10
#define ALTTAB_WIN_SIZE 140

#define ALTF2_WIDTH 400
#define ALTF2_HEIGHT 200

#define MAX_WINDOW_COUNT 100

#define TOTAL_CELL_WIDTH (title_width)
#define LEFT_BOUND (width - TIME_LEFT - date_widget_width - ICON_PADDING - widgets_width)

#define WIDGET_WIDTH 24
#define WIDGET_RIGHT (width - TIME_LEFT - date_widget_width)
#define WIDGET_POSITION(i) (WIDGET_RIGHT - WIDGET_WIDTH * (i+1))

#define LOGOUT_WIDTH 36

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

static struct TT_Font * font = NULL;
static struct TT_Font * font_bold = NULL;
static struct TT_Font * font_mono = NULL;
static struct TT_Font * font_mono_bold = NULL;

static int widgets_width = 0;
static int widgets_volume_enabled = 0;
static int widgets_network_enabled = 0;
static int widgets_weather_enabled = 0;

static int date_widget_width = 92;

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
struct MenuList * netstat;
struct MenuList * calmenu;
struct MenuList * clockmenu;
struct MenuList * weather;
struct MenuList * volume_menu;
static yutani_wid_t _window_menu_wid = 0;

static int _close_enough(struct yutani_msg_window_mouse_event * me) {
	if (me->command == YUTANI_MOUSE_EVENT_RAISE && sqrt(pow(me->new_x - me->old_x, 2) + pow(me->new_y - me->old_y, 2)) < 10) {
		return 1;
	}
	return 0;
}


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
	uint32_t bufid;
	uint32_t width;
	uint32_t height;
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
			usleep(3000);
		}
		panel_hidden = 0;
	} else {
		/* Hide the panel */
		for (int i = 1; i <= PANEL_HEIGHT-1; i++) {
			yutani_window_move(yctx, panel, 0, -i);
			usleep(3000);
		}
		panel_hidden = 1;
	}
}

/* Handle SIGINT by telling other threads (clock) to shut down */
static void sig_int(int sig) {
	printf("Received shutdown signal in panel!\n");
	_continue = 0;
	signal(SIGINT, sig_int);
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
	menu_prepare(window_menu, yctx);
	if (window_menu->window) {
		yutani_window_move(yctx, window_menu->window, y, x);
		yutani_flip(yctx, window_menu->window);
	}
}


#define VOLUME_DEVICE_ID 0
#define VOLUME_KNOB_ID   0
static long volume_level = 0;
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
static void set_volume(void) {
	snd_knob_value_t value = {0};
	value.device = VOLUME_DEVICE_ID; /* TODO configure this somewhere */
	value.id     = VOLUME_KNOB_ID;   /* TODO this too */
	value.val    = volume_level;

	ioctl(mixer, SND_MIXER_WRITE_KNOB, &value);
	redraw();
}
static void volume_raise(void) {
	volume_level += 0x10000000;
	if (volume_level > 0xF0000000) volume_level = 0xFC000000;
	set_volume();
}
static void volume_lower(void) {
	volume_level -= 0x10000000;
	if (volume_level < 0x0) volume_level = 0x0;
	set_volume();
}

#define VOLUME_SLIDER_LEFT_PAD  38
#define VOLUME_SLIDER_RIGHT_PAD  14
#define VOLUME_SLIDER_PAD (VOLUME_SLIDER_LEFT_PAD + VOLUME_SLIDER_RIGHT_PAD)
#define VOLUME_SLIDER_VERT_PAD   10
#define VOLUME_SLIDER_BALL_RADIUS 8

struct SliderStuff {
	int level;
	uint32_t on;
	uint32_t off;
};

uint32_t volume_pattern(int32_t x, int32_t y, double alpha, void * extra) {
	struct SliderStuff * stuff = extra;
	if (alpha > 1.0) alpha = 1.0;
	if (alpha < 0.0) alpha = 0.0;
	uint32_t color = stuff->off;
	if (x < stuff->level + VOLUME_SLIDER_LEFT_PAD) {
		color = stuff->on;
	}
	color |= rgba(0,0,0,alpha*255);
	return premultiply(color);
}

void _menu_draw_MenuEntry_Slider(gfx_context_t * ctx, struct MenuEntry * self, int offset) {
	self->offset = offset;

	draw_sprite_alpha_paint(ctx, sprite_volume_high, 4, offset, 1.0, rgb(0,0,0));

	struct SliderStuff stuff;
	stuff.level = (ctx->width - VOLUME_SLIDER_PAD) * (float)volume_level / (float)0xFC000000;
	stuff.on  = rgba(0,120,220,0);
	stuff.off = rgba(140,140,140,0);
	draw_rounded_rectangle_pattern(ctx,
		/* x */ VOLUME_SLIDER_LEFT_PAD - 4,
		/* y */ offset + VOLUME_SLIDER_VERT_PAD - 1,
		/* w */ ctx->width - VOLUME_SLIDER_PAD + 8,
		/* h */ self->height - 2 * VOLUME_SLIDER_VERT_PAD + 2, 6, volume_pattern, &stuff);
	stuff.on  = rgba(40,160,255,0);
	stuff.off = rgba(200,200,200,0);
	draw_rounded_rectangle_pattern(ctx,
		/* x */ VOLUME_SLIDER_LEFT_PAD - 3,
		/* y */ offset + VOLUME_SLIDER_VERT_PAD,
		/* w */ ctx->width - VOLUME_SLIDER_PAD + 6,
		/* h */ self->height - 2 * VOLUME_SLIDER_VERT_PAD, 5, volume_pattern, &stuff);

	draw_rounded_rectangle(ctx,
		/* x */ stuff.level - VOLUME_SLIDER_BALL_RADIUS + VOLUME_SLIDER_LEFT_PAD,
		/* y */ offset + 12 - VOLUME_SLIDER_BALL_RADIUS,
		/* w */ VOLUME_SLIDER_BALL_RADIUS * 2,
		/* h */ VOLUME_SLIDER_BALL_RADIUS * 2, VOLUME_SLIDER_BALL_RADIUS, rgb(140,140,140));
	draw_rounded_rectangle(ctx,
		/* x */ stuff.level - VOLUME_SLIDER_BALL_RADIUS + 1 + VOLUME_SLIDER_LEFT_PAD,
		/* y */ offset + 12 - VOLUME_SLIDER_BALL_RADIUS + 1,
		/* w */ VOLUME_SLIDER_BALL_RADIUS * 2 - 2,
		/* h */ VOLUME_SLIDER_BALL_RADIUS * 2 - 2, VOLUME_SLIDER_BALL_RADIUS - 1, rgb(220,220,220));
}

int _menu_mouse_MenuEntry_Slider(struct MenuEntry * self, struct yutani_msg_window_mouse_event * event) {
	if (event->buttons & YUTANI_MOUSE_BUTTON_LEFT) {
		/* Figure out where it is */
		float level = (float)(event->new_x - VOLUME_SLIDER_LEFT_PAD) / (float)(self->width - VOLUME_SLIDER_PAD);
		if (level >= 1.0) level = 1.0;
		if (level <= 0.0) level = 0.0;
		if (volume_level != level * 0xFC000000) {
			volume_level = level * 0xFC000000;
			set_volume();
			return 1;
		}
	}
	return 0;
}

static struct MenuEntryVTable slider_vtable = {
	.methods = 4,
	.renderer = _menu_draw_MenuEntry_Slider,
	.mouse_event = _menu_mouse_MenuEntry_Slider,
};

struct MenuEntry * menu_create_slider(void) {
	struct MenuEntry * out = menu_create_separator(); /* Steal some defaults */
	out->_type = -1; /* Special */
	out->height = 24;
	out->rwidth = 200;
	out->vtable = &slider_vtable;
	return out;
}


static int volume_left = 0;
static void show_volume_status(void) {
	if (!volume_menu) {
		volume_menu = menu_create();
		volume_menu->flags |= MENU_FLAG_BUBBLE_LEFT;
	}

	/* Clear the menu */
	while (volume_menu->entries->length) {
		node_t * node = list_pop(volume_menu->entries);
		menu_free_entry((struct MenuEntry *)node->value);
		free(node);
	}

	menu_insert(volume_menu, menu_create_slider());

	/* TODO Our mixer supports multiple knobs and we could show all of them. */
	/* TODO We could also show a nice slider... if we had one... */

	if (!volume_menu->window) {
		menu_prepare(volume_menu, yctx);
		if (volume_menu->window) {
			if (volume_left + volume_menu->window->width > (unsigned int)width) {
				yutani_window_move(yctx, volume_menu->window, width - volume_menu->window->width, DROPDOWN_OFFSET);
			} else {
				yutani_window_move(yctx, volume_menu->window, volume_left, DROPDOWN_OFFSET);
			}
			yutani_flip(yctx,volume_menu->window);
		}
	}
}

static int weather_left = 0;
static struct MenuEntry_Normal * weather_title_entry;
static struct MenuEntry_Normal * weather_updated_entry;
static struct MenuEntry_Normal * weather_conditions_entry;
static struct MenuEntry_Normal * weather_humidity_entry;
static struct MenuEntry_Normal * weather_clouds_entry;
static char * weather_title_str;
static char * weather_updated_str;
static char * weather_conditions_str;
static char * weather_humidity_str;
static char * weather_clouds_str;
static char * weather_temp_str;
static int weather_status_valid = 0;
static hashmap_t * weather_icons = NULL;
static sprite_t * weather_icon = NULL;

static void update_weather_status(void) {
	FILE * f = fopen("/tmp/weather-parsed.conf","r");
	if (!f) {
		weather_status_valid = 0;
		if (widgets_weather_enabled) {
			widgets_weather_enabled = 0;
			/* Unshow */
			widgets_width -= 2*WIDGET_WIDTH;
		}
		return;
	}

	weather_status_valid = 1;
	if (!widgets_weather_enabled) {
		widgets_weather_enabled = 1;
		widgets_width += 2*WIDGET_WIDTH;
	}

	if (weather_title_str) free(weather_title_str);
	if (weather_updated_str) free(weather_updated_str);
	if (weather_conditions_str) free(weather_conditions_str);
	if (weather_humidity_str) free(weather_humidity_str);
	if (weather_clouds_str) free(weather_clouds_str);
	if (weather_temp_str) free(weather_temp_str);

	/* read the entire status file */
	fseek(f, 0, SEEK_END);
	size_t size = ftell(f);
	fseek(f, 0, SEEK_SET);
	char * data = malloc(size + 1);
	fread(data, size, 1, f);
	data[size] = 0;
	fclose(f);

	/* Find relevant pieces */
	char * t = data;
	char * temp = t;
	t = strstr(t, "\n"); *t = '\0'; t++;
	char * temp_r = t;
	t = strstr(t, "\n"); *t = '\0'; t++;
	char * conditions = t;
	t = strstr(t, "\n"); *t = '\0'; t++;
	char * icon = t;
	t = strstr(t, "\n"); *t = '\0'; t++;
	char * humidity = t;
	t = strstr(t, "\n"); *t = '\0'; t++;
	char * clouds = t;
	t = strstr(t, "\n"); *t = '\0'; t++;
	char * city = t;
	t = strstr(t, "\n"); *t = '\0'; t++;
	char * updated = t;

	if (!weather_icons) {
		weather_icons = hashmap_create(10);
	}

	if (!hashmap_has(weather_icons, icon)) {
		sprite_t * tmp = malloc(sizeof(sprite_t));
		char path[512];
		sprintf(path,"/usr/share/icons/weather/%s.png", icon);
		load_sprite(tmp, path);
		hashmap_set(weather_icons, icon, tmp);
	}

	weather_icon = hashmap_get(weather_icons, icon);

	char tmp[300];
	sprintf(tmp, "Weather for <b>%s</b>", city);
	weather_title_str = strdup(tmp);
	sprintf(tmp, "<small><i>%s</i></small>", updated);
	weather_updated_str = strdup(tmp);
	sprintf(tmp, "<b>%s°</b> - %s", temp, conditions);
	weather_conditions_str = strdup(tmp);
	sprintf(tmp, "<b>Humidity:</b> %s%%", humidity);
	weather_humidity_str = strdup(tmp);
	sprintf(tmp, "<b>Clouds:</b> %s%%", clouds);
	weather_clouds_str = strdup(tmp);

	sprintf(tmp, "%s°", temp_r);
	weather_temp_str = strdup(tmp);

	free(data);
}

static int netstat_left = 0;
static int netstat_count = 0;
static char netstat_data[32][1024];

static void ip_ntoa(const uint32_t src_addr, char * out) {
	snprintf(out, 16, "%d.%d.%d.%d",
		(src_addr & 0xFF000000) >> 24,
		(src_addr & 0xFF0000) >> 16,
		(src_addr & 0xFF00) >> 8,
		(src_addr & 0xFF));
}

static void check_network(const char * if_name) {
	if (netstat_count >= 32) return;

	char if_path[512];
	snprintf(if_path, 511, "/dev/net/%s", if_name);
	int netdev = open(if_path, O_RDWR);

	if (netdev < 0) return;

	/* Get IPv4 address */
	uint32_t ip_addr;
	if (!ioctl(netdev, SIOCGIFADDR, &ip_addr)) {
		char ip_str[16];
		ip_ntoa(ntohl(ip_addr), ip_str);
		snprintf(netstat_data[netstat_count], 1023, "%s: %s", if_name, ip_str);
		network_status |= 2;
	} else {
		snprintf(netstat_data[netstat_count], 1023, "%s: disconnected", if_name);
		network_status |= 1;
	}

	close(netdev);
	netstat_count++;
}

static void update_network_status(void) {
	network_status = 0;
	netstat_count = 0;

	DIR * d = opendir("/dev/net");
	if (!d) return;

	struct dirent * ent;
	while ((ent = readdir(d))) {
		if (ent->d_name[0] == '.') continue;
		if (!strcmp(ent->d_name, "lo")) continue; /* Ignore loopback */
		check_network(ent->d_name);
	}

	closedir(d);
}

static void show_logout_menu(void) {
	if (!logout_menu->window) {
		menu_prepare(logout_menu, yctx);
		if (logout_menu->window) {
			yutani_window_move(yctx, logout_menu->window, width - logout_menu->window->width - X_PAD, DROPDOWN_OFFSET);
			yutani_flip(yctx, logout_menu->window);
		}
	}
}

static void show_app_menu(void) {
	if (!appmenu->window) {
		menu_prepare(appmenu, yctx);
		if (appmenu->window) {
			yutani_window_move(yctx, appmenu->window, X_PAD, DROPDOWN_OFFSET);
			yutani_flip(yctx, appmenu->window);
		}
	}
}

static void show_cal_menu(void) {
	if (!calmenu->window) {
		menu_prepare(calmenu, yctx);
		if (calmenu->window) {
			yutani_window_move(yctx, calmenu->window, width - TIME_LEFT - date_widget_width / 2 - calmenu->window->width / 2, DROPDOWN_OFFSET);
			yutani_flip(yctx, calmenu->window);
		}
	}
}

static void show_clock_menu(void) {
	if (!clockmenu->window) {
		menu_prepare(clockmenu, yctx);
		if (clockmenu->window) {
			yutani_window_move(yctx, clockmenu->window, width - LOGOUT_WIDTH - clockmenu->window->width, DROPDOWN_OFFSET);
			yutani_flip(yctx, clockmenu->window);
		}
	}
}

static void weather_refresh(struct MenuEntry * self) {
	(void)self;
	system("weather-tool &");
}

static void weather_configure(struct MenuEntry * self) {
	(void)self;
	system("terminal sh -c \"sudo weather-configurator; weather-tool\" &");
}

static void show_weather_status(void) {
	if (!weather) {
		weather = menu_create();
		weather->flags |= MENU_FLAG_BUBBLE_LEFT;
		weather_title_entry = (struct MenuEntry_Normal *)menu_create_normal(NULL, NULL, "", NULL);
		menu_insert(weather, weather_title_entry);
		weather_updated_entry = (struct MenuEntry_Normal *)menu_create_normal(NULL, NULL, "", NULL);
		menu_insert(weather, weather_updated_entry);
		menu_insert(weather, menu_create_separator());
		weather_conditions_entry = (struct MenuEntry_Normal *)menu_create_normal(NULL, NULL, "", NULL);
		menu_insert(weather, weather_conditions_entry);
		weather_humidity_entry = (struct MenuEntry_Normal *)menu_create_normal(NULL, NULL, "", NULL);
		menu_insert(weather, weather_humidity_entry);
		weather_clouds_entry = (struct MenuEntry_Normal *)menu_create_normal(NULL, NULL, "", NULL);
		menu_insert(weather, weather_clouds_entry);
		menu_insert(weather, menu_create_separator());
		menu_insert(weather, menu_create_normal("refresh", NULL, "Refresh...", weather_refresh));
		menu_insert(weather, menu_create_normal("config", NULL, "Configure...", weather_configure));
		menu_insert(weather, menu_create_separator());
		menu_insert(weather, menu_create_normal(NULL, NULL, "<small><i>Weather data provided by</i></small>", NULL));
		menu_insert(weather, menu_create_normal(NULL, NULL, "<color #0000FF>OpenWeatherMap.org</color>", NULL));
	}
	if (weather_status_valid) {
		menu_update_title(weather_title_entry, weather_title_str);
		menu_update_title(weather_updated_entry, weather_updated_str);
		menu_update_title(weather_conditions_entry, weather_conditions_str);
		menu_update_title(weather_humidity_entry, weather_humidity_str);
		menu_update_title(weather_clouds_entry, weather_clouds_str);
	}
	if (!weather->window) {
		int mwidth, mheight, offset;
		menu_calculate_dimensions(weather, &mheight, &mwidth);
		if (weather_left + mwidth > width - X_PAD) {
			if (weather_left + mwidth / 2 > width - X_PAD) {
				offset = weather_left + WIDGET_WIDTH * 2 - mwidth / 2;
				weather->flags = (weather->flags & ~MENU_FLAG_BUBBLE) | MENU_FLAG_BUBBLE_RIGHT;
			} else {
				offset = weather_left + WIDGET_WIDTH - mwidth / 2;
				weather->flags = (weather->flags & ~MENU_FLAG_BUBBLE) | MENU_FLAG_BUBBLE_CENTER;
			}
		} else {
			offset = weather_left;
			weather->flags = (weather->flags & ~MENU_FLAG_BUBBLE) | MENU_FLAG_BUBBLE_LEFT;
		}
		menu_prepare(weather, yctx);
		if (weather->window) {
			yutani_window_move(yctx, weather->window, offset, DROPDOWN_OFFSET);
			yutani_flip(yctx, weather->window);
		}
	}
}

static void show_network_status(void) {
	if (!netstat) {
		netstat = menu_create();
		netstat->flags |= MENU_FLAG_BUBBLE_LEFT;
		menu_insert(netstat, menu_create_normal(NULL, NULL, "<b>Network Status</b>", NULL));
		menu_insert(netstat, menu_create_separator());
	}
	while (netstat->entries->length > 2) {
		node_t * node = list_pop(netstat->entries);
		menu_free_entry((struct MenuEntry *)node->value);
		free(node);
	}
	if (!network_status) {
		menu_insert(netstat, menu_create_normal(NULL, NULL, "No network.", NULL));
	} else {
		for (int i = 0; i < netstat_count; ++i) {
			menu_insert(netstat, menu_create_normal(NULL, NULL, netstat_data[i], NULL));
		}
	}
	if (!netstat->window) {
		menu_prepare(netstat, yctx);
		if (netstat->window) {
			if (netstat_left + netstat->window->width > (unsigned int)width) {
				yutani_window_move(yctx, netstat->window, width - netstat->window->width, DROPDOWN_OFFSET);
			} else {
				yutani_window_move(yctx, netstat->window, netstat_left, DROPDOWN_OFFSET);
			}
			yutani_flip(yctx, netstat->window);
		}
	}
}

/* Callback for mouse events */
static void panel_check_click(struct yutani_msg_window_mouse_event * evt) {
	if (evt->wid == panel->wid) {
		if (evt->command == YUTANI_MOUSE_EVENT_CLICK || _close_enough(evt)) {
			/* Up-down click */
			if (evt->new_x >= width - LOGOUT_WIDTH ) {
				show_logout_menu();
			} else if (evt->new_x < APP_OFFSET) {
				show_app_menu();
			} else if (evt->new_x >= width - TIME_LEFT) {
				show_clock_menu();
			} else if (evt->new_x >= width - TIME_LEFT - date_widget_width) {
				show_cal_menu();
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
			if (widgets_weather_enabled) {
				if (evt->new_x > WIDGET_POSITION(widget+1) && evt->new_x < WIDGET_POSITION(widget-1)) {
					weather_left = WIDGET_POSITION(widget+1);
					show_weather_status();
				}
				widget += 2;
			}
			if (widgets_network_enabled) {
				if (evt->new_x > WIDGET_POSITION(widget) && evt->new_x < WIDGET_POSITION(widget-1)) {
					netstat_left = WIDGET_POSITION(widget);
					show_network_status();
				}
				widget++;
			}
			if (widgets_volume_enabled) {
				if (evt->new_x > WIDGET_POSITION(widget) && evt->new_x < WIDGET_POSITION(widget-1)) {
					volume_left = WIDGET_POSITION(widget);
					show_volume_status();
				}
				widget++;
			}
		} else if (evt->buttons & YUTANI_MOUSE_BUTTON_RIGHT) {
			if (evt->new_x >= APP_OFFSET && evt->new_x < LEFT_BOUND) {
				for (int i = 0; i < MAX_WINDOW_COUNT; ++i) {
					if (ads_by_l[i] == NULL) break;
					if (evt->new_x >= ads_by_l[i]->left && evt->new_x < ads_by_l[i]->left + TOTAL_CELL_WIDTH) {
						window_show_menu(ads_by_l[i]->wid, evt->new_x, DROPDOWN_OFFSET);
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
				if (widgets_weather_enabled) {
					if (evt->new_x > WIDGET_POSITION(widget+1) && evt->new_x < WIDGET_POSITION(widget-1)) {
						/* Ignore */
					}
					widget += 2;
				}
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

	tt_set_size(font, 20);
	int t = tt_string_width(font, altf2_buffer);
	tt_draw_string(a2ctx, font, center_x_a2(t), 80, altf2_buffer, rgb(255,255,255));

	flip(a2ctx);
	yutani_flip(yctx, alt_f2);
}

static void redraw_alttab(void) {
	if (!actx) return;
	if (new_focused == -1) return;

	/* Draw the background, right now just a dark semi-transparent box */
	draw_fill(actx, 0);
	draw_rounded_rectangle(actx,0,0, ALTTAB_WIDTH, ALTTAB_HEIGHT, 10, ALTTAB_BACKGROUND);

	if (ads_by_z[new_focused]) {
		struct window_ad * ad = ads_by_z[new_focused];

		/* try very hard to get a window texture */
		char key[1024];
		YUTANI_SHMKEY_EXP(yctx->server_ident, key, 1024, ad->bufid);
		size_t size;
		uint32_t * buf =  shm_obtain(key, &size);

		if (buf) {
			sprite_t tmp;
			tmp.width = ad->width;
			tmp.height = ad->height;
			tmp.bitmap = buf;

			int oy = 0;
			int sw, sh;
			if (tmp.width > tmp.height) {
				sw = ALTTAB_WIN_SIZE;
				sh = tmp.height * ALTTAB_WIN_SIZE / tmp.width;
				oy = (ALTTAB_WIN_SIZE - sh) / 2;
			} else {
				sh = ALTTAB_WIN_SIZE;
				sw = tmp.width * ALTTAB_WIN_SIZE / tmp.height;
			}
			draw_sprite_scaled(actx, &tmp, center_x_a(sw), ALTTAB_OFFSET + oy, sw, sh);

			shm_release(key);

			sprite_t * icon = icon_get_48(ad->icon);
			draw_sprite(actx, icon, center_x_a(-ALTTAB_WIN_SIZE) - 50, ALTTAB_OFFSET + ALTTAB_WIN_SIZE - 50);
		} else {
			sprite_t * icon = icon_get_48(ad->icon);
			draw_sprite(actx, icon, center_x_a(48), ALTTAB_OFFSET + (ALTTAB_WIN_SIZE - 48) / 2);
		}

		tt_set_size(font, 16);
		int t = tt_string_width(font, ad->name);
		tt_draw_string(actx, font, center_x_a(t), 12 + ALTTAB_OFFSET + 140 + 16, ad->name, rgb(255,255,255));
	}

	flip(actx);
	yutani_flip(yctx, alttab);
}

static pthread_t _waiter_thread;
static void * logout_prompt_waiter(void * arg) {
	if (system("showdialog \"Log Out\" /usr/share/icons/48/exit.png \"Are you sure you want to log out?\"") == 0) {
		yutani_session_end(yctx);
		_continue = 0;
	}
	return NULL;
}

static void launch_application_menu(struct MenuEntry * self) {
	struct MenuEntry_Normal * _self = (void *)self;

	if (!strcmp((char *)_self->action,"log-out")) {
		/* Spin off a thread for this */
		pthread_create(&_waiter_thread, NULL, logout_prompt_waiter, NULL);
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

		launch_application("exec terminal");
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

	if ((ke->event.modifiers & KEY_MOD_LEFT_ALT) &&
		(ke->event.keycode == KEY_F3) &&
		(ke->event.action == KEY_ACTION_DOWN)) {
		for (int i = 0; i < MAX_WINDOW_COUNT; ++i) {
			if (ads_by_l[i] == NULL) break;
			if (ads_by_l[i]->flags & 1) {
				window_show_menu(ads_by_l[i]->wid, ads_by_l[i]->left, DROPDOWN_OFFSET);
			}
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
		actx = NULL;

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
			alttab = yutani_window_create_flags(yctx, ALTTAB_WIDTH, ALTTAB_HEIGHT,
				YUTANI_WINDOW_FLAG_NO_STEAL_FOCUS | YUTANI_WINDOW_FLAG_NO_ANIMATION);

			yutani_set_stack(yctx, alttab, YUTANI_ZORDER_OVERLAY);

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

/**
 * Clip text and add ellipsis to fit a specified display width.
 */
static char * ellipsify(char * input, int font_size, struct TT_Font * font, int max_width, int * out_width) {
	int len = strlen(input);
	char * out = malloc(len + 4);
	memcpy(out, input, len + 1);
	int width;
	tt_set_size(font, font_size);
	while ((width = tt_string_width(font, out)) > max_width) {
		len--;
		out[len+0] = '.';
		out[len+1] = '.';
		out[len+2] = '.';
		out[len+3] = '\0';
	}

	if (out_width) *out_width = width;

	return out;
}

static void redraw(void) {
	spin_lock(&drawlock);

	struct timeval now;
	struct tm * timeinfo;

	uint32_t txt_color = TEXT_COLOR;
	int t = 0;

	/* Redraw the background */
	memcpy(ctx->backbuffer, bg_blob, bg_size);

	/* Get the current time for the clock */
	gettimeofday(&now, NULL);
	timeinfo = localtime((time_t *)&now.tv_sec);

	/* Hours : Minutes : Seconds */
	{
		char time[80];
		strftime(time, 80, "%H:%M:%S", timeinfo);
		tt_set_size(font, 16);
		tt_draw_string(ctx, font, width - TIME_LEFT, 3 + Y_PAD + 17, time, clockmenu->window ? HILIGHT_COLOR : txt_color);
	}

	{
		int weekday_width, date_width;
		char weekday[80], date[80];

		strftime(weekday, 80, "%A", timeinfo);
		strftime(date, 80, "%B %e", timeinfo);

		tt_set_size(font, 11);
		tt_set_size(font_bold, 11);

		/* Update date_widget_width */
		weekday_width = tt_string_width(font, weekday);
		date_width = tt_string_width(font_bold, date);

		date_widget_width = (weekday_width > date_width ? weekday_width : date_width) + 24; /* A bit of padding... */

		/* Day-of-week */
		t = (date_widget_width - weekday_width) / 2;
		tt_draw_string(ctx, font, width - TIME_LEFT - date_widget_width + t, 2 + Y_PAD + 11, weekday,  calmenu->window ? HILIGHT_COLOR : txt_color);

		/* Month Day */
		t = (date_widget_width - date_width) / 2;
		tt_draw_string(ctx, font_bold, width - TIME_LEFT - date_widget_width + t, 12 + Y_PAD + 11, date,  calmenu->window ? HILIGHT_COLOR : txt_color);
	}

	/* Applications menu */
	tt_set_size(font, 16);
	tt_draw_string(ctx, font, 16, 3 + Y_PAD + 17, "Applications", appmenu->window ? HILIGHT_COLOR : txt_color);

	/* Draw each widget */
	int widget = 0;
	/* Weather */
	if (widgets_weather_enabled) {
		uint32_t color = (weather && weather->window) ? HILIGHT_COLOR : ICON_COLOR;
		tt_set_size(font, 12);
		int t = tt_string_width(font, weather_temp_str);
		tt_draw_string(ctx, font, WIDGET_POSITION(widget) + (WIDGET_WIDTH - t) / 2, 5 + Y_PAD + 12, weather_temp_str, color);
		draw_sprite_alpha_paint(ctx, weather_icon, WIDGET_POSITION(widget+1), ICON_Y_PAD, 1.0, color);
		widget += 2;
	}
	/* - Network */
	if (widgets_network_enabled) {
		uint32_t color = (netstat && netstat->window) ? HILIGHT_COLOR : ICON_COLOR;
		if (network_status & 2) {
			draw_sprite_alpha_paint(ctx, sprite_net_active, WIDGET_POSITION(widget), ICON_Y_PAD, 1.0, color);
		} else {
			draw_sprite_alpha_paint(ctx, sprite_net_disabled, WIDGET_POSITION(widget), ICON_Y_PAD, 1.0, color);
		}
		widget++;
	}
	/* - Volume */
	if (widgets_volume_enabled) {
		uint32_t color = (volume_menu && volume_menu->window) ? HILIGHT_COLOR : ICON_COLOR;
		if (volume_level < 10) {
			draw_sprite_alpha_paint(ctx, sprite_volume_mute, WIDGET_POSITION(widget), ICON_Y_PAD, 1.0, color);
		} else if (volume_level < 0x547ae147) {
			draw_sprite_alpha_paint(ctx, sprite_volume_low, WIDGET_POSITION(widget), ICON_Y_PAD, 1.0, color);
		} else if (volume_level < 0xa8f5c28e) {
			draw_sprite_alpha_paint(ctx, sprite_volume_med, WIDGET_POSITION(widget), ICON_Y_PAD, 1.0, color);
		} else {
			draw_sprite_alpha_paint(ctx, sprite_volume_high, WIDGET_POSITION(widget), ICON_Y_PAD, 1.0, color);
		}
		widget++;
	}

	/* Now draw the window list */
	int i = 0, j = 0;
	spin_lock(&lock);
	if (window_list) {
		foreach(node, window_list) {
			struct window_ad * ad = node->value;
			int w = title_width;

			if (APP_OFFSET + i + w > LEFT_BOUND) {
				break;
			}

			/* Hilight the focused window */
			if (ad->flags & 1) {
				/* This is the focused window */
				for (int y = 0; y < GRADIENT_HEIGHT; ++y) {
					for (int x = APP_OFFSET + i; x < APP_OFFSET + i + w; ++x) {
						GFX(ctx, x, y+Y_PAD) = alpha_blend_rgba(GFX(ctx, x, y+Y_PAD), GRADIENT_AT(y));
					}
				}
			}

			if (title_width >= MIN_TEXT_WIDTH) {
				/* Ellipsifiy the title */
				char * s = ellipsify(ad->name, 14, font, title_width - 4, NULL);
				sprite_t * icon = icon_get_48(ad->icon);
				gfx_context_t * subctx = init_graphics_subregion(ctx, APP_OFFSET + i, Y_PAD, w, PANEL_HEIGHT - Y_PAD * 2);
				draw_sprite_scaled_alpha(subctx, icon, w - 48 - 2, 0, 48, 48, (ad->flags & 1) ? 1.0 : 0.7);
				tt_draw_string_shadow(subctx, font, s, 14, 2, TEXT_Y_OFFSET, (j == focused_app) ? HILIGHT_COLOR : (ad->flags & 1) ? FOCUS_COLOR : txt_color, rgb(0,0,0), 4);
				free(subctx);
				free(s);
			} else {
				sprite_t * icon = icon_get_16(ad->icon);
				gfx_context_t * subctx = init_graphics_subregion(ctx, APP_OFFSET + i, Y_PAD, w, PANEL_HEIGHT - Y_PAD * 2);
				draw_sprite_scaled(subctx, icon, 6, 6, 16, 16);
				free(subctx);
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
	draw_sprite_alpha_paint(ctx, sprite_logout, width - LOGOUT_WIDTH, 1 + ICON_Y_PAD, 1.0, (logout_menu->window ? HILIGHT_COLOR : ICON_COLOR)); /* Logout button */

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
		ad->name = &s[0];
		ad->icon = &s[wa->icon];
		ad->strings = s;
		ad->flags = wa->flags;
		ad->wid = wa->wid;
		ad->bufid = wa->bufid;
		ad->width = wa->width;
		ad->height = wa->height;

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
			title_width = 28;
		} else {
			title_width = tmp / new_window_list->length;
			if (title_width > MAX_TEXT_WIDTH) {
				title_width = MAX_TEXT_WIDTH;
			}
			if (title_width < MIN_TEXT_WIDTH) {
				title_width = 28;
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

static void redraw_panel_background(gfx_context_t * ctx, int width, int height) {
	draw_fill(ctx, rgba(0,0,0,0));
	draw_rounded_rectangle(ctx, X_PAD, Y_PAD, width - X_PAD * 2, panel->height - Y_PAD * 2, 14, rgba(0,0,0,200));
}

static void resize_finish(int xwidth, int xheight) {
	yutani_window_resize_accept(yctx, panel, xwidth, xheight);

	reinit_graphics_yutani(ctx, panel);
	yutani_window_resize_done(yctx, panel);

	width = xwidth;

	redraw_panel_background(ctx, xwidth, xheight);

	/* Copy the prerendered background so we can redraw it quickly */
	bg_size = panel->width * panel->height * sizeof(uint32_t);
	bg_blob = realloc(bg_blob, bg_size);
	memcpy(bg_blob, ctx->backbuffer, bg_size);

	update_window_list();
	redraw();
}

static void bind_keys(void) {

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

	/* Alt+F3 = window context menu */
	yutani_key_bind(yctx, KEY_F3, KEY_MOD_LEFT_ALT, YUTANI_BIND_STEAL);

	/* This lets us receive all just-modifier key releases */
	yutani_key_bind(yctx, KEY_LEFT_ALT, 0, YUTANI_BIND_PASSTHROUGH);

}

static void sig_usr2(int sig) {
	yutani_set_stack(yctx, panel, YUTANI_ZORDER_TOP);
	yutani_flip(yctx, panel);
	bind_keys();
	signal(SIGUSR2, sig_usr2);
}

static sprite_t * watchface = NULL;

static void watch_draw_line(gfx_context_t * ctx, int offset, double r, double a, double b, uint32_t color, float thickness) {
	double theta = (a / b) * 2.0 * M_PI;
	draw_line_aa(ctx,
		70 + 4,
		70 + 4 + sin(theta) * r,
		70 + offset,
		70 + offset - cos(theta) * r, color, thickness);
}

void _menu_draw_MenuEntry_Clock(gfx_context_t * ctx, struct MenuEntry * self, int offset) {
	self->offset = offset;

	draw_sprite(ctx, watchface, 4, offset);

	struct timeval now;
	struct tm * timeinfo;
	gettimeofday(&now, NULL);
	timeinfo = localtime((time_t *)&now.tv_sec);

	double sec = timeinfo->tm_sec + (double)now.tv_usec / 1000000.0;
	double min = timeinfo->tm_min + sec / 60.0;
	double hour = (timeinfo->tm_hour % 12) + min / 60.0;

	watch_draw_line(ctx, offset, 40, hour, 12, rgb(0,0,0), 2.0);
	watch_draw_line(ctx, offset, 60, min, 60, rgb(0,0,0), 1.5);
	watch_draw_line(ctx, offset, 65, sec, 60, rgb(240,0,0), 1.0);

}

static struct MenuEntryVTable clock_vtable = {
	.methods = 3,
	.renderer = _menu_draw_MenuEntry_Clock,
};

struct MenuEntry * menu_create_clock(void) {
	struct MenuEntry * out = menu_create_separator(); /* Steal some defaults */

	if (!watchface) {
		watchface = malloc(sizeof(sprite_t));
		load_sprite(watchface, "/usr/share/icons/watchface.png");
	}

	out->_type = -1; /* Special */
	out->height = 140;
	out->rwidth = 148;
	out->vtable = &clock_vtable;
	return out;
}

#define CALENDAR_LINE_HEIGHT 22
#define CALENDAR_BASE_HEIGHT 45
#define CALENDAR_PAD_HEIGHT  2

static int days_in_month(struct tm * timeinfo) {
	static int days_in_months[] = {
		31, 0, 31, 30, 31, 30, 31,
		31, 30, 31, 30, 31,
	};
	if (timeinfo->tm_mon != 1) return days_in_months[timeinfo->tm_mon];
	/* How many days in February? */
	struct tm tmp;
	memcpy(&tmp, timeinfo, sizeof(struct tm));
	tmp.tm_mday = 29;
	tmp.tm_hour = 12;
	time_t tmp3 = mktime(&tmp);
	struct tm * tmp2 = localtime(&tmp3);
	return tmp2->tm_mday == 29 ? 29 : 28;
}

static int weeks_in_month(struct tm * timeinfo) {
	int line = 0;
	int wday = (36 + timeinfo->tm_wday - timeinfo->tm_mday) % 7;
	for (int day = 1; day <= days_in_month(timeinfo); day++, (wday = (wday + 1) % 7)) {
		if (wday == 6) {
			line++;
		}
	}
	return (wday ? line + 1 : line);
}

void _menu_draw_MenuEntry_Calendar(gfx_context_t * ctx, struct MenuEntry * self, int offset) {
	self->offset = offset;

	struct timeval now;
	gettimeofday(&now, NULL);

	struct tm actual;
	struct tm * timeinfo;
	timeinfo = localtime((time_t *)&now.tv_sec);
	memcpy(&actual, timeinfo, sizeof(struct tm));
	timeinfo = &actual;

	/* Render heading with Month Year */
	{
		char month[20];
		strftime(month, 20, "%B %Y", timeinfo);

		tt_set_size(font_bold, 16);
		tt_draw_string(ctx, font_bold, (self->width - tt_string_width(font_bold, month)) / 2, self->offset + 16, month, rgb(0,0,0));
	}

	/* Get ready to draw a table... */
	int cell_size = self->width / 7;
	int base_left = (self->width - cell_size * 7) / 2;

	/* Render weekday abbreviations */
	const char * weekdays[] = {"Su","Mo","Tu","We","Th","Fr","Sa",NULL};
	int left = base_left;
	tt_set_size(font, 11);
	for (const char ** w = weekdays; *w; w++) {
		tt_draw_string(ctx, font, left + (cell_size - tt_string_width(font,*w)) / 2,
			self->offset + 22 + 13, *w, rgb(0,0,0));
		left += cell_size;
	}

	int weeks = weeks_in_month(timeinfo);
	self->height = CALENDAR_LINE_HEIGHT * weeks + CALENDAR_BASE_HEIGHT + CALENDAR_PAD_HEIGHT;

	/* The 1st was a... */
	int wday = (36 + timeinfo->tm_wday - timeinfo->tm_mday) % 7;

	int line = 0;
	left = base_left + cell_size * wday;
	tt_set_size(font, 13);
	for (int day = 1; day <= days_in_month(timeinfo); day++, (wday = (wday + 1) % 7)) {
		char date[12];
		snprintf(date, 11, "%d", day);
		/* Is this the cell for today? */
		if (day == timeinfo->tm_mday) {
			draw_rounded_rectangle(ctx, left - 1, self->offset + CALENDAR_BASE_HEIGHT + line * CALENDAR_LINE_HEIGHT - 2, cell_size + 2, CALENDAR_LINE_HEIGHT, 12, SPECIAL_COLOR);
			tt_draw_string(ctx, font, left + (cell_size - tt_string_width(font, date)) / 2,
				self->offset + CALENDAR_BASE_HEIGHT + 13 + line * CALENDAR_LINE_HEIGHT, date, rgb(255,255,255));
		} else {
			tt_draw_string(ctx, font, left + (cell_size - tt_string_width(font, date)) / 2,
				self->offset + CALENDAR_BASE_HEIGHT + 13 + line * CALENDAR_LINE_HEIGHT, date, (wday == 0 || wday == 6) ? rgba(0,0,0,120) : rgb(0,0,0));
		}
		if (wday == 6) {
			left = base_left;
			line++;
		} else {
			left += cell_size;
		}
	}
}

static struct MenuEntryVTable calendar_vtable = {
	.methods = 3,
	.renderer = _menu_draw_MenuEntry_Calendar,
};

/*
 * Special menu entry to display a calendar
 */
struct MenuEntry * menu_create_calendar(void) {
	struct MenuEntry * out = menu_create_separator(); /* Steal some defaults */

	out->_type = -1; /* Special */

	struct timeval now;
	gettimeofday(&now, NULL);
	out->height = CALENDAR_LINE_HEIGHT * weeks_in_month(localtime((time_t *)&now.tv_sec)) + CALENDAR_BASE_HEIGHT + CALENDAR_PAD_HEIGHT;

	tt_set_size(font_mono, 13);
	out->rwidth = 200; //tt_string_width(font_mono, "XX XX XX XX XX XX XX") + 20;
	out->vtable = &calendar_vtable;
	return out;
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

	font           = tt_font_from_shm("sans-serif");
	font_bold      = tt_font_from_shm("sans-serif.bold");
	font_mono      = tt_font_from_shm("monospace");
	font_mono_bold = tt_font_from_shm("monospace.bold");

	/* For convenience, store the display size */
	width  = yctx->display_width;
	height = yctx->display_height;

	/* Create the panel window */
	panel = yutani_window_create_flags(yctx, width, PANEL_HEIGHT, YUTANI_WINDOW_FLAG_NO_STEAL_FOCUS | YUTANI_WINDOW_FLAG_ALT_ANIMATION);

	/* And move it to the top layer */
	yutani_set_stack(yctx, panel, YUTANI_ZORDER_TOP);
	yutani_window_update_shape(yctx, panel, YUTANI_SHAPE_THRESHOLD_CLEAR);

	/* Initialize graphics context against the window */
	ctx = init_graphics_yutani_double_buffer(panel);

	/* Clear it out (the compositor should initialize it cleared anyway */
	draw_fill(ctx, rgba(0,0,0,0));
	flip(ctx);
	yutani_flip(yctx, panel);

	/* Load textures for the background and logout button */
	sprite_panel  = malloc(sizeof(sprite_t));
	sprite_logout = malloc(sizeof(sprite_t));

	load_sprite(sprite_logout, "/usr/share/icons/panel-shutdown.png");

	struct stat stat_tmp;
	if (!stat("/dev/dsp",&stat_tmp)) {
		widgets_volume_enabled = 1;
		widgets_width += WIDGET_WIDTH;
		sprite_volume_mute = malloc(sizeof(sprite_t));
		sprite_volume_low  = malloc(sizeof(sprite_t));
		sprite_volume_med  = malloc(sizeof(sprite_t));
		sprite_volume_high = malloc(sizeof(sprite_t));
		load_sprite(sprite_volume_mute, "/usr/share/icons/24/volume-mute.png");
		load_sprite(sprite_volume_low,  "/usr/share/icons/24/volume-low.png");
		load_sprite(sprite_volume_med,  "/usr/share/icons/24/volume-medium.png");
		load_sprite(sprite_volume_high, "/usr/share/icons/24/volume-full.png");
		/* XXX store current volume */
	}

	{
		widgets_network_enabled = 1;
		widgets_width += WIDGET_WIDTH;
		sprite_net_active = malloc(sizeof(sprite_t));
		load_sprite(sprite_net_active, "/usr/share/icons/24/net-active.png");
		sprite_net_disabled = malloc(sizeof(sprite_t));
		load_sprite(sprite_net_disabled, "/usr/share/icons/24/net-disconnected.png");
	}

	weather_refresh(NULL);

	/* Draw the background */
	redraw_panel_background(ctx, panel->width, panel->height);

	/* Copy the prerendered background so we can redraw it quickly */
	bg_size = panel->width * panel->height * sizeof(uint32_t);
	bg_blob = malloc(bg_size);
	memcpy(bg_blob, ctx->backbuffer, bg_size);

	/* Catch SIGINT */
	signal(SIGINT, sig_int);
	signal(SIGUSR2, sig_usr2);

	appmenu = menu_set_get_root(menu_set_from_description("/etc/panel.menu", launch_application_menu));
	appmenu->flags = MENU_FLAG_BUBBLE_CENTER;

	clockmenu = menu_create();
	clockmenu->flags |= MENU_FLAG_BUBBLE_RIGHT;
	menu_insert(clockmenu, menu_create_clock());

	calmenu = menu_create();
	calmenu->flags |= MENU_FLAG_BUBBLE_CENTER;
	menu_insert(calmenu, menu_create_calendar());

	window_menu = menu_create();
	window_menu->flags |= MENU_FLAG_BUBBLE_LEFT;
	menu_insert(window_menu, menu_create_normal(NULL, NULL, "Maximize", _window_menu_start_maximize));
	menu_insert(window_menu, menu_create_normal(NULL, NULL, "Move", _window_menu_start_move));
	menu_insert(window_menu, menu_create_separator());
	menu_insert(window_menu, menu_create_normal(NULL, NULL, "Close", _window_menu_close));

	logout_menu = menu_create();
	logout_menu->flags |= MENU_FLAG_BUBBLE_RIGHT;
	menu_insert(logout_menu, menu_create_normal("exit", "log-out", "Log Out", launch_application_menu));

	/* Subscribe to window updates */
	yutani_subscribe_windows(yctx);

	/* Ask compositor for window list */
	update_window_list();

	/* Key bindings */
	bind_keys();

	time_t last_tick = 0;

	int fds[1] = {fileno(yctx->sock)};

	while (_continue) {

		int index = fswait2(1,fds,clockmenu->window ? 50 : 200);

		if (clockmenu->window) {
			menu_force_redraw(clockmenu);
		}

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
		}

		struct timeval now;
		gettimeofday(&now, NULL);
		if (now.tv_sec != last_tick) {
			last_tick = now.tv_sec;
			waitpid(-1, NULL, WNOHANG);
			update_volume_level();
			update_network_status();
			update_weather_status();
			redraw();
			if (was_tabbing) {
				redraw_alttab();
			}
		}
	}

	/* Close the panel window */
	yutani_close(yctx, panel);

	/* Stop notifying us of window changes */
	yutani_unsubscribe_windows(yctx);

	return 0;
}

