/**
 * @brief Panel window list widget
 */
#include <toaru/yutani.h>
#include <toaru/graphics.h>
#include <toaru/menu.h>
#include <toaru/text.h>
#include <toaru/panel.h>
#include <toaru/icon_cache.h>

#define GRADIENT_HEIGHT 24
#define GRADIENT_AT(y) premultiply(rgba(72, 167, 255, ((24-(y))*160)/24))
#define MAX_TEXT_WIDTH 180
#define MIN_TEXT_WIDTH 50
#define TOTAL_CELL_WIDTH (title_width)
static struct MenuList * window_menu;
static int title_width = 0;
static yutani_wid_t _window_menu_wid = 0;
static int focused_app = -1;

static void _window_menu_start_move(struct MenuEntry * self) {
	if (!_window_menu_wid)
		return;
	yutani_focus_window(yctx, _window_menu_wid);
	yutani_window_drag_start_wid(yctx, _window_menu_wid);
}

static void _window_menu_start_maximize(struct MenuEntry * self) {
	if (!_window_menu_wid)
		return;
	yutani_special_request_wid(yctx, _window_menu_wid, YUTANI_SPECIAL_REQUEST_MAXIMIZE);
	yutani_focus_window(yctx, _window_menu_wid);
}

static void _window_menu_start_minimize(struct MenuEntry * self) {
	if (!_window_menu_wid)
		return;
	yutani_special_request_wid(yctx, _window_menu_wid, YUTANI_SPECIAL_REQUEST_MINIMIZE);
}

static void _window_menu_close(struct MenuEntry * self) {
	if (!_window_menu_wid)
		return;
	yutani_special_request_wid(yctx, _window_menu_wid, YUTANI_SPECIAL_REQUEST_PLEASE_CLOSE);
}

static void window_show_menu(yutani_wid_t wid, int x) {
	if (window_menu->window) return;
	_window_menu_wid = wid;
	panel_menu_show_at(window_menu, x);
}

static int widget_draw_windowlist(struct PanelWidget * this, gfx_context_t * ctx) {
	if (!window_list || window_list->length == 0) {
		title_width = 0;
	} else if (this->width <= 0) {
		title_width = 28;
	} else {
		title_width = this->width / window_list->length;
		if (title_width > MAX_TEXT_WIDTH) {
			title_width = MAX_TEXT_WIDTH;
		}
		if (title_width < MIN_TEXT_WIDTH) {
			title_width = 28;
		}
	}

	int i = 0, j = 0;
	if (window_list) {
		foreach(node, window_list) {
			struct window_ad * ad = node->value;
			int w = title_width;

			if (i + w > this->width) {
				break;
			}

			/* Hilight the focused window */
			if (ad->flags & 1) {
				/* This is the focused window */
				for (int y = 0; y < GRADIENT_HEIGHT; ++y) {
					for (int x = i; x < i + w; ++x) {
						GFX(ctx, x, y) = alpha_blend_rgba(GFX(ctx, x, y), GRADIENT_AT(y));
					}
				}
			}

			uint32_t text_color = this->pctx->color_text_normal;
			if (j == focused_app) text_color = this->pctx->color_text_hilighted;
			else if (ad->flags & 1) text_color = this->pctx->color_text_focused;
			else if (ad->flags & 2) text_color = premultiply(rgba(_RED(this->pctx->color_text_normal),_GRE(this->pctx->color_text_normal),_BLU(this->pctx->color_text_normal),127));

			if (title_width >= MIN_TEXT_WIDTH) {
				/* Ellipsifiy the title */
				char * s = tt_ellipsify(ad->name, 14, this->pctx->font, title_width - 4, NULL);
				sprite_t * icon = icon_get_48(ad->icon);
				gfx_context_t * subctx = init_graphics_subregion(ctx, i, 0, w, ctx->height-1);
				draw_sprite_scaled_alpha(subctx, icon, w - 48 - 2, 0, 48, 48, (ad->flags & 1) ? 1.0 : 0.7);
				tt_draw_string_shadow(subctx, this->pctx->font, s, 14, 2, 6, text_color, rgb(0,0,0), 4);
				free(subctx);
				free(s);
			} else {
				sprite_t * icon = icon_get_16(ad->icon);
				gfx_context_t * subctx = init_graphics_subregion(ctx, i, 0, w, ctx->height-1);
				draw_sprite_scaled(subctx, icon, 6, 6, 16, 16);
				free(subctx);
			}

			ad->left = this->left + i;

			yutani_window_panel_size(yctx, ad->wid, ad->left + this->pctx->basewindow->x, this->pctx->basewindow->y, w, ctx->height);

			j++;
			i += w;
		}
	}

	return 0;
}

