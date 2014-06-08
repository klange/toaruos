/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2014 Kevin Lange
 */
/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Wallpaper renderer.
 *
 */
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <math.h>
#include <sys/wait.h>

#include "lib/yutani.h"
#include "lib/graphics.h"
#include "lib/shmemfonts.h"
#include "lib/hashmap.h"

#define ICON_X         24
#define ICON_TOP_Y     40
#define ICON_SPACING_Y 74
#define ICON_WIDTH     48
#define EXTRA_WIDTH    24

static uint16_t win_width;
static uint16_t win_height;
static yutani_t * yctx;
static yutani_window_t * wina;
static gfx_context_t * ctx;
static sprite_t * wallpaper;
static hashmap_t * icon_cache;

static int center_x(int x) {
	return (win_width - x) / 2;
}

static int center_y(int y) {
	return (win_height - y) / 2;
}

typedef struct {
	char * icon;
	char * appname;
	char * title;
	sprite_t * icon_sprite;
} application_t;

#if 0
static application_t applications[] = {
	{"utilities-terminal", "terminal", "Terminal", NULL},
	{"applications-painting", "draw", "Draw!", NULL},
	{"applications-simulation", "game", "RPG Demo", NULL},
	{"julia", "julia", "Julia Fractals", NULL},
	{NULL, NULL, NULL, NULL},
};
#else
static application_t * applications = NULL;
#endif

static volatile int _continue = 1;

/* Default search paths for icons, in order of preference */
static char * icon_directories[] = {
	"/usr/share/icons/48",
	"/usr/share/icons/24",
	"/usr/share/icons",
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
			fprintf(stderr, "Checking %s for icon\n", path);
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


void launch_application(char * app) {
	if (!fork()) {
		printf("Starting %s\n", app);
		char * args[] = {"/bin/sh", "-c", app, NULL};
		execvp(args[0], args);
		exit(1);
	}
}

char * next_run_activate = NULL;
int focused_app = -1;

static void redraw_apps(void) {
	draw_sprite(ctx, wallpaper, 0, 0);

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

	flip(ctx);
}

static void set_focused(int i) {
	if (focused_app != i) {
		int old_focused = focused_app;
		focused_app = i;
		redraw_apps();
		if (old_focused >= 0) {
			yutani_flip_region(yctx, wina, 0, ICON_TOP_Y + ICON_SPACING_Y * old_focused, ICON_WIDTH + 2 * EXTRA_WIDTH, ICON_SPACING_Y);
		}
		if (focused_app >= 0) {
			yutani_flip_region(yctx, wina, 0, ICON_TOP_Y + ICON_SPACING_Y * focused_app, ICON_WIDTH + 2 * EXTRA_WIDTH, ICON_SPACING_Y);
		}
	}
}

static void wallpaper_check_click(struct yutani_msg_window_mouse_event * evt) {
	if (evt->command == YUTANI_MOUSE_EVENT_CLICK) {
		printf("Click!\n");
		if (evt->new_x > ICON_X && evt->new_x < ICON_X + ICON_WIDTH) {
			uint32_t i = 0;
			while (1) {
				if (!applications[i].icon) {
					break;
				}
				if ((evt->new_y > ICON_TOP_Y + ICON_SPACING_Y * i) &&
					(evt->new_y < ICON_TOP_Y + ICON_SPACING_Y + ICON_SPACING_Y * i)) {
					printf("Launching application \"%s\"...\n", applications[i].title);
					launch_application(applications[i].appname);
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

		fprintf(stderr, "Icon: %s %s %s\n", icon, name, title);

		applications[i].icon = strdup(icon);
		applications[i].appname = strdup(name);
		applications[i].title = strdup(title);

		i++;
	}

	fclose(f);
}

int main (int argc, char ** argv) {
	yctx = yutani_init();

	int width  = yctx->display_width;
	int height = yctx->display_height;

	sprite_t * wallpaper_tmp = malloc(sizeof(sprite_t));

	char f_name[512];
	sprintf(f_name, "%s/.wallpaper.png", getenv("HOME"));
	FILE * f = fopen(f_name, "r");
	if (f) {
		fclose(f);
		load_sprite_png(wallpaper_tmp, f_name);
	} else {
		load_sprite_png(wallpaper_tmp, "/usr/share/wallpaper.png");
	}

	/* Initialize hashmap for icon cache */
	icon_cache = hashmap_create(10);

	{ /* Generic fallback icon */
		sprite_t * app_icon = malloc(sizeof(sprite_t));
		load_sprite_png(app_icon, "/usr/share/icons/48/applications-generic.png");
		hashmap_set(icon_cache, "generic", app_icon);
	}

	sprintf(f_name, "%s/.desktop", getenv("HOME"));
	f = fopen(f_name, "r");
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

	float x = (float)width  / (float)wallpaper_tmp->width;
	float y = (float)height / (float)wallpaper_tmp->height;

	int nh = (int)(x * (float)wallpaper_tmp->height);
	int nw = (int)(y * (float)wallpaper_tmp->width);;

	wallpaper = create_sprite(width, height, ALPHA_OPAQUE);
	gfx_context_t * tmp = init_graphics_sprite(wallpaper);

	if (nw > width) {
		draw_sprite_scaled(tmp, wallpaper_tmp, (width - nw) / 2, 0, nw, height);
	} else {
		draw_sprite_scaled(tmp, wallpaper_tmp, 0, (height - nh) / 2, width, nh);
	}

	free(tmp);

	win_width = width;
	win_height = height;

	wina = yutani_window_create(yctx, width, height);
	assert(wina);
	yutani_set_stack(yctx, wina, YUTANI_ZORDER_BOTTOM);
	ctx = init_graphics_yutani_double_buffer(wina);
	init_shmemfonts();

	redraw_apps();
	yutani_flip(yctx, wina);

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
