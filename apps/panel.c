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
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/fswait.h>
#include <sys/shm.h>

/* auto-dep: export-dynamic */
#include <dlfcn.h>

#include <toaru/yutani.h>
#include <toaru/yutani-internal.h>
#include <toaru/graphics.h>
#include <toaru/hashmap.h>
#include <toaru/icon_cache.h>
#include <toaru/menu.h>
#include <toaru/text.h>

/* Several theming defines are in here */
#include <toaru/panel.h>

/* These are local to the core panel, so we don't need to put them in the header */
#define ALTTAB_WIDTH  250
#define ALTTAB_HEIGHT 200
#define ALTTAB_BACKGROUND premultiply(rgba(0,0,0,150))
#define ALTTAB_OFFSET 10
#define ALTTAB_WIN_SIZE 140

#define ALTF2_WIDTH 400
#define ALTF2_HEIGHT 200

/* How many windows we can support in the advertisement lift before truncating it */
#define MAX_WINDOW_COUNT 100
/* Height of the panel window */
#define PANEL_HEIGHT 27
/* How far down dropdown menus should be shown */
#define DROPDOWN_OFFSET PANEL_HEIGHT
/* How much padding should be assured on the left and right of the screen for menus */
#define MENU_PAD 4

static struct PanelContext panel_context;

static gfx_context_t * ctx = NULL;
static yutani_window_t * panel = NULL;

static gfx_context_t * actx = NULL;
static yutani_window_t * alttab = NULL;

static gfx_context_t * a2ctx = NULL;
static yutani_window_t * alt_f2 = NULL;

static size_t bg_size;
static char * bg_blob;

/* External interface for widgets */
list_t * window_list = NULL;
yutani_t * yctx;
int width;
int height;

list_t * widgets_enabled = NULL;

/* Windows, indexed by z-order */
struct window_ad * ads_by_z[MAX_WINDOW_COUNT+1] = {NULL};

int active_window = -1;

static int was_tabbing = 0;
static int new_focused = -1;

static void widgets_layout(void);

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
	return (alttab->width - x) / 2;
}

static int center_x_a2(int x) {
	return (ALTF2_WIDTH - x) / 2;
}

static volatile int _continue = 1;

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

void launch_application(char * app) {
	if (!fork()) {
		printf("Starting %s\n", app);
		char * args[] = {"/bin/sh", "-c", app, NULL};
		execvp(args[0], args);
		exit(1);
	}
}

/* Callback for mouse events */
static void panel_check_click(struct yutani_msg_window_mouse_event * evt) {
	static struct PanelWidget * old_target = NULL;

	if (evt->wid == panel->wid) {

		/* Figure out what widget this belongs to */
		struct PanelWidget * target = NULL;
		if (evt->new_y >= 0 && evt->new_y < PANEL_HEIGHT) {
			foreach(widget_node, widgets_enabled) {
				struct PanelWidget * widget = widget_node->value;
				if (evt->new_x >= widget->left && evt->new_x < widget->left + widget->width) {
					target = widget;
					break;
				}
			}
		}

		int needs_redraw = 0;

		if (evt->command == YUTANI_MOUSE_EVENT_CLICK || _close_enough(evt)) {
			if (target) needs_redraw |= target->click(target, evt);
		} else if (evt->buttons & YUTANI_MOUSE_BUTTON_RIGHT) {
			if (target) needs_redraw |= target->right_click(target, evt);
		} else if (evt->command == YUTANI_MOUSE_EVENT_MOVE || evt->command == YUTANI_MOUSE_EVENT_ENTER) {
			if (old_target && target != old_target) needs_redraw |= old_target->leave(old_target, evt);
			if (target && target != old_target) needs_redraw |= target->enter(target, evt);
			if (target) needs_redraw |= target->move(target, evt);
			old_target = target;
		} else if (evt->command == YUTANI_MOUSE_EVENT_LEAVE) {
			if (old_target) needs_redraw |= old_target->leave(old_target, evt);
			old_target = NULL;
		}

		if (needs_redraw) redraw();
	}
}

static char altf2_buffer[1024] = {0};
static unsigned int altf2_collected = 0;

static void close_altf2(void) {
	free(a2ctx->backbuffer);
	free(a2ctx);

	altf2_buffer[0] = 0;
	altf2_collected = 0;

	yutani_close(yctx, alt_f2);
	alt_f2 = NULL;
}

