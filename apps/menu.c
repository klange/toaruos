/* vim: ts=4 sw=4 noexpandtab
 *
 * Menu tool / eventual library.
 */
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

#define MENU_ENTRY_HEIGHT 20
#define MENU_BACKGROUND rgb(239,238,232)
#define MENU_ICON_SIZE 16

static yutani_t * yctx;

enum ListEntry_Type {
	ListEntry_Unknown,
	ListEntry_Normal,
	ListEntry_Submenu,
	ListEntry_Separator,
};

struct ListEntry {
	enum ListEntry_Type _type;

	int height; /* All must have a height, so put it here. */
	int width; /* Actual width */
	int rwidth; /* Requested width */
	int hilight; /* Is currently hilighted */

	void (*renderer)(gfx_context_t *, struct ListEntry *, int);
	void (*focus_change)(struct ListEntry *, int);
	void (*activate)(struct ListEntry *, int);
};

struct ListEntry_Normal {
	struct ListEntry; /* dependent on plan9 extensions */
	const char * icon;
	const char * title;
	const char * action;
};

struct ListEntry_Submenu {
	struct ListEntry;
	const char * icon;
	const char * title;
	const char * action;
};

struct ListEntry_Separator {
	struct ListEntry;
};

static uint32_t interp_colors(uint32_t bottom, uint32_t top, uint8_t interp) {
	uint8_t red = (_RED(bottom) * (255 - interp) + _RED(top) * interp) / 255;
	uint8_t gre = (_GRE(bottom) * (255 - interp) + _GRE(top) * interp) / 255;
	uint8_t blu = (_BLU(bottom) * (255 - interp) + _BLU(top) * interp) / 255;
	uint8_t alp = (_ALP(bottom) * (255 - interp) + _ALP(top) * interp) / 255;
	return rgba(red,gre,blu, alp);
}

#define HILIGHT_BORDER_TOP rgb(54,128,205)
#define HILIGHT_GRADIENT_TOP rgb(93,163,236)
#define HILIGHT_GRADIENT_BOTTOM rgb(56,137,220)
#define HILIGHT_BORDER_BOTTOM rgb(47,106,167)

void _menu_draw_ListEntry_Normal(gfx_context_t * ctx, struct ListEntry * self, int offset) {
	struct ListEntry_Normal * _self = (struct ListEntry_Normal *)self;

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

void _menu_focus_ListEntry_Normal(struct ListEntry * self, int focused) {

}

void _menu_activate_ListEntry_Normal(struct ListEntry * self, int flags) {
	struct ListEntry_Normal * _self = (struct ListEntry_Normal *)self;

	fprintf(stdout, "%s\n", _self->action);
	exit(0);
}

struct ListEntry * menu_create_normal(const char * icon, const char * action, const char * title) {
	struct ListEntry_Normal * out = malloc(sizeof(struct ListEntry_Normal));

	out->_type = ListEntry_Normal;
	out->height = MENU_ENTRY_HEIGHT;
	out->hilight = 0;
	out->renderer = _menu_draw_ListEntry_Normal;
	out->focus_change = _menu_focus_ListEntry_Normal;
	out->activate = _menu_activate_ListEntry_Normal;
	out->icon = strdup(icon);
	out->title = strdup(title);
	out->action = strdup(action);

	out->rwidth = 50 + draw_sdf_string_width(out->title, 16, SDF_FONT_THIN);

	return (struct ListEntry *)out;
}

void _menu_draw_ListEntry_Submenu(gfx_context_t * ctx, struct ListEntry * self, int offset) {
	_menu_draw_ListEntry_Normal(ctx,self,offset);
}

void _menu_focus_ListEntry_Submenu(struct ListEntry * self, int focused) {

}

void _menu_activate_ListEntry_Submenu(struct ListEntry * self, int focused) {

}

struct ListEntry * menu_create_submenu(const char * icon, const char * action, const char * title) {
	struct ListEntry_Submenu * out = malloc(sizeof(struct ListEntry_Submenu));

	out->_type = ListEntry_Submenu;
	out->height = MENU_ENTRY_HEIGHT;
	out->hilight = 0;
	out->renderer = _menu_draw_ListEntry_Submenu;
	out->focus_change = _menu_focus_ListEntry_Submenu;
	out->activate = _menu_activate_ListEntry_Submenu;
	out->icon = strdup(icon);
	out->title = strdup(title);
	out->action = strdup(action);

	out->rwidth = 50 + draw_sdf_string_width(out->title, 16, SDF_FONT_THIN);

	return (struct ListEntry *)out;
}

void _menu_draw_ListEntry_Separator(gfx_context_t * ctx, struct ListEntry * self, int offset) {
	draw_line(ctx, 2, self->width-4, offset+3, offset+3, rgb(178,178,178));
	draw_line(ctx, 2, self->width-5, offset+4, offset+4, rgb(250,250,250));
}

void _menu_focus_ListEntry_Separator(struct ListEntry * self, int focused) {

}

struct ListEntry * menu_create_separator(void) {
	struct ListEntry_Separator * out = malloc(sizeof(struct ListEntry_Separator));

	out->_type = ListEntry_Separator;
	out->height = 6;
	out->hilight = 0;
	out->renderer = _menu_draw_ListEntry_Separator;
	out->focus_change = _menu_focus_ListEntry_Separator;
	out->rwidth = 10; /* at least a bit please */

	return (struct ListEntry *)out;
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

static void _menu_calculate_dimensions(list_t * list, int * height, int * width) {
	*width = 0;
	*height = 8; /* TODO top and height */
	foreach(node, list) {
		struct ListEntry * entry = node->value;
		*height += entry->height;
		if (*width < entry->rwidth) {
			*width = entry->rwidth;
		}
	}
	/* Go back through and update actual widths */
	foreach(node, list) {
		struct ListEntry * entry = node->value;
		entry->width = *width;
	}
}

hashmap_t * menu_from_description(const char * path) {
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
				hashmap_set(out, current_name, current_list);
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
			list_insert(current_list, menu_create_normal(icon,action,title));
		}
	}

	if (current_name) {
		hashmap_set(out, current_name, current_list);
	}

	return out;

failure:
	fprintf(stderr, "malformed description file\n");
	if (f != stdin) {
		fclose(f);
	}
	free(out);
	return NULL;
}

