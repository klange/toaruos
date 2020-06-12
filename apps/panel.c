/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2018 K. Lange
 *
 * Yutani Panel
 *
 * Provides an applications menu, a window list, status widgets,
 * and a clock, manages the user session, and provides alt-tab
 * window switching and alt-f2 app runner.
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
#include <sys/fswait.h>

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
static int widgets_weather_enabled = 0;

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
	sprintf(tmp, "Weather for %s", city);
	weather_title_str = strdup(tmp);
	sprintf(tmp, "%s", updated);
	weather_updated_str = strdup(tmp);
	sprintf(tmp, "%s° - %s", temp, conditions);
	weather_conditions_str = strdup(tmp);
	sprintf(tmp, "Humidity: %s%%", humidity);
	weather_humidity_str = strdup(tmp);
	sprintf(tmp, "Clouds: %s%%", clouds);
	weather_clouds_str = strdup(tmp);

	sprintf(tmp, "%s°", temp_r);
	weather_temp_str = strdup(tmp);

	free(data);
}

static int netstat_left = 0;

static struct MenuEntry_Normal * netstat_ip_entry;
static char * netstat_ip = NULL;
static struct MenuEntry_Normal * netstat_device_entry;
static char * netstat_device = NULL;
static struct MenuEntry_Normal * netstat_gateway_entry;
static char * netstat_gateway = NULL;
static struct MenuEntry_Normal * netstat_dns_entry;
static char * netstat_dns = NULL;
static struct MenuEntry_Normal * netstat_mac_entry;
static char * netstat_mac = NULL;

