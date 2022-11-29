/**
 * @brief Panel extensions header
 *
 * Exposed API for the panel
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021 K. Lange
 */

#pragma once

#include <_cheader.h>
#include <stdint.h>
#include <toaru/yutani.h>
#include <toaru/text.h>

_Begin_C_Header

struct PanelWidget {
	int highlighted;
	int left;
	int width;
	int fill;

	int (*click)(struct PanelWidget *, struct yutani_msg_window_mouse_event *);
	int (*right_click)(struct PanelWidget *, struct yutani_msg_window_mouse_event *);
	int (*leave)(struct PanelWidget *, struct yutani_msg_window_mouse_event *);
	int (*enter)(struct PanelWidget *, struct yutani_msg_window_mouse_event *);
	int (*move)(struct PanelWidget *, struct yutani_msg_window_mouse_event *);
	int (*draw)(struct PanelWidget *, gfx_context_t * ctx);
	int (*update)(struct PanelWidget *, int *force_updates);
	int (*onkey)(struct PanelWidget *, struct yutani_msg_key_event *);
};

extern yutani_t * yctx;
extern list_t * widgets_enabled;
extern int width, height;
extern struct PanelWidget * widget_new(void);

#define MAX_WINDOW_COUNT 100
#define PANEL_HEIGHT 36
#define DROPDOWN_OFFSET 34
#define FONT_SIZE 14
#define X_PAD 4
#define Y_PAD 4
#define ICON_Y_PAD 5

#define TEXT_Y_OFFSET 6
#define ICON_PADDING 2

#define HILIGHT_COLOR rgb(142,216,255)
#define FOCUS_COLOR   rgb(255,255,255)
#define TEXT_COLOR    rgb(230,230,230)
#define ICON_COLOR    rgb(230,230,230)
#define SPECIAL_COLOR rgb(93,163,236)

extern struct TT_Font * font;
extern struct TT_Font * font_bold;
extern struct TT_Font * font_mono;
extern struct TT_Font * font_mono_bold;

extern void launch_application_menu(struct MenuEntry * self);

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

extern struct window_ad * ads_by_z[];
extern list_t * window_list;
extern int focused_app;
extern int active_window;
extern void redraw(void);
extern char * ellipsify(char * input, int font_size, struct TT_Font * font, int max_width, int * out_width);
extern int panel_menu_show(struct PanelWidget * this, struct MenuList * menu);
extern int panel_menu_show_at(struct MenuList * menu, int x);

_End_C_Header
