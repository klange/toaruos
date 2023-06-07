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

struct PanelContext {
	uint32_t color_text_normal;
	uint32_t color_text_hilighted;
	uint32_t color_text_focused;
	uint32_t color_icon_normal;
	uint32_t color_special;

	int font_size_default;

	yutani_window_t * basewindow;

	struct TT_Font * font;
	struct TT_Font * font_bold;
	struct TT_Font * font_mono;
	struct TT_Font * font_mono_bold;

	int extra_widget_spacing;
};

struct PanelWidget {
	struct PanelContext * pctx;
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
extern struct PanelWidget * widget_new(void);

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
extern void redraw(void);
extern int panel_menu_show(struct PanelWidget * this, struct MenuList * menu);
extern int panel_menu_show_at(struct MenuList * menu, int x);
extern void panel_highlight_widget(struct PanelWidget * this, gfx_context_t * ctx, int active);

_End_C_Header
