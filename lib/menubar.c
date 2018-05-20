#include <toaru/yutani.h>
#include <toaru/graphics.h>
#include <toaru/menu.h>
#include <toaru/menubar.h>
#include <toaru/sdf.h>

void menu_bar_render(struct menu_bar * self, gfx_context_t * ctx) {
	int _x = self->x;
	int _y = self->y;
	int width = self->width;

	uint32_t menu_bar_color = rgb(59,59,59);
	for (int y = 0; y < MENU_BAR_HEIGHT; ++y) {
		for (int x = 0; x < width; ++x) {
			GFX(ctx, x+_x,y+_y) = menu_bar_color;
		}
	}

	/* for each menu entry */
	int offset = _x;
	struct menu_bar_entries * _entries = self->entries;

	while (_entries->title) {
		int w = draw_sdf_string_width(_entries->title, 16, SDF_FONT_THIN) + 10;
		if ((self->active_menu && hashmap_has(menu_get_windows_hash(), (void*)self->active_menu_wid)) && _entries == self->active_entry) {
			for (int y = _y; y < _y + MENU_BAR_HEIGHT; ++y) {
				for (int x = offset + 2; x < offset + 2 + w; ++x) {
					GFX(ctx, x, y) = rgb(93,163,236);
				}
			}
		}
		offset += draw_sdf_string(ctx, offset + 4, _y + 2, _entries->title, 16, rgb(255,255,255), SDF_FONT_THIN) + 10;
		_entries++;
	}
}

void menu_bar_show_menu(yutani_t * yctx, yutani_window_t * window, struct menu_bar * self, int offset, struct menu_bar_entries * _entries) {
	struct MenuList * new_menu = menu_set_get_menu(self->set, _entries->action);
	menu_show(new_menu, yctx);
	yutani_window_move(yctx, new_menu->window, window->x + offset, window->y + self->y + MENU_BAR_HEIGHT);
	self->active_menu = new_menu;
	self->active_menu_wid = new_menu->window->wid;
	self->active_entry = _entries;
	if (self->redraw_callback) {
		self->redraw_callback();
	}
}

int menu_bar_mouse_event(yutani_t * yctx, yutani_window_t * window, struct menu_bar * self, struct yutani_msg_window_mouse_event * me, int x, int y) {
	if (x < self->x || x >= self->x + self->width || y < self->y || y >= self->y + 24 /* base height */) {
		return 0;
	}

	int offset = self->x;

	struct menu_bar_entries * _entries = self->entries;

	while (_entries->title) {
		int w = draw_sdf_string_width(_entries->title, 16, SDF_FONT_THIN) + 10;
		if (x >= offset && x < offset + w) {
			if (me->command == YUTANI_MOUSE_EVENT_CLICK) {
				menu_bar_show_menu(yctx, window, self,offset,_entries);
			} else if (self->active_menu && hashmap_has(menu_get_windows_hash(), (void*)self->active_menu_wid) && _entries != self->active_entry) {
				menu_definitely_close(self->active_menu);
				menu_bar_show_menu(yctx, window, self,offset,_entries);
			}
		}

		offset += w;
		_entries++;
	}

	return 0;
}
