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
#include <limits.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <sys/wait.h>

#include "lib/pthread.h"
#include "lib/yutani.h"
#include "lib/graphics.h"
#include "lib/shmemfonts.h"
#include "lib/hashmap.h"
#include "lib/spinlock.h"

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
#define LEFT_BOUND (width - TIME_LEFT - DATE_WIDTH - ICON_PADDING)

static gfx_context_t * ctx;
static yutani_t * yctx;
static yutani_window_t * panel;
static gfx_context_t * actx;
static yutani_window_t * alttab;
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

/* Windows, indexed by list order */
static struct window_ad * ads_by_l[MAX_WINDOW_COUNT+1] = {NULL};
/* Windows, indexed by z-order */
static struct window_ad * ads_by_z[MAX_WINDOW_COUNT+1] = {NULL};

static int focused_app = -1;
static int active_window = -1;
static int was_tabbing = 0;
static int new_focused = -1;

static int title_width = 0;

static sprite_t * icon_get(char * name);

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

	if ((was_tabbing) && (ke->event.keycode == 0) &&
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

			/* And move it to the top layer */
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

	/* This lets us receive all just-modifier key releases */
	yutani_key_bind(yctx, 0, 0, YUTANI_BIND_PASSTHROUGH);

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
