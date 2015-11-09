/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2015 Kevin Lange
 *
 * Wallpaper renderer.
 *
 */
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <math.h>
#include <time.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/time.h>

#include "lib/yutani.h"
#include "lib/graphics.h"
#include "lib/shmemfonts.h"
#include "lib/hashmap.h"
#include "lib/confreader.h"

#include "lib/trace.h"
#define TRACE_APP_NAME "wallpaper"

#define DEFAULT_WALLPAPER "/usr/share/wallpapers/yosemite.png"

#define ICON_X         24
#define ICON_TOP_Y     40
#define ICON_SPACING_Y 74
#define ICON_WIDTH     48
#define EXTRA_WIDTH    24

static int width;
static int height;
static yutani_t * yctx;
static yutani_window_t * wina;
static gfx_context_t * ctx;
static sprite_t * wallpaper;
static hashmap_t * icon_cache;

static char f_name[512];


static int center_x(int x) {
	return (width - x) / 2;
}

static int center_y(int y) {
	return (height - y) / 2;
}

typedef struct {
	char * icon;
	char * appname;
	char * title;
	sprite_t * icon_sprite;
} application_t;

static application_t * applications = NULL;

static volatile int _continue = 1;

/* Default search paths for icons, in order of preference */
static char * icon_directories[] = {
	"/usr/share/icons/48",
	"/usr/share/icons/external/48",
	"/usr/share/icons/24",
	"/usr/share/icons/external/24",
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

static void launch_application(char * app) {
	if (!fork()) {
		char * args[] = {"/bin/sh", "-c", app, NULL};
		execvp(args[0], args);
		exit(1);
	}
}

char * next_run_activate = NULL;
int focused_app = -1;

static void redraw_apps_x(int should_flip) {
	/* Load Application Shortcuts */
	uint32_t i = 0;
	while (1) {
		if (!applications[i].icon) {
			break;
		}
		draw_sprite(ctx, applications[i].icon_sprite, ICON_X, ICON_TOP_Y + ICON_SPACING_Y * i);

		uint32_t color = rgb(255,255,255);

		if (i == focused_app) {
			color = rgb(142,216,255);
		}

		int str_w = draw_string_width(applications[i].title) / 2;
		int str_x = ICON_X + ICON_WIDTH / 2 - str_w;
		int str_y = ICON_TOP_Y + ICON_SPACING_Y * i + ICON_WIDTH + 14;
		draw_string_shadow(ctx, str_x, str_y, color, applications[i].title, rgb(0,0,0), 2, 1, 1, 3.0);

		++i;
	}

	if (should_flip) {
		flip(ctx);
	}
}

static void redraw_apps(int should_flip) {
	draw_sprite(ctx, wallpaper, 0, 0);
	redraw_apps_x(should_flip);
}

static void set_focused(int i) {
	if (focused_app != i) {
		int old_focused = focused_app;
		focused_app = i;
		redraw_apps(1);
		if (old_focused >= 0) {
			yutani_flip_region(yctx, wina, 0, ICON_TOP_Y + ICON_SPACING_Y * old_focused, ICON_WIDTH + 2 * EXTRA_WIDTH, ICON_SPACING_Y);
		}
		if (focused_app >= 0) {
			yutani_flip_region(yctx, wina, 0, ICON_TOP_Y + ICON_SPACING_Y * focused_app, ICON_WIDTH + 2 * EXTRA_WIDTH, ICON_SPACING_Y);
		}
	}
}

#define ANIMATION_TICKS 500
#define SCALE_MAX 2.0f
static void play_animation(int i) {

	struct timeval start;
	gettimeofday(&start, NULL);

	sprite_t * sprite = applications[i].icon_sprite;

	int x = ICON_X;
	int y = ICON_TOP_Y + ICON_SPACING_Y * i;

	while (1) {
		uint32_t tick;
		struct timeval t;
		gettimeofday(&t, NULL);

		uint32_t sec_diff = t.tv_sec - start.tv_sec;
		uint32_t usec_diff = t.tv_usec - start.tv_usec;

		if (t.tv_usec < start.tv_usec) {
			sec_diff -= 1;
			usec_diff = (1000000 + t.tv_usec) - start.tv_usec;
		}

		tick = (uint32_t)(sec_diff * 1000 + usec_diff / 1000);
		if (tick > ANIMATION_TICKS) break;

		float percent = (float)tick / (float)ANIMATION_TICKS;
		float scale = 1.0f + (SCALE_MAX - 1.0f) * percent;
		float opacity = 1.0f - 1.0f * percent;

		int offset_x = sprite->width / 2 - scale * (sprite->width / 2);
		int offset_y = sprite->height / 2 - scale * (sprite->height / 2);

		redraw_apps(0);
		draw_sprite_scaled_alpha(ctx, sprite, x + offset_x, y + offset_y, sprite->width * scale, sprite->height * scale, opacity);
		flip(ctx);
		yutani_flip_region(yctx, wina, 0, y - sprite->height, x + sprite->width * 2, y + sprite->height * 2);

	}

	redraw_apps(1);
	yutani_flip_region(yctx, wina, 0, y - sprite->height, x + sprite->width * 2, y + sprite->height * 2);
}

static void wallpaper_check_click(struct yutani_msg_window_mouse_event * evt) {
	if (evt->command == YUTANI_MOUSE_EVENT_CLICK) {
		if (evt->new_x > ICON_X && evt->new_x < ICON_X + ICON_WIDTH) {
			uint32_t i = 0;
			while (1) {
				if (!applications[i].icon) {
					break;
				}
				if ((evt->new_y > ICON_TOP_Y + ICON_SPACING_Y * i) &&
					(evt->new_y < ICON_TOP_Y + ICON_SPACING_Y + ICON_SPACING_Y * i)) {
					launch_application(applications[i].appname);
					play_animation(i);
				}
				++i;
			}
			/* Within the icon range */
		}
	} else if (evt->command == YUTANI_MOUSE_EVENT_MOVE || evt->command == YUTANI_MOUSE_EVENT_ENTER) {
		if (evt->new_x > 0 && evt->new_x < ICON_X + ICON_WIDTH + EXTRA_WIDTH) {
			uint32_t i = 0;
			while (1) {
				if (!applications[i].icon) {
					set_focused(-1);
					break;
				}
				if ((evt->new_y > ICON_TOP_Y + ICON_SPACING_Y * i) &&
					(evt->new_y < ICON_TOP_Y + ICON_SPACING_Y + ICON_SPACING_Y * i)) {
					set_focused(i);
					break;
				}
				++i;
			}
			/* Within the icon range */
		} else {
			set_focused(-1);
		}
	} else if (evt->command == YUTANI_MOUSE_EVENT_LEAVE) {
		set_focused(-1);
	}
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

sprite_t * load_wallpaper(void) {
	sprite_t * o_wallpaper = NULL;

	sprite_t * wallpaper_tmp = calloc(1,sizeof(sprite_t));

	sprintf(f_name, "%s/.desktop.conf", getenv("HOME"));

	confreader_t * conf = confreader_load(f_name);

	load_sprite_png(wallpaper_tmp, confreader_getd(conf, "", "wallpaper", DEFAULT_WALLPAPER));

	confreader_free(conf);

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

void sig_usr(int sig) {
	sprite_t * new_wallpaper = load_wallpaper();

	struct timeval start;
	gettimeofday(&start, NULL);

	while (1) {
		uint32_t tick;
		struct timeval t;
		gettimeofday(&t, NULL);

		uint32_t sec_diff = t.tv_sec - start.tv_sec;
		uint32_t usec_diff = t.tv_usec - start.tv_usec;

		if (t.tv_usec < start.tv_usec) {
			sec_diff -= 1;
			usec_diff = (1000000 + t.tv_usec) - start.tv_usec;
		}

		tick = (uint32_t)(sec_diff * 1000 + usec_diff / 1000);
		if (tick > ANIMATION_TICKS) break;

		float percent = (float)tick / (float)ANIMATION_TICKS;

		draw_sprite(ctx, wallpaper, 0, 0);
		draw_sprite_alpha(ctx, new_wallpaper, 0, 0, percent);
		redraw_apps_x(1);
		yutani_flip(yctx, wina);
	}

	free(wallpaper);
	wallpaper = new_wallpaper;
	draw_sprite(ctx, wallpaper, 0, 0);
	redraw_apps_x(1);

	yutani_flip(yctx, wina);
}

int main (int argc, char ** argv) {
	yctx = yutani_init();

	width  = yctx->display_width;
	height = yctx->display_height;

	/* Initialize hashmap for icon cache */
	icon_cache = hashmap_create(10);

	{ /* Generic fallback icon */
		sprite_t * app_icon = malloc(sizeof(sprite_t));
		load_sprite_png(app_icon, "/usr/share/icons/48/applications-generic.png");
		hashmap_set(icon_cache, "generic", app_icon);
	}

	sprintf(f_name, "%s/.desktop", getenv("HOME"));
	FILE * f = fopen(f_name, "r");
	if (!f) {
		f = fopen("/etc/default.desktop", "r");
	}
	read_applications(f);

	/* Load applications */
	uint32_t i = 0;
	while (applications[i].icon) {
		applications[i].icon_sprite = icon_get(applications[i].icon);
		++i;
	}

	wallpaper = load_wallpaper();

	wina = yutani_window_create(yctx, width, height);
	assert(wina);
	yutani_set_stack(yctx, wina, YUTANI_ZORDER_BOTTOM);
	ctx = init_graphics_yutani_double_buffer(wina);
	init_shmemfonts();

	redraw_apps(1);
	yutani_flip(yctx, wina);

	/* Set SIGUSR1 to reload wallpaper. */
	signal(SIGUSR1, sig_usr);

	while (_continue) {
		yutani_msg_t * m = yutani_poll(yctx);
		waitpid(-1, NULL, WNOHANG);
		if (m) {
			switch (m->type) {
				case YUTANI_MSG_WINDOW_MOUSE_EVENT:
					wallpaper_check_click((struct yutani_msg_window_mouse_event *)m->data);
					break;
				case YUTANI_MSG_SESSION_END:
					_continue = 0;
					break;
			}
			free(m);
		}
	}

	yutani_close(yctx, wina);

	return 0;
}