static void _menu_redraw(yutani_window_t * menu_window, gfx_context_t * ctx, list_t * entries) {
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
		struct ListEntry * entry = node->value;
		if (entry->renderer) {
			entry->renderer(ctx, entry, offset);
		}

		offset += entry->height;
	}

	/* Expose menu */
	flip(ctx);
	yutani_flip(yctx, menu_window);
}

static void _menu_create_window(list_t * entries, yutani_window_t **win_out, gfx_context_t **ctx_out) {
	/* Calculate window dimensions */
	int height, width;
	_menu_calculate_dimensions(entries, &height, &width);

	/* Create window */
	yutani_window_t * menu_window = yutani_window_create(yctx, width, height);
	gfx_context_t * ctx = init_graphics_yutani_double_buffer(menu_window);

	_menu_redraw(menu_window, ctx, entries);

	*win_out = menu_window;
	*ctx_out = ctx;
}

int main(int argc, char * argv[]) {

	if (argc < 2) {
		fprintf(stderr, "%s: expected argument\n", argv[0]);
		return 1;
	}

	yctx = yutani_init();

	/* Create menu from file. */
	hashmap_t * menu = menu_from_description(argv[1]);

#if 0
	list_t * hash_keys = hashmap_keys(menu);
	foreach(_key, hash_keys) {
		char * key = (char *)_key->value;
		fprintf(stderr, "[%s]\n", key);
		list_t * entries = hashmap_get(menu, key);

		foreach(node, entries) {
			struct ListEntry * entry = node->value;

			switch (entry->_type) {
				case ListEntry_Unknown:
					fprintf(stderr, "- Unknown menu list entry.\n");
					break;
				case ListEntry_Normal:
					fprintf(stderr, "- Action: %s\n", ((struct ListEntry_Normal *)entry)->title);
					break;
				case ListEntry_Submenu:
					fprintf(stderr, "- Submenu: %s\n", ((struct ListEntry_Submenu *)entry)->title);
					break;
				case ListEntry_Separator:
					fprintf(stderr, "- Separator\n");
					break;
			}
		}
	}
	list_free(hash_keys);
	free(hash_keys);
#endif

	((struct ListEntry *)((list_t *)hashmap_get(menu,"_"))->head->value)->hilight = 1;

	yutani_window_t * window;
	gfx_context_t * ctx;
	list_t * entries = hashmap_get(menu, "_");
	_menu_create_window(entries, &window, &ctx);
	yutani_window_move(yctx, window, 0, 80);

	while (1) {
		yutani_msg_t * m = yutani_poll(yctx);
		if (m) {
			switch (m->type) {
				case YUTANI_MSG_KEY_EVENT:
					{
						struct yutani_msg_key_event * ke = (void*)m->data;
						if (ke->event.action == KEY_ACTION_DOWN && ke->event.keycode == 'q') {
							return 0;
						}
					}
					break;
				/* Mouse movement / click */
				case YUTANI_MSG_WINDOW_MOUSE_EVENT:
					{
						struct yutani_msg_window_mouse_event * me = (void*)m->data;
						if (me->wid = window->wid) {
							int offset = 4;
							int changed = 0;
							foreach(node, entries) {
								struct ListEntry * entry = node->value;
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
								_menu_redraw(window,ctx,entries);
							}
						}
					}
					break;
#if 0
				case YUTANI_MSG_WINDOW_FOCUS_CHANGE:
					handle_focus_event((struct yutani_msg_window_focus_change *)m->data);
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
						resize_finish(wr->width, wr->height);
					}
					break;
#endif
				default:
					break;
			}
			free(m);
		}
	}

	return 0;
}
