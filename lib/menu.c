#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <sys/types.h>

#include <toaru/yutani.h>
#include <toaru/graphics.h>
#include <toaru/sdf.h>
#include <toaru/hashmap.h>
#include <toaru/list.h>
#include <toaru/icon_cache.h>

#include <toaru/menu.h>

#define MENU_ENTRY_HEIGHT 20
#define MENU_BACKGROUND rgb(239,238,232)
#define MENU_ICON_SIZE 16

#define HILIGHT_BORDER_TOP rgb(54,128,205)
#define HILIGHT_GRADIENT_TOP rgb(93,163,236)
#define HILIGHT_GRADIENT_BOTTOM rgb(56,137,220)
#define HILIGHT_BORDER_BOTTOM rgb(47,106,167)

static hashmap_t * menu_windows = NULL;
static yutani_t * my_yctx = NULL;

__attribute__((constructor))
static void _init_menus(void) {
	menu_windows = hashmap_create_int(10);
}

void _menu_draw_MenuEntry_Normal(gfx_context_t * ctx, struct MenuEntry * self, int offset) {
	struct MenuEntry_Normal * _self = (struct MenuEntry_Normal *)self;

	/* Background gradient */
	if (_self->hilight) {
		draw_line(ctx, 1, _self->width-2, offset, offset, HILIGHT_BORDER_TOP);
		draw_line(ctx, 1, _self->width-2, offset + _self->height - 1, offset + _self->height - 1, HILIGHT_BORDER_BOTTOM);
		for (int i = 1; i < self->height-1; ++i) {
			int thing = ((i - 1) * 256) / (_self->height - 2);
			if (thing > 255) thing = 255;
			if (thing < 0) thing = 0;
			uint32_t c = interp_colors(HILIGHT_GRADIENT_TOP, HILIGHT_GRADIENT_BOTTOM, thing);
			draw_line(ctx, 1, self->width-2, offset + i, offset + i, c);
		}
	}

	/* Icon */
	sprite_t * icon = icon_get_16(_self->icon);
	if (icon->width == MENU_ICON_SIZE) {
		draw_sprite(ctx, icon, 4, offset + 2);
	} else {
		draw_sprite_scaled(ctx, icon, 4, offset + 2, MENU_ICON_SIZE, MENU_ICON_SIZE);
	}

	/* Foreground text color */
	uint32_t color = _self->hilight ? rgb(255,255,255) : rgb(0,0,0);

	/* Draw title */
	draw_sdf_string(ctx, 22, offset + 1, _self->title, 16, color, SDF_FONT_THIN);
}

void _menu_focus_MenuEntry_Normal(struct MenuEntry * self, int focused) {

}

void _menu_activate_MenuEntry_Normal(struct MenuEntry * self, int flags) {
	struct MenuEntry_Normal * _self = (struct MenuEntry_Normal *)self;

	if (_self->callback) {
		_self->callback(_self);
	}
}

struct MenuEntry * menu_create_normal(const char * icon, const char * action, const char * title, void (*callback)(struct MenuEntry *)) {
	struct MenuEntry_Normal * out = malloc(sizeof(struct MenuEntry_Normal));

	out->_type = MenuEntry_Normal;
	out->height = MENU_ENTRY_HEIGHT;
	out->hilight = 0;
	out->renderer = _menu_draw_MenuEntry_Normal;
	out->focus_change = _menu_focus_MenuEntry_Normal;
	out->activate = _menu_activate_MenuEntry_Normal;
	out->icon = strdup(icon);
	out->title = strdup(title);
	out->action = strdup(action);
	out->callback = callback;

	out->rwidth = 50 + draw_sdf_string_width(out->title, 16, SDF_FONT_THIN);

	return (struct MenuEntry *)out;
}

void _menu_draw_MenuEntry_Submenu(gfx_context_t * ctx, struct MenuEntry * self, int offset) {
	_menu_draw_MenuEntry_Normal(ctx,self,offset);
}

void _menu_focus_MenuEntry_Submenu(struct MenuEntry * self, int focused) {

}

void _menu_activate_MenuEntry_Submenu(struct MenuEntry * self, int focused) {

}

struct MenuEntry * menu_create_submenu(const char * icon, const char * action, const char * title) {
	struct MenuEntry_Submenu * out = malloc(sizeof(struct MenuEntry_Submenu));

	out->_type = MenuEntry_Submenu;
	out->height = MENU_ENTRY_HEIGHT;
	out->hilight = 0;
	out->renderer = _menu_draw_MenuEntry_Submenu;
	out->focus_change = _menu_focus_MenuEntry_Submenu;
	out->activate = _menu_activate_MenuEntry_Submenu;
	out->icon = strdup(icon);
	out->title = strdup(title);
	out->action = strdup(action);

