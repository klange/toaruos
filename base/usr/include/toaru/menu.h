#pragma once

#include <_cheader.h>
#include <toaru/graphics.h>
#include <toaru/hashmap.h>
#include <toaru/list.h>

_Begin_C_Header

enum MenuEntry_Type {
	MenuEntry_Unknown,
	MenuEntry_Normal,
	MenuEntry_Submenu,
	MenuEntry_Separator,
};

struct MenuList;

struct MenuEntry {
	enum MenuEntry_Type _type;
	struct MenuList * _owner;

	int height; /* All must have a height, so put it here. */
	int width; /* Actual width */
	int rwidth; /* Requested width */
	int hilight; /* Is currently hilighted */
	int offset; /* Our offset when we were rendered */

	void (*renderer)(gfx_context_t *, struct MenuEntry *, int);
	void (*focus_change)(struct MenuEntry *, int);
	void (*activate)(struct MenuEntry *, int);

	void (*callback)(struct MenuEntry *);
};

struct MenuEntry_Normal {
	struct MenuEntry; /* dependent on plan9 extensions */
	const char * icon;
	const char * title;
	const char * action;
};

struct MenuEntry_Submenu {
	struct MenuEntry;
	const char * icon;
	const char * title;
	const char * action;
	struct MenuList * _my_child;
};

struct MenuEntry_Separator {
	struct MenuEntry;
};

struct MenuList {
	list_t * entries;
	gfx_context_t * ctx;
	yutani_window_t * window;
	struct MenuSet * set;
	struct MenuList * child;
	struct MenuList * parent;
	struct menu_bar * _bar;
	int closed;
};

struct MenuSet {
	hashmap_t * _menus;
};

extern struct MenuEntry * menu_create_normal(const char * icon, const char * action, const char * title, void (*callback)(struct MenuEntry *));
extern struct MenuEntry * menu_create_submenu(const char * icon, const char * action, const char * title);
extern struct MenuEntry * menu_create_separator(void);
extern struct MenuList * menu_create(void);
extern struct MenuSet * menu_set_from_description(const char * path, void (*callback)(struct MenuEntry *));

extern void menu_insert(struct MenuList * menu, struct MenuEntry * entry);
extern void menu_show(struct MenuList * menu, yutani_t * yctx);
extern void menu_show_at(struct MenuList * menu, yutani_window_t * parent, int x, int y);
extern int menu_process_event(yutani_t * yctx, yutani_msg_t * m);
extern struct MenuList * menu_set_get_root(struct MenuSet * menu);
extern struct MenuList * menu_set_get_menu(struct MenuSet * menu, char * submenu);

extern void menu_free_entry(struct MenuEntry * ptr);
extern void menu_free_menu(struct MenuList * ptr);
extern void menu_free_set(struct MenuSet * ptr);

extern hashmap_t * menu_get_windows_hash(void);
extern int menu_definitely_close(struct MenuList * menu);
extern struct MenuSet * menu_set_create(void);
extern void menu_set_insert(struct MenuSet * set, char * action, struct MenuList * menu);
extern void menu_update_title(struct MenuEntry * self, char * new_title);
extern void menu_force_redraw(struct MenuList * menu);

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
	int active_entry_idx;
	yutani_window_t * window;

	int num_entries;

	void (*redraw_callback)(void);
};

extern void menu_bar_render(struct menu_bar * self, gfx_context_t * ctx);
extern int menu_bar_mouse_event(yutani_t * yctx, yutani_window_t * window, struct menu_bar * self, struct yutani_msg_window_mouse_event * me, int x, int y);
extern void menu_bar_show_menu(yutani_t * yctx, yutani_window_t * window, struct menu_bar * self, int offset, struct menu_bar_entries * _entries);

_End_C_Header
