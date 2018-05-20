#pragma once

#include <toaru/graphics.h>
#include <toaru/hashmap.h>
#include <toaru/list.h>

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