	out->rwidth = 50 + draw_sdf_string_width(out->title, 16, SDF_FONT_THIN);

	return (struct MenuEntry *)out;
}

void _menu_draw_MenuEntry_Separator(gfx_context_t * ctx, struct MenuEntry * self, int offset) {
	draw_line(ctx, 2, self->width-4, offset+3, offset+3, rgb(178,178,178));
	draw_line(ctx, 2, self->width-5, offset+4, offset+4, rgb(250,250,250));
}

void _menu_focus_MenuEntry_Separator(struct MenuEntry * self, int focused) {

}

struct MenuEntry * menu_create_separator(void) {
	struct MenuEntry_Separator * out = malloc(sizeof(struct MenuEntry_Separator));

	out->_type = MenuEntry_Separator;
	out->height = 6;
	out->hilight = 0;
	out->renderer = _menu_draw_MenuEntry_Separator;
	out->focus_change = _menu_focus_MenuEntry_Separator;
	out->rwidth = 10; /* at least a bit please */

	return (struct MenuEntry *)out;
}

static int _close_enough(struct yutani_msg_window_mouse_event * me) {
	if (me->command == YUTANI_MOUSE_EVENT_RAISE && sqrt(pow(me->new_x - me->old_x, 2) + pow(me->new_y - me->old_y, 2)) < 10) {
		return 1;
	}
	return 0;
}

static char read_buf[1024];
static size_t available = 0;
static size_t offset = 0;
static size_t read_from = 0;
static char * read_line(FILE * f, char * out, ssize_t len) {
	while (len > 0) {
		if (available == 0) {
			if (offset == 1024) {
				offset = 0;
			}
			size_t r = read(fileno(f), &read_buf[offset], 1024 - offset);
			read_from = offset;
			available = r;
			offset += available;
		}

		if (available == 0) {
			*out = '\0';
			return out;
		}

		while (read_from < offset && len > 0) {
			*out = read_buf[read_from];
			len--;
			read_from++;
			available--;
			if (*out == '\n') {
				return out;
			}
			out++;
		}
	}

	return out;
}

static void _menu_calculate_dimensions(struct MenuList * menu, int * height, int * width) {
	list_t * list = menu->entries;
	*width = 0;
	*height = 8; /* TODO top and height */
	foreach(node, list) {
		struct MenuEntry * entry = node->value;
		*height += entry->height;
		if (*width < entry->rwidth) {
			*width = entry->rwidth;
		}
	}
	/* Go back through and update actual widths */
	foreach(node, list) {
		struct MenuEntry * entry = node->value;
		entry->width = *width;
	}
}

struct MenuList * menu_set_get_root(struct MenuSet * menu) {
	return (void*)hashmap_get(menu->_menus,"_");
}

struct MenuSet * menu_set_from_description(const char * path, void (*callback)(struct MenuEntry *)) {
	FILE * f;
	if (!strcmp(path,"-")) {
		f = stdin;
	} else {
		f = fopen(path,"r");
	}

	if (!f) {
		return NULL;
	}

	hashmap_t * out = hashmap_create(10);

	char * current_name = NULL;
	list_t * current_list = NULL;

	/* Read through the file */
	char line[256];
	while (1) {
		memset(line, 0, 256);
		read_line(f, line, 256);
		if (!*line) break;

		if (line[strlen(line)-1] == '\n') {
			line[strlen(line)-1] = '\0';
		}

		if (!*line) continue; /* skip blank */

		if (*line == ':') {
			/* New menu */
			if (current_name) {
				struct MenuList * p = malloc(sizeof(struct MenuList));
				p->entries = current_list;
				p->ctx = NULL;
				p->window = NULL;
				hashmap_set(out, current_name, p);
			}
			current_name = strdup(line+1);
			current_list = list_create();
		} else if (*line == '#') {
			/* Comment */
			continue;
		} else if (*line == '-') {
			if (!current_list) {
				fprintf(stderr, "Tried to add separator with no active menu.\n");
				goto failure;
			}
			list_insert(current_list, menu_create_separator());
		} else if (*line == '&') {
			if (!current_list) {
				fprintf(stderr, "Tried to add submenu with no active menu.\n");
				goto failure;
			}
			char * action = line+1;
			char * icon = strstr(action,",");
			if (!icon) {
				fprintf(stderr, "Malformed line in submenu: no icon\n");
				goto failure;
			}
			*icon = '\0';
			icon++;
			char * title = strstr(icon,",");
			if (!title) {
				fprintf(stderr, "Malformed line in submenu: no title\n");
				goto failure;
			}
			*title = '\0';
			title++;
			list_insert(current_list, menu_create_submenu(icon,action,title));
		} else {
			if (!current_list) {
				fprintf(stderr, "Tried to add item with no active menu.\n");
				goto failure;
			}
			char * action = line;
			char * icon = strstr(action,",");
			if (!icon) {
				fprintf(stderr, "Malformed line in action: no icon\n");
				goto failure;
			}
			*icon = '\0';
			icon++;
			char * title = strstr(icon,",");
			if (!title) {
				fprintf(stderr, "Malformed line in action: no title\n");
				goto failure;
			}
			*title = '\0';
			title++;
			list_insert(current_list, menu_create_normal(icon,action,title,callback));
		}
	}