static void redraw_altf2(void) {
	draw_fill(a2ctx, 0);
	draw_rounded_rectangle(a2ctx,0,0, ALTF2_WIDTH, ALTF2_HEIGHT, 11, premultiply(rgba(120,120,120,150)));
	draw_rounded_rectangle(a2ctx,1,1, ALTF2_WIDTH-2, ALTF2_HEIGHT-2, 10, ALTTAB_BACKGROUND);

	tt_set_size(panel_context.font, 20);
	int t = tt_string_width(panel_context.font, altf2_buffer);
	tt_draw_string(a2ctx, panel_context.font, center_x_a2(t), 80, altf2_buffer, rgb(255,255,255));

	flip(a2ctx);
	yutani_flip(yctx, alt_f2);
}

static void redraw_alttab(void) {
	if (!actx) return;
	while (new_focused > -1 && !ads_by_z[new_focused]) {
		new_focused--;
	}
	if (new_focused == -1) {
		/* Stop tabbing */
		was_tabbing = 0;
		free(actx->backbuffer);
		free(actx);
		actx = NULL;
		yutani_close(yctx, alttab);
		return;
	}

	/* How many windows do we have? */
	unsigned int window_count = 0;
	while (ads_by_z[window_count]) window_count++;

#define ALTTAB_COLUMNS 5

	/* How many rows should that be? */
	int rows = (window_count - 1) / ALTTAB_COLUMNS + 1;

	/* How many columns? */
	int columns = (rows == 1) ? window_count : ALTTAB_COLUMNS;

	/* How much padding on the last row? */
	int last_row = (window_count % columns) ? ((ALTTAB_WIN_SIZE + 20) * (columns - (window_count % columns))) / 2  : 0;

	/* Is the window the right size? */
	unsigned int expected_width = columns * (ALTTAB_WIN_SIZE + 20) + 40;
	unsigned int expected_height = rows * (ALTTAB_WIN_SIZE + 20) + 60;

	if (alttab->width != expected_width || alttab->height != expected_height) {
		yutani_window_resize(yctx, alttab, expected_width, expected_height);
		return;
	}

	/* Draw the background, right now just a dark semi-transparent box */
	draw_fill(actx, 0);
	draw_rounded_rectangle(actx,0,0, alttab->width, alttab->height, 11, premultiply(rgba(120,120,120,150)));
	draw_rounded_rectangle(actx,1,1, alttab->width-2, alttab->height-2, 10, ALTTAB_BACKGROUND);

	for (unsigned int i = 0; i < window_count; ++i) {
		if (!ads_by_z[i]) continue;
		struct window_ad * ad = ads_by_z[i];

		/* Figure out grid alignment for this element */
		int pos_x = ((window_count - i - 1) % ALTTAB_COLUMNS) * (ALTTAB_WIN_SIZE + 20) + 20;
		int pos_y = ((window_count - i - 1) / ALTTAB_COLUMNS) * (ALTTAB_WIN_SIZE + 20) + 20;

		if ((window_count - i - 1) / ALTTAB_COLUMNS == (unsigned int)rows - 1) {
			pos_x += last_row;
		}

		if (i == (unsigned int)new_focused) {
			draw_rounded_rectangle(actx, pos_x, pos_y, ALTTAB_WIN_SIZE + 20, ALTTAB_WIN_SIZE + 20, 7, premultiply(rgba(170,170,170,150)));
		}

		/* try very hard to get a window texture */
		char key[1024];
		YUTANI_SHMKEY_EXP(yctx->server_ident, key, 1024, ad->bufid);
		size_t size = 0;
		uint32_t * buf =  shm_obtain(key, &size);

		if (buf && size >= ad->width * ad->height * 4) {
			sprite_t tmp;
			tmp.width = ad->width;
			tmp.height = ad->height;
			tmp.bitmap = buf;

			int ox = 0;
			int oy = 0;
			int sw, sh;
			if (tmp.width > tmp.height) {
				sw = ALTTAB_WIN_SIZE;
				sh = tmp.height * ALTTAB_WIN_SIZE / tmp.width;
				oy = (ALTTAB_WIN_SIZE - sh) / 2;
			} else {
				sh = ALTTAB_WIN_SIZE;
				sw = tmp.width * ALTTAB_WIN_SIZE / tmp.height;
				ox = (ALTTAB_WIN_SIZE - sw) / 2;
			}
			draw_sprite_scaled(actx, &tmp,
				pos_x + ox + 10,
				pos_y + oy + 10,
				sw, sh);

			shm_release(key);

			sprite_t * icon = icon_get_48(ad->icon);
			draw_sprite(actx, icon, pos_x + 10 + ALTTAB_WIN_SIZE - 50, pos_y + 10 + ALTTAB_WIN_SIZE - 50);
		} else {
			sprite_t * icon = icon_get_48(ad->icon);
			draw_sprite(actx, icon, pos_x + 10 + (ALTTAB_WIN_SIZE - 48) / 2, pos_y + 10 + (ALTTAB_WIN_SIZE - 48) / 2);
		}

	}

	{
		struct window_ad * ad = ads_by_z[new_focused];
		int t;
		char * title = tt_ellipsify(ad->name, 16, panel_context.font, alttab->width - 20, &t);
		tt_set_size(panel_context.font, 16);
		tt_draw_string(actx, panel_context.font, center_x_a(t), rows * (ALTTAB_WIN_SIZE + 20) + 44, title, rgb(255,255,255));
		free(title);
	}

	flip(actx);
	yutani_window_move(yctx, alttab, center_x(alttab->width), center_y(alttab->height));
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

void launch_application_menu(struct MenuEntry * self) {
	struct MenuEntry_Normal * _self = (void *)self;

	if (!strcmp((char *)_self->action,"log-out")) {
		/* Spin off a thread for this */
		pthread_create(&_waiter_thread, NULL, logout_prompt_waiter, NULL);
	} else {
		launch_application((char *)_self->action);
	}
}

static void handle_key_event(struct yutani_msg_key_event * ke) {
	/**
	 * Is the Alt+F2 command entry window open?
	 * Then we should capture all typing and use
	 * direct it to the 'input box'.
	 */
	if (alt_f2 && ke->wid == alt_f2->wid) {
		if (ke->event.action == KEY_ACTION_DOWN) {
			/* Escape = cancel */
			if (ke->event.keycode == KEY_ESCAPE) {
				close_altf2();
				return;
			}

			/* Backspace */
			if (ke->event.key == '\b') {
				if (altf2_collected) {
					altf2_buffer[altf2_collected-1] = '\0';
					altf2_collected--;
					redraw_altf2();
				}
				return;
			}

			/* Enter */
			if (ke->event.key == '\n') {
				/* execute */
				launch_application(altf2_buffer);
				close_altf2();
				return;
			}

			/* Some other key */
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

	/* Ctrl-Alt-T = Open a new terminal */
	if ((ke->event.modifiers & KEY_MOD_LEFT_CTRL) &&
		(ke->event.modifiers & KEY_MOD_LEFT_ALT) &&
		(ke->event.keycode == 't') &&
		(ke->event.action == KEY_ACTION_DOWN)) {

		launch_application("exec terminal");
		return;
	}

	/* Ctrl-F11 = Toggle visibility of the panel */
	if ((ke->event.modifiers & KEY_MOD_LEFT_CTRL) &&
		(ke->event.keycode == KEY_F11) &&
		(ke->event.action == KEY_ACTION_DOWN)) {

		fprintf(stderr, "[panel] Toggling visibility.\n");
		toggle_hide_panel();
		return;
	}

	/* Alt-F2 = Show the command entry window */
	if ((ke->event.modifiers & KEY_MOD_LEFT_ALT) &&
		(ke->event.keycode == KEY_F2) &&
		(ke->event.action == KEY_ACTION_DOWN)) {
		/* show menu */
		if (!alt_f2) {
			alt_f2 = yutani_window_create_flags(yctx, ALTF2_WIDTH, ALTF2_HEIGHT, YUTANI_WINDOW_FLAG_BLUR_BEHIND);
			yutani_window_update_shape(yctx, alt_f2, 5);
			yutani_window_move(yctx, alt_f2, center_x(ALTF2_WIDTH), center_y(ALTF2_HEIGHT));
			a2ctx = init_graphics_yutani_double_buffer(alt_f2);
			redraw_altf2();
		}
	}

	/* Maybe a plugin wants to handle this key bind */
	foreach(widget_node, widgets_enabled) {
		struct PanelWidget * widget = widget_node->value;
		widget->onkey(widget, ke);
	}

	/* Releasing Alt when the Alt-Tab switcher is visible */
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

	/* Alt-Tab = Switch windows */
	if ((ke->event.modifiers & KEY_MOD_LEFT_ALT) &&
		(ke->event.keycode == '\t') &&
		(ke->event.action == KEY_ACTION_DOWN)) {

		/* Alt-Tab and Alt-Shift-Tab should go in alternate directions */
		int direction = (ke->event.modifiers & KEY_MOD_LEFT_SHIFT) ? 1 : -1;

		/* Are there no windows to switch? */
		if (window_list->length < 1) return;

		/* Figure out the new focused window */
		if (was_tabbing) {
			new_focused = new_focused + direction;
		} else {
			new_focused = active_window + direction;
			/* Create tab window */
			alttab = yutani_window_create_flags(yctx, ALTTAB_WIDTH, ALTTAB_HEIGHT,
				YUTANI_WINDOW_FLAG_NO_STEAL_FOCUS | YUTANI_WINDOW_FLAG_NO_ANIMATION | YUTANI_WINDOW_FLAG_BLUR_BEHIND);
			yutani_window_update_shape(yctx, alttab, 5);

			yutani_set_stack(yctx, alttab, YUTANI_ZORDER_OVERLAY);

			/* Initialize graphics context against the window */
			actx = init_graphics_yutani_double_buffer(alttab);
		}

		/* Handle wraparound */
		if (new_focused < 0) {
			new_focused = 0;
			for (int i = 0; i < MAX_WINDOW_COUNT; i++) {
				if (ads_by_z[i+1] == NULL) {
					new_focused = i;
					break;
				}
			}
		} else if (ads_by_z[new_focused] == NULL) {
			if (ads_by_z[0]) {
				new_focused = 0;
			} else {
				new_focused = -1;
			}
		}

		was_tabbing = 1;
		redraw_alttab();
	}
}

void redraw(void) {
	memcpy(ctx->backbuffer, bg_blob, bg_size);

	foreach(widget_node, widgets_enabled) {
		struct PanelWidget * widget = widget_node->value;
		gfx_context_t * inner = init_graphics_subregion(ctx, widget->left, 0, widget->width, PANEL_HEIGHT);
		widget->draw(widget, inner);
		free(inner);
	}

	/* Flip */
	flip(ctx);
	yutani_flip(yctx, panel);
}

static void update_window_list(void) {
	yutani_query_windows(yctx);

	list_t * new_window_list = list_create();

	ads_by_z[0] = NULL;

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

	/* And redraw the panel */
	redraw();
}

static void redraw_panel_background(gfx_context_t * ctx, int width, int height) {
	draw_fill(ctx, rgba(0,0,0,0xF2));
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

	widgets_layout();
	update_window_list();
}

static void bind_keys(void) {

	/* Cltr-Alt-T = launch terminal */
	yutani_key_bind(yctx, 't', KEY_MOD_LEFT_CTRL | KEY_MOD_LEFT_ALT, YUTANI_BIND_STEAL);

	/* Alt+Tab = app switcher*/
	yutani_key_bind(yctx, '\t', KEY_MOD_LEFT_ALT, YUTANI_BIND_STEAL);
	yutani_key_bind(yctx, '\t', KEY_MOD_LEFT_ALT | KEY_MOD_LEFT_SHIFT, YUTANI_BIND_STEAL);

	/* Ctrl-F11 = toggle panel visibility */
	yutani_key_bind(yctx, KEY_F11, KEY_MOD_LEFT_CTRL, YUTANI_BIND_STEAL);

	/* Alt+F2 = show app runner */
	yutani_key_bind(yctx, KEY_F2, KEY_MOD_LEFT_ALT, YUTANI_BIND_STEAL);

	/* This lets us receive all just-modifier key releases */
	yutani_key_bind(yctx, KEY_LEFT_ALT, 0, YUTANI_BIND_PASSTHROUGH);

}

static void sig_usr2(int sig) {
	yutani_set_stack(yctx, panel, YUTANI_ZORDER_TOP);
	yutani_flip(yctx, panel);
	bind_keys();
	signal(SIGUSR2, sig_usr2);
}

static int mouse_event_ignore(struct PanelWidget * this, struct yutani_msg_window_mouse_event * evt) {
	return 0;
}

static int widget_enter_generic(struct PanelWidget * this, struct yutani_msg_window_mouse_event * evt) {
	this->highlighted = 1; /* Highlighted */
	return 1;
}

static int widget_leave_generic(struct PanelWidget * this, struct yutani_msg_window_mouse_event * evt) {
	this->highlighted = 0; /* Not highlighted */
	return 1;
}

void panel_highlight_widget(struct PanelWidget * this, gfx_context_t * ctx, int active) {
	if (this->highlighted || active) {
		draw_rounded_rectangle(ctx, 3, 3, ctx->width - 6, ctx->height - 6, 11, premultiply(rgba(120,120,120,active ? 180 : 150)));
	}
}

static int widget_draw_generic(struct PanelWidget * this, gfx_context_t * ctx) {
	draw_rounded_rectangle(
		ctx, 0, 0, ctx->width, ctx->height, 7, premultiply(rgba(120,120,120,150)));
	if (this->highlighted) {
		draw_rounded_rectangle(
			ctx, 1, 1, ctx->width-2, ctx->height-2, 6, premultiply(rgba(120,160,230,220)));
	} else {
		draw_rounded_rectangle(
			ctx, 1, 1, ctx->width-2, ctx->height-2, 6, ALTTAB_BACKGROUND);
	}
	return 0;
}

static int widget_update_generic(struct PanelWidget * this, int * force_updates) {
	return 0;
}

static int widget_onkey_generic(struct PanelWidget * this, struct yutani_msg_key_event * evt) {
	return 0;
}

struct PanelWidget * widget_new(void) {
	struct PanelWidget * out = calloc(1, sizeof(struct PanelWidget));
	out->pctx = &panel_context;
	out->draw = widget_draw_generic;
	out->click = mouse_event_ignore; /* click_generic */
	out->right_click = mouse_event_ignore; /* right_click_generic */
	out->leave = widget_leave_generic;
	out->enter = widget_enter_generic;
	out->update = widget_update_generic;
	out->onkey = widget_onkey_generic;
	out->move = mouse_event_ignore; /* move_generic */
	out->highlighted = 0;
	out->fill = 0;
	return out;
}

static void widgets_layout(void) {
	int total_width = 0;
	int flexible_widgets = 0;
	foreach(node, widgets_enabled) {
		struct PanelWidget * widget = node->value;
		if (widget->fill) {
			flexible_widgets++;
		} else {
			total_width += widget->width;
		}
	}

	/* Now lay out the widgets */
	int x = 0;
	int available = width;
	foreach(node, widgets_enabled) {
		struct PanelWidget * widget = node->value;
		widget->left = x;
		if (widget->fill) {
			widget->width = (available - total_width) / flexible_widgets;
		}
		x += widget->width;
	}
}

static void update_periodic_widgets(int * force_updates) {
	*force_updates = 0;
	int needs_layout = 0;
	foreach(widget_node, widgets_enabled) {
		struct PanelWidget * widget = widget_node->value;
		needs_layout |= widget->update(widget, force_updates);
	}
	if (needs_layout) widgets_layout();
	redraw();
}

int panel_menu_show_at(struct MenuList * menu, int x) {
	int mwidth, mheight, offset;

	/* Calculate the expected size of the menu window. */
	menu_calculate_dimensions(menu, &mheight, &mwidth);

	if (x - mwidth / 2 < MENU_PAD) {
		offset = MENU_PAD;
		menu->flags = (menu->flags & ~MENU_FLAG_BUBBLE) | MENU_FLAG_BUBBLE_LEFT;
	} else if (x + mwidth / 2 > width - MENU_PAD) {
		offset = width - MENU_PAD - mwidth;
		menu->flags = (menu->flags & ~MENU_FLAG_BUBBLE) | MENU_FLAG_BUBBLE_RIGHT;
	} else {
		offset = x - mwidth / 2;
		menu->flags = (menu->flags & ~MENU_FLAG_BUBBLE) | MENU_FLAG_BUBBLE_CENTER;
	}

	menu->flags |= MENU_FLAG_TAIL_POSITION;
	menu->tail_offset = x - offset;

	/* Prepare the menu, which creates the window. */
	menu_prepare(menu, yctx);

	/* If we succeeded, move it to the final offset and display it */
	if (menu->window) {
		yutani_window_move_relative(yctx, menu->window, panel, offset, DROPDOWN_OFFSET);
		yutani_flip(yctx,menu->window);
		return 0;
	}

	return 1;
}

int panel_menu_show(struct PanelWidget * this, struct MenuList * menu) {
	return panel_menu_show_at(menu, this->left + this->width / 2);
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

	/* Shared fonts */
	panel_context.font           = tt_font_from_shm("sans-serif");
	panel_context.font_bold      = tt_font_from_shm("sans-serif.bold");
	panel_context.font_mono      = tt_font_from_shm("monospace");
	panel_context.font_mono_bold = tt_font_from_shm("monospace.bold");

	/* For convenience, store the display size */
	width  = yctx->display_width;
	height = yctx->display_height;

	panel_context.color_text_normal    = rgb(230,230,230);
	panel_context.color_text_hilighted = rgb(142,216,255);
	panel_context.color_text_focused   = rgb(255,255,255);
	panel_context.color_icon_normal    = rgb(230,230,230);
	panel_context.color_special        = rgb(93,163,236);
	panel_context.font_size_default    = 14;
	panel_context.extra_widget_spacing = 12;

	/* Create the panel window */
	panel = yutani_window_create_flags(yctx, width, PANEL_HEIGHT, YUTANI_WINDOW_FLAG_NO_STEAL_FOCUS | YUTANI_WINDOW_FLAG_ALT_ANIMATION);
	panel_context.basewindow = panel;

	/* And move it to the top layer */
	yutani_set_stack(yctx, panel, YUTANI_ZORDER_TOP);
	yutani_window_update_shape(yctx, panel, YUTANI_SHAPE_THRESHOLD_CLEAR);

	/* Initialize graphics context against the window */
	ctx = init_graphics_yutani_double_buffer(panel);

	/* Draw the background */
	redraw_panel_background(ctx, panel->width, panel->height);

	/* Copy the prerendered background so we can redraw it quickly */
	bg_size = panel->width * panel->height * sizeof(uint32_t);
	bg_blob = malloc(bg_size);
	memcpy(bg_blob, ctx->backbuffer, bg_size);

	/* Catch SIGINT */
	signal(SIGINT, sig_int);
	signal(SIGUSR2, sig_usr2);

	widgets_enabled = list_create();

	/* Initialize requested widgets */
	const char * widgets_to_load[] = {
		"appmenu",
		"windowlist",
		"volume",
		"network",
		"weather",
		"date",
		"clock",
		"logout",
		NULL
	};

	for (const char ** widget = widgets_to_load; *widget; widget++) {
		char lib_name[200];
		char func_name[200];

		snprintf(lib_name, 200, "libtoaru_panel_%s.so", *widget);
		snprintf(func_name, 200, "widget_init_%s", *widget);

		void * lib = dlopen(lib_name, 0);
		if (!lib) {
			fprintf(stderr, "panel: failed to load widget '%s'\n", *widget);
		} else {
			void (*widget_init)(void) = dlsym(lib, func_name);
			if (!widget_init) {
				fprintf(stderr, "panel: failed to initialize widget '%s'\n", *widget);
			} else {
				widget_init();
			}
		}
	}

	/* Lay out the widgets */
	int force_updates = 0;
	update_periodic_widgets(&force_updates);
	widgets_layout();

	/* Subscribe to window updates */
	yutani_subscribe_windows(yctx);

	/* Ask compositor for window list */
	update_window_list();

	/* Key bindings */
	bind_keys();

	time_t last_tick = 0;

	int fds[1] = {fileno(yctx->sock)};

	fprintf(stderr, "entering loop?\n");

	while (_continue) {

		int index = fswait2(1,fds,force_updates ? 50 : 200); /* ~20 fps? */

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
							if (wr->wid == panel->wid) {
								resize_finish(wr->width, wr->height);
							} else if (alttab && wr->wid == alttab->wid) {
								yutani_window_resize_accept(yctx, alttab, wr->width, wr->height);
								reinit_graphics_yutani(actx, alttab);
								redraw_alttab();
								yutani_window_resize_done(yctx, alttab);
							}
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
		if (now.tv_sec != last_tick || force_updates) {
			last_tick = now.tv_sec;
			waitpid(-1, NULL, WNOHANG);
			if (was_tabbing) {
				redraw_alttab();
			}
			update_periodic_widgets(&force_updates);
		}
	}

	/* Close the panel window */
	yutani_close(yctx, panel);

	/* Stop notifying us of window changes */
	yutani_unsubscribe_windows(yctx);

	return 0;
}

