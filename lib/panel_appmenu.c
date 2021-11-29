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
	tt_set_size(font, 16);
	tt_draw_string(ctx, font, 0, 20, "Applications", appmenu->window ? HILIGHT_COLOR : TEXT_COLOR);
	return 0;
}

static int widget_click_appmenu(struct PanelWidget * this, struct yutani_msg_window_mouse_event * evt) {
	if (!appmenu->window) {
		panel_menu_show(this,appmenu);
		return 1;
	}
	return 0;
}

struct PanelWidget * widget_init_appmenu(void) {
	appmenu = menu_set_get_root(menu_set_from_description("/etc/panel.menu", launch_application_menu));
	appmenu->flags = MENU_FLAG_BUBBLE_CENTER;

	struct PanelWidget * widget = widget_new();
	widget->width = 130;
	widget->draw = widget_draw_appmenu;
	widget->click = widget_click_appmenu;
	list_insert(widgets_enabled, widget);
	return widget;
}

