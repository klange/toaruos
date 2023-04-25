/**
 * @brief Panel "Applications" menu widget
 */
#include <toaru/yutani.h>
#include <toaru/yutani-internal.h>
#include <toaru/graphics.h>
#include <toaru/hashmap.h>
#include <toaru/menu.h>
#include <toaru/text.h>
#include <toaru/panel.h>

static struct MenuList * appmenu;

static int widget_draw_appmenu(struct PanelWidget * this, gfx_context_t * ctx) {
	panel_highlight_widget(this,ctx, !!appmenu->window);
	tt_set_size(this->pctx->font, 16);
	int w = tt_string_width(this->pctx->font, "Applications");
	tt_draw_string(ctx, this->pctx->font, (ctx->width - w) / 2, 20, "Applications", appmenu->window ? this->pctx->color_text_hilighted : this->pctx->color_text_normal);
	return 0;
}

static int widget_click_appmenu(struct PanelWidget * this, struct yutani_msg_window_mouse_event * evt) {
	if (!appmenu->window) {
		panel_menu_show(this,appmenu);
		return 1;
	}
	return 0;
}

static int widget_onkey_appmenu(struct PanelWidget * this, struct yutani_msg_key_event * ke) {
	if ((ke->event.modifiers & KEY_MOD_LEFT_ALT) &&
		(ke->event.keycode == KEY_F1) &&
		(ke->event.action == KEY_ACTION_DOWN)) {
		panel_menu_show(this,appmenu);
	}
	return 0;
}

struct PanelWidget * widget_init_appmenu(void) {
	appmenu = menu_set_get_root(menu_set_from_description("/etc/panel.menu", launch_application_menu));
	appmenu->flags = MENU_FLAG_BUBBLE_CENTER;

	/* Bind Alt+F1 */
	yutani_key_bind(yctx, KEY_F1, KEY_MOD_LEFT_ALT, YUTANI_BIND_STEAL);

	struct PanelWidget * widget = widget_new();

	widget->width = 130;
	widget->draw = widget_draw_appmenu;
	widget->click = widget_click_appmenu;
	widget->onkey = widget_onkey_appmenu;
	list_insert(widgets_enabled, widget);
	return widget;
}

