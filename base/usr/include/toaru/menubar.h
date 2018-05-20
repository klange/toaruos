#pragma once

#include <toaru/menu.h>

#define MENU_BAR_HEIGHT 24

struct menu_bar_entries {
	char * title;
	char * action;
};

struct menu_bar {
	int x;
	int y;
	int width;

	struct menu_bar_entries * entries;

	struct MenuSet * set;

	struct menu_bar_entries * active_entry;
	struct MenuList * active_menu;
	int active_menu_wid;

	void (*redraw_callback)(void);
};

extern void menu_bar_render(struct menu_bar * self, gfx_context_t * ctx);
extern int menu_bar_mouse_event(yutani_t * yctx, yutani_window_t * window, struct menu_bar * self, struct yutani_msg_window_mouse_event * me, int x, int y);
