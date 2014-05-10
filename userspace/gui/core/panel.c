/* vim: tabstop=4 shiftwidth=4 noexpandtab
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
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/utsname.h>

/* TODO: Move all of the configurable rendering
 * parameters up here */
#define PANEL_HEIGHT 28
#define FONT_SIZE 14
#define TIME_LEFT 108
#define DATE_WIDTH 70

#include "lib/pthread.h"
#include "lib/yutani.h"
#include "lib/graphics.h"
#include "lib/shmemfonts.h"
#include "lib/hashmap.h"
#include "lib/spinlock.h"

static gfx_context_t * ctx;
static yutani_t * yctx;
static yutani_window_t * panel;
static list_t * window_list = NULL;
static volatile int lock = 0;
static volatile int drawlock = 0;

static hashmap_t * icon_cache;

static size_t bg_size;
static char * bg_blob;

static int width;
static int height;

static sprite_t * sprite_panel;
static sprite_t * sprite_logout;

static int center_x(int x) {
	return (width - x) / 2;
}

static int center_y(int y) {
	return (height - y) / 2;
}

static void redraw(void);

static volatile int _continue = 1;

/* XXX Stores some quick access information about the window list */
static int icon_lefts[20] = {0};
static int icon_wids[20] = {0};
static int focused_app = -1;
static int active_window = -1;

struct window_ad {
	yutani_wid_t wid;
	uint32_t flags;
	char * name;
	char * icon;
	char * strings;
};

/* Handle SIGINT by telling other threads (clock) to shut down */
static void sig_int(int sig) {
	printf("Received shutdown signal in panel!\n");
	_continue = 0;
}

/* Update the hover-focus window */
static void set_focused(int i) {
	if (focused_app != i) {
		focused_app = i;
		redraw();
	}
}

/* Callback for mouse events */
static void panel_check_click(struct yutani_msg_window_mouse_event * evt) {
	if (evt->command == YUTANI_MOUSE_EVENT_CLICK) {
		/* Up-down click */
		if (evt->new_x >= width - 24 ) {
			yutani_session_end(yctx);
			_continue = 0;
		} else {
			for (int i = 0; i < 18; ++i) {
				if (evt->new_x >= icon_lefts[i] && evt->new_x < icon_lefts[i+1]) {
					if (icon_wids[i]) {
						yutani_focus_window(yctx, icon_wids[i]);
					}
					break;
				}
			}
		}
	} else if (evt->command == YUTANI_MOUSE_EVENT_MOVE || evt->command == YUTANI_MOUSE_EVENT_ENTER) {
		/* Movement, or mouse entered window */
		if (evt->new_y < PANEL_HEIGHT) {
			for (int i = 0; i < 18; ++i) {
				if (icon_lefts[i] == 0) {
					set_focused(-1);
					break;
				}
				if (evt->new_x >= icon_lefts[i] && evt->new_x < icon_lefts[i+1]) {
					set_focused(i);
					break;
				}
			}
		} else {
			set_focused(-1);
		}
	} else if (evt->command == YUTANI_MOUSE_EVENT_LEAVE) {
		/* Mouse left panel window */
		set_focused(-1);
	}
}

static void launch_application(char * app) {
	if (!fork()) {
		char * args[] = {app, NULL};
		execvp(args[0], args);
		exit(1);
	}
}

static void handle_key_event(struct yutani_msg_key_event * ke) {
	if ((ke->event.modifiers & KEY_MOD_LEFT_CTRL) &&
		(ke->event.modifiers & KEY_MOD_LEFT_ALT) &&
		(ke->event.keycode == 't') &&
		(ke->event.action == KEY_ACTION_DOWN)) {

		launch_application("terminal");
	}

	if ((ke->event.modifiers & KEY_MOD_LEFT_ALT) &&
		(ke->event.keycode == '\t') &&
		(ke->event.action == KEY_ACTION_DOWN)) {

		int direction = (ke->event.modifiers & KEY_MOD_LEFT_SHIFT) ? -1 : 1;

		int new_focused = active_window + direction;
		if (new_focused < 0) {
			new_focused = 0;
			for (int i = 0; i < 18; i++) {
				if (icon_wids[i+1] == 0) {
					new_focused = i;
					break;
				}
			}
		}
		if (icon_wids[new_focused] == 0) {
			new_focused = 0;
		}

		yutani_focus_window(yctx, icon_wids[new_focused]);

	}

}

/* Default search paths for icons, in order of preference */
static char * icon_directories[] = {
	"/usr/share/icons/24",
	"/usr/share/icons/48",
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

static void redraw(void) {
	spin_lock(&drawlock);

	struct timeval now;
	int last = 0;
	struct tm * timeinfo;
	char   buffer[80];

	uint32_t txt_color = rgb(230,230,230);
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

	/* TODO: Future applications menu */
	set_font_face(FONT_SANS_SERIF_BOLD);
	set_font_size(14);
	draw_string(ctx, 10, 18, txt_color, "Applications");

	/* Now draw the window list */
	int i = 0, j = 0;
	spin_lock(&lock);
	if (window_list) {
		foreach(node, window_list) {
			struct window_ad * ad = node->value;
			char * s = ad->name;

			set_font_face(FONT_SANS_SERIF);
			set_font_size(13);

			int w = 26 + draw_string_width(s) + 20;

			/* Hilight the focused window */
			if (ad->flags & 1) {
				/* This is the focused window */
				for (int y = 0; y < 24; ++y) {
					for (int x = 135 + i; x < 135 + i + w - 10; ++x) {
						GFX(ctx, x, y) = alpha_blend_rgba(GFX(ctx, x, y), premultiply(rgba(72, 167, 255, ((24-y)*160)/24)));
					}
				}
			}

			/* Get the icon for this window */
			sprite_t * icon = icon_get(ad->icon);

			/* Draw it, scaled if necessary */
			if (icon->width == 24) {
				draw_sprite(ctx, icon, 140 + i, 0);
			} else {
				draw_sprite_scaled(ctx, icon, 140 + i, 0, 24, 24);
			}

			/* Then draw the window title, with appropriate color */
			if (j == focused_app) {
				/* Current hilighted - title should be a light blue */
				draw_string(ctx, 140 + i + 26, 18, rgb(142,216,255), s);
			} else {
				if (ad->flags & 1) {
					/* Top window should be white */
					draw_string(ctx, 140 + i + 26, 18, rgb(255,255,255), s);
				} else {
					/* Otherwise, off white */
					draw_string(ctx, 140 + i + 26, 18, txt_color, s);
				}
			}

			/* XXX This keeps track of how far left each window list item is
			 * so we can map clicks up in the mouse callback. */
			if (j < 18) {
				icon_lefts[j] = 140 + i;
				j++;
			}
			i += w;
		}
		if (j < 19) {
			icon_lefts[j] = 140 + i;
			icon_lefts[j+1] = 0;
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

	int i = 0;
	int new_active_window = 0;
	/*
	 * Update each of the wid entries in our array so we can map
	 * clicks to window focus events for each window
	 */
	foreach(node, new_window_list) {
		struct window_ad * ad = node->value;
		if (i < 19) {
			icon_wids[i] = ad->wid;
			icon_wids[i+1] = 0;
			if (ad->flags & 1) {
				new_active_window = i;
			}
		}
		i++;
	}
	active_window = new_active_window;

	/* Then free up the old list and replace it with the new list */
	spin_lock(&lock);
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