static int widget_click_windowlist(struct PanelWidget * this, struct yutani_msg_window_mouse_event * evt) {
	foreach(node, window_list) {
		struct window_ad * ad = node->value;
		if (evt->new_x >= ad->left && evt->new_x < ad->left + TOTAL_CELL_WIDTH) {
			yutani_focus_window(yctx, ad->wid);
			break;
		}
	}
	return 0;
}

static int widget_rightclick_windowlist(struct PanelWidget * this, struct yutani_msg_window_mouse_event * evt) {
	foreach(node, window_list) {
		struct window_ad * ad = node->value;
		if (evt->new_x >= ad->left && evt->new_x < ad->left + TOTAL_CELL_WIDTH) {
			window_show_menu(ad->wid, evt->new_x);
		}
	}
	return 0;
}

/* Update the hover-focus window */
static int set_focused(int i) {
	if (focused_app != i) {
		focused_app = i;
		return 1;
	}
	return 0;
}


static int widget_move_windowlist(struct PanelWidget * this, struct yutani_msg_window_mouse_event * evt) {
	int found = 0;
	int i = 0;
	int should_redraw = 0;

	foreach(node, window_list) {
		struct window_ad * ad = node->value;
		if (evt->new_x >= ad->left && evt->new_x < ad->left + TOTAL_CELL_WIDTH) {
			found = 1;
			should_redraw |= set_focused(i);
			break;
		}
		i++;
	}

	if (!found) {
		should_redraw |= set_focused(-1);
	}

	int scroll_direction = 0;
	if (evt->buttons & YUTANI_MOUSE_SCROLL_UP) scroll_direction = -1;
	else if (evt->buttons & YUTANI_MOUSE_SCROLL_DOWN) scroll_direction = 1;

	if (scroll_direction != 0) {
		struct window_ad * last = window_list->tail ? window_list->tail->value : NULL;
		int focus_next = 0;
		foreach(node, window_list) {
			struct window_ad * ad = node->value;
			if (focus_next) {
				yutani_focus_window(yctx, ad->wid);
				return 1;
			}
			if (ad->flags & 1) {
				if (scroll_direction == -1) {
					yutani_focus_window(yctx, last->wid);
					return 1;
				}
				if (scroll_direction == 1) {
					focus_next = 1;
				}
			}
			last = ad;
		}
		if (focus_next && window_list->head) {
			struct window_ad * ad = window_list->head->value;
			yutani_focus_window(yctx, ad->wid);
			return 1;
		}
	}

	return should_redraw;
}

static int widget_leave_windowlist(struct PanelWidget * this, struct yutani_msg_window_mouse_event * evt) {
	this->highlighted = 0;
	return set_focused(-1);
}

static int widget_onkey_windowlist(struct PanelWidget * this, struct yutani_msg_key_event * ke) {
	if ((ke->event.modifiers & KEY_MOD_LEFT_ALT) &&
		(ke->event.keycode == KEY_F3) &&
		(ke->event.action == KEY_ACTION_DOWN)) {
		foreach(node, window_list) {
			struct window_ad * ad = node->value;
			if (ad->flags & 1) {
				window_show_menu(ad->wid, ad->left + title_width / 2);
			}
		}
	}
	return 0;
}

struct PanelWidget * widget_init_windowlist(void) {
	window_menu = menu_create();
	window_menu->flags |= MENU_FLAG_BUBBLE_LEFT;
	menu_insert(window_menu, menu_create_normal(NULL, NULL, "Maximize", _window_menu_start_maximize));
	menu_insert(window_menu, menu_create_normal(NULL, NULL, "Minimize", _window_menu_start_minimize));
	menu_insert(window_menu, menu_create_normal(NULL, NULL, "Move", _window_menu_start_move));
	menu_insert(window_menu, menu_create_separator());
	menu_insert(window_menu, menu_create_normal(NULL, NULL, "Close", _window_menu_close));

	/* Alt+F3 = window context menu */
	yutani_key_bind(yctx, KEY_F3, KEY_MOD_LEFT_ALT, YUTANI_BIND_STEAL);

	struct PanelWidget * widget = widget_new();
	widget->fill = 1;
	widget->draw = widget_draw_windowlist;
	widget->click = widget_click_windowlist;
	widget->right_click = widget_rightclick_windowlist;
	widget->move = widget_move_windowlist;
	widget->leave = widget_leave_windowlist;
	widget->onkey = widget_onkey_windowlist;
	list_insert(widgets_enabled, widget);
	return widget;
}