	if (current_name) {
		struct MenuList * p = malloc(sizeof(struct MenuList));
		p->entries = current_list;
		p->ctx = NULL;
		p->window = NULL;
		hashmap_set(out, current_name, p);
	}

	struct MenuSet * _out = malloc(sizeof(struct MenuSet));
	_out->_menus = out;

	return _out;

failure:
	fprintf(stderr, "malformed description file\n");
	if (f != stdin) {
		fclose(f);
	}
	free(out);
	return NULL;
}

static void _menu_redraw(yutani_window_t * menu_window, yutani_t * yctx, struct MenuList * menu) {

	gfx_context_t * ctx = menu->ctx;
	list_t * entries = menu->entries;
	/* Window background */
	draw_fill(ctx, MENU_BACKGROUND);

	/* Window border */
	draw_line(ctx, 0, ctx->width-1, 0, 0, rgb(109,111,112));
	draw_line(ctx, 0, 0, 0, ctx->height-1, rgb(109,111,112));
	draw_line(ctx, ctx->width-1, ctx->width-1, 0, ctx->height-1, rgb(109,111,112));
	draw_line(ctx, 0, ctx->width-1, ctx->height-1, ctx->height-1, rgb(109,111,112));

	/* Draw menu entries */
	int offset = 4;
	foreach(node, entries) {
		struct MenuEntry * entry = node->value;
		if (entry->renderer) {
			entry->renderer(ctx, entry, offset);
		}

		offset += entry->height;
	}

	/* Expose menu */
	flip(ctx);
	yutani_flip(yctx, menu_window);
}

void menu_show(struct MenuList * menu, yutani_t * yctx) {
	/* Calculate window dimensions */
	int height, width;
	_menu_calculate_dimensions(menu,&height, &width);

	my_yctx = yctx;

	/* Create window */
	yutani_window_t * menu_window = yutani_window_create_flags(yctx, width, height, YUTANI_WINDOW_FLAG_ALT_ANIMATION);
	if (menu->ctx) {
		reinit_graphics_yutani(menu->ctx, menu_window);
	} else {
		menu->ctx = init_graphics_yutani_double_buffer(menu_window);
	}

	menu_window->user_data = menu;
	menu->window = menu_window;

	_menu_redraw(menu_window, yctx, menu);

	hashmap_set(menu_windows, (void*)menu_window->wid, menu_window);
}

int menu_process_event(yutani_t * yctx, yutani_msg_t * m) {
	if (m) {
		switch (m->type) {
			case YUTANI_MSG_WINDOW_MOUSE_EVENT:
				{
					struct yutani_msg_window_mouse_event * me = (void*)m->data;
					if (hashmap_has(menu_windows, (void*)me->wid)) {
						yutani_window_t * window = hashmap_get(menu_windows, (void *)me->wid);
						struct MenuList * menu = window->user_data;
						int offset = 4;
						int changed = 0;
						foreach(node, menu->entries) {
							struct MenuEntry * entry = node->value;
							if (me->new_y >= offset && me->new_y < offset + entry->height &&
									me->new_x >= 0 && me->new_x < entry->width) {
								if (!entry->hilight) {
									changed = 1;
									entry->hilight = 1;
								}
								if (me->command == YUTANI_MOUSE_EVENT_CLICK || _close_enough(me)) {
									if (entry->activate) {
										entry->activate(entry, 0);
									}
								}
							} else {
								if (entry->hilight) {
									changed = 1;
									entry->hilight = 0;
								}
							}
							offset += entry->height;
						}
						if (changed) {
							_menu_redraw(window,yctx,menu);
						}
					}
				}
				break;
			case YUTANI_MSG_WINDOW_FOCUS_CHANGE:
				{
					struct yutani_msg_window_focus_change * me = (void*)m->data;
					if (hashmap_has(menu_windows, (void*)me->wid)) {
						yutani_window_t * window = hashmap_get(menu_windows, (void *)me->wid);
						struct MenuList * menu = window->user_data;
						if (!me->focused) {
							/* XXX leave menu */
							hashmap_remove(menu_windows, (void*)me->wid);
							yutani_close(yctx, window);
							menu->window = NULL;
							/* if root and not window.root.menus and window.root.focused */
							return 1;
						} else {
							window->focused = me->focused;
							/* Redraw? */
						}
					}
				}
				break;
			default:
				break;
		}
	}
	return 0;
}