static void update_network_status(void) {
	FILE * net = fopen("/proc/netif","r");

	if (!net) return;

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
			if (netstat_ip) {
				free(netstat_ip);
			}
			char tmp[512];
			sprintf(tmp, "IP: %s", &line[strlen("ip: ")]);
			char * lf = strstr(tmp,"\n");
			if (lf) *lf = '\0';
			netstat_ip = strdup(tmp);
		} else if (strstr(line,"device:") != NULL) {
			network_status = 1;
			if (netstat_device) {
				free(netstat_device);
			}
			char tmp[512];
			sprintf(tmp, "Device: %s", &line[strlen("device: ")]);
			char * lf = strstr(tmp,"\n");
			if (lf) *lf = '\0';
			netstat_device = strdup(tmp);
		} else if (strstr(line,"gateway:") != NULL) {
			network_status = 1;
			if (netstat_gateway) {
				free(netstat_gateway);
			}
			char tmp[512];
			sprintf(tmp, "Gateway: %s", &line[strlen("gateway: ")]);
			char * lf = strstr(tmp,"\n");
			if (lf) *lf = '\0';
			netstat_gateway = strdup(tmp);
		} else if (strstr(line,"dns:") != NULL) {
			network_status = 1;
			if (netstat_dns) {
				free(netstat_dns);
			}
			char tmp[512];
			sprintf(tmp, "Primary DNS: %s", &line[strlen("dns: ")]);
			char * lf = strstr(tmp,"\n");
			if (lf) *lf = '\0';
			netstat_dns = strdup(tmp);
		} else if (strstr(line,"mac:") != NULL) {
			network_status = 1;
			if (netstat_mac) {
				free(netstat_mac);
			}
			char tmp[512];
			sprintf(tmp, "MAC: %s", &line[strlen("mac: ")]);
			char * lf = strstr(tmp,"\n");
			if (lf) *lf = '\0';
			netstat_mac = strdup(tmp);
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

static void show_cal_menu(void) {
	if (!calmenu->window) {
		menu_show(calmenu, yctx);
		if (calmenu->window) {
			yutani_window_move(yctx, calmenu->window, width - 24 - calmenu->window->width, PANEL_HEIGHT);
		}
	}
}

static void show_clock_menu(void) {
	if (!clockmenu->window) {
		menu_show(clockmenu, yctx);
		if (clockmenu->window) {
			yutani_window_move(yctx, clockmenu->window, width - 24 - clockmenu->window->width, PANEL_HEIGHT);
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
		menu_insert(weather, menu_create_normal(NULL, NULL, "Weather data provided by", NULL));
		menu_insert(weather, menu_create_normal(NULL, NULL, "OpenWeatherMap.org", NULL));
	}
	if (weather_status_valid) {
		menu_update_title(weather_title_entry, weather_title_str);
		menu_update_title(weather_updated_entry, weather_updated_str);
		menu_update_title(weather_conditions_entry, weather_conditions_str);
		menu_update_title(weather_humidity_entry, weather_humidity_str);
		menu_update_title(weather_clouds_entry, weather_clouds_str);
	}
	if (!weather->window) {
		menu_show(weather, yctx);
		if (weather->window) {
			if (weather_left + weather->window->width > (unsigned int)width) {
				yutani_window_move(yctx, weather->window, width - weather->window->width, PANEL_HEIGHT);
			} else {
				yutani_window_move(yctx, weather->window, weather_left, PANEL_HEIGHT);
			}
		}
	}
}

static void show_network_status(void) {
	if (!netstat) {
		netstat = menu_create();
		menu_insert(netstat, menu_create_normal(NULL, NULL, "Network Status", NULL));
		menu_insert(netstat, menu_create_separator());
		netstat_ip_entry = (struct MenuEntry_Normal *)menu_create_normal(NULL, NULL, "", NULL);
		menu_insert(netstat, netstat_ip_entry);
		netstat_dns_entry = (struct MenuEntry_Normal *)menu_create_normal(NULL, NULL, "", NULL);
		menu_insert(netstat, netstat_dns_entry);
		netstat_gateway_entry = (struct MenuEntry_Normal *)menu_create_normal(NULL, NULL, "", NULL);
		menu_insert(netstat, netstat_gateway_entry);
		netstat_mac_entry = (struct MenuEntry_Normal *)menu_create_normal(NULL, NULL, "", NULL);
		menu_insert(netstat, netstat_mac_entry);
		netstat_device_entry = (struct MenuEntry_Normal *)menu_create_normal(NULL, NULL, "", NULL);
		menu_insert(netstat, netstat_device_entry);
	}
	if (network_status) {
		menu_update_title(netstat_ip_entry, netstat_ip);
		menu_update_title(netstat_device_entry, netstat_device ? netstat_device : "(?)");
		menu_update_title(netstat_dns_entry, netstat_dns ? netstat_dns : "(?)");
		menu_update_title(netstat_gateway_entry, netstat_gateway ? netstat_gateway : "(?)");
		menu_update_title(netstat_mac_entry, netstat_mac ? netstat_mac : "(?)");
	} else {
		menu_update_title(netstat_ip_entry, "No network.");
		menu_update_title(netstat_device_entry, "");
		menu_update_title(netstat_dns_entry, "");
		menu_update_title(netstat_gateway_entry, "");
		menu_update_title(netstat_mac_entry, "");
	}
	if (!netstat->window) {
		menu_show(netstat, yctx);
		if (netstat->window) {
			if (netstat_left + netstat->window->width > (unsigned int)width) {
				yutani_window_move(yctx, netstat->window, width - netstat->window->width, PANEL_HEIGHT);
			} else {
				yutani_window_move(yctx, netstat->window, netstat_left, PANEL_HEIGHT);
			}
		}
	}
}

/* Callback for mouse events */
static void panel_check_click(struct yutani_msg_window_mouse_event * evt) {
	if (evt->wid == panel->wid) {
		if (evt->command == YUTANI_MOUSE_EVENT_CLICK || _close_enough(evt)) {
			/* Up-down click */
			if (evt->new_x >= width - 24 ) {
				show_logout_menu();
			} else if (evt->new_x < APP_OFFSET) {
				show_app_menu();
			} else if (evt->new_x >= width - TIME_LEFT) {
				show_clock_menu();
			} else if (evt->new_x >= width - TIME_LEFT - DATE_WIDTH) {
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
					weather_left = WIDGET_POSITION(widget);
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
					/* TODO: Show the volume manager */
				}
				widget++;
			}
		} else if (evt->buttons & YUTANI_MOUSE_BUTTON_RIGHT) {
			if (evt->new_x >= APP_OFFSET && evt->new_x < LEFT_BOUND) {
				for (int i = 0; i < MAX_WINDOW_COUNT; ++i) {
					if (ads_by_l[i] == NULL) break;
					if (evt->new_x >= ads_by_l[i]->left && evt->new_x < ads_by_l[i]->left + TOTAL_CELL_WIDTH) {
						window_show_menu(ads_by_l[i]->wid, evt->new_x, PANEL_HEIGHT);
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

	int t = draw_sdf_string_width(altf2_buffer, 22, SDF_FONT_THIN);
	draw_sdf_string(a2ctx, center_x_a2(t), 60, altf2_buffer, 22, rgb(255,255,255), SDF_FONT_THIN);

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

		int t = draw_sdf_string_width(ad->name, 18, SDF_FONT_THIN);

		draw_sdf_string(actx, center_x_a(t), 12+ALTTAB_OFFSET+40, ad->name, 18, rgb(255,255,255), SDF_FONT_THIN);
	}

	flip(actx);
	yutani_flip(yctx, alttab);
}

static void launch_application_menu(struct MenuEntry * self) {
	struct MenuEntry_Normal * _self = (void *)self;

	if (!strcmp((char *)_self->action,"log-out")) {
		if (system("showdialog \"Log Out\" /usr/share/icons/48/exit.png \"Are you sure you want to log out?\"") == 0) {
			yutani_session_end(yctx);
			_continue = 0;
		}
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
				window_show_menu(ads_by_l[i]->wid, ads_by_l[i]->left, PANEL_HEIGHT);
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
	draw_sdf_string(ctx, width - TIME_LEFT, 3, buffer, 20, clockmenu->window ? HILIGHT_COLOR : txt_color, SDF_FONT_THIN);

	/* Day-of-week */
	strftime(buffer, 80, "%A", timeinfo);
	t = draw_sdf_string_width(buffer, 12, SDF_FONT_THIN);
	t = (DATE_WIDTH - t) / 2;
	draw_sdf_string(ctx, width - TIME_LEFT - DATE_WIDTH + t, 2, buffer, 12, calmenu->window ? HILIGHT_COLOR : txt_color, SDF_FONT_THIN);

	/* Month Day */
	strftime(buffer, 80, "%h %e", timeinfo);
	t = draw_sdf_string_width(buffer, 12, SDF_FONT_BOLD);
	t = (DATE_WIDTH - t) / 2;
	draw_sdf_string(ctx, width - TIME_LEFT - DATE_WIDTH + t, 12, buffer, 12, calmenu->window ? HILIGHT_COLOR : txt_color, SDF_FONT_BOLD);

	/* Applications menu */
	draw_sdf_string(ctx, 8, 3, "Applications", 20, appmenu->window ? HILIGHT_COLOR : txt_color, SDF_FONT_THIN);

	/* Draw each widget */
	int widget = 0;
	/* Weather */
	if (widgets_weather_enabled) {
		uint32_t color = (weather && weather->window) ? HILIGHT_COLOR : ICON_COLOR;
		int t = draw_sdf_string_width(weather_temp_str, 15, SDF_FONT_THIN);
		draw_sdf_string(ctx, WIDGET_POSITION(widget) + (WIDGET_WIDTH - t) / 2, 5, weather_temp_str, 15, color, SDF_FONT_THIN);
		draw_sprite_alpha_paint(ctx, weather_icon, WIDGET_POSITION(widget+1), 0, 1.0, color);
		widget += 2;
	}
	/* - Network */
	if (widgets_network_enabled) {
		uint32_t color = (netstat && netstat->window) ? HILIGHT_COLOR : ICON_COLOR;
		if (network_status == 1) {
			draw_sprite_alpha_paint(ctx, sprite_net_active, WIDGET_POSITION(widget), 0, 1.0, color);
		} else {
			draw_sprite_alpha_paint(ctx, sprite_net_disabled, WIDGET_POSITION(widget), 0, 1.0, color);
		}
		widget++;
	}
	/* - Volume */
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
	draw_sprite_alpha_paint(ctx, sprite_logout, width - 23, 1, 1.0, (logout_menu->window ? HILIGHT_COLOR : ICON_COLOR)); /* Logout button */

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

struct MenuEntry * menu_create_clock(void) {
	struct MenuEntry * out = menu_create_separator(); /* Steal some defaults */

	if (!watchface) {
		watchface = malloc(sizeof(sprite_t));
		load_sprite(watchface, "/usr/share/icons/watchface.png");
	}

	out->_type = -1; /* Special */
	out->height = 140;
	out->rwidth = 148;
	out->renderer = _menu_draw_MenuEntry_Clock;
	return out;
}

const char * month_names[] = {
	"January",
	"February",
	"March",
	"April",
	"May",
	"June",
	"July",
	"August",
	"September",
	"October",
	"November",
	"December",
};

int days_in_months[] = {
	31, 0, 31, 30, 31, 30, 31,
	31, 30, 31, 30, 31,
};

void _menu_draw_MenuEntry_Calendar(gfx_context_t * ctx, struct MenuEntry * self, int offset) {
	self->offset = offset;

	char lines[9][22];
	memset(lines, 0, sizeof(lines));

	struct timeval now;
	gettimeofday(&now, NULL);

	struct tm actual;
	struct tm * timeinfo;
	timeinfo = localtime((time_t *)&now.tv_sec);
	memcpy(&actual, timeinfo, sizeof(struct tm));
	timeinfo = &actual;

	char month[20];
	sprintf(month, "%s %d", month_names[timeinfo->tm_mon], timeinfo->tm_year + 1900);

	int len = (20 - strlen(month)) / 2;
	while (len > 0) {
		strcat(lines[0]," ");
		len--;
	}
	strcat(lines[0],month);

	/* Days of week */
	strcat(lines[1],"Su Mo Tu We Th Fr Sa");

	int days_in_month = days_in_months[timeinfo->tm_mon];
	if (days_in_month == 0) {
		/* How many days in February? */
		struct tm tmp;
		memcpy(&tmp, timeinfo, sizeof(struct tm));
		tmp.tm_mday = 29;
		tmp.tm_hour = 12;
		time_t tmp3 = mktime(&tmp);
		struct tm * tmp2 = localtime(&tmp3);
		if (tmp2->tm_mday == 29) {
			days_in_month = 29;
		} else {
			days_in_month = 28;
		}
	}

	int mday = timeinfo->tm_mday;
	int wday = timeinfo->tm_wday; /* 0 == sunday */

	while (mday > 1) {
		mday--;
		wday = (wday + 6) % 7;
	}

	for (int i = 0; i < wday; ++i) {
		strcat(lines[2],"   ");
	}

	int line = 2;
	while (mday <= days_in_month) {
		/* TODO Bold text? */
		char tmp[5];
		sprintf(tmp, "%2d ", mday);
		strcat(lines[line], tmp);
		if (wday == 6) line++;
		mday++;
		wday = (wday + 1) % 7;
	}

	self->height = 16 * (line+1) + 8;

	/* Go through each and draw with monospace font */
	for (int i = 0; i < 9; ++i) {
		if (lines[i][0] != 0) {
			draw_sdf_string(ctx, 10, 4 + i * 17, lines[i], 16, rgb(0,0,0), i == 0 ? SDF_FONT_MONO_BOLD : SDF_FONT_MONO);
		}
	}
}

/*
 * Special menu entry to display a calendar
 */
struct MenuEntry * menu_create_calendar(void) {
	struct MenuEntry * out = menu_create_separator(); /* Steal some defaults */

	out->_type = -1; /* Special */
	out->height = 16 * 9 + 8;
	out->rwidth = draw_sdf_string_width("XX XX XX XX XX XX XX", 16, SDF_FONT_MONO) + 20;
	out->renderer = _menu_draw_MenuEntry_Calendar;
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

	load_sprite(sprite_panel,  "/usr/share/panel.png");
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

	/* TODO Probably should use the app launch shortcut */
	system("sh -c \"sleep 4; weather-tool\" &");

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

	appmenu = menu_set_get_root(menu_set_from_description("/etc/panel.menu", launch_application_menu));

	clockmenu = menu_create();
	menu_insert(clockmenu, menu_create_clock());

	calmenu = menu_create();
	menu_insert(calmenu, menu_create_calendar());

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
		} else {
			struct timeval now;
			gettimeofday(&now, NULL);
			if (now.tv_sec != last_tick) {
				last_tick = now.tv_sec;
				waitpid(-1, NULL, WNOHANG);
				update_volume_level();
				update_network_status();
				update_weather_status();
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

