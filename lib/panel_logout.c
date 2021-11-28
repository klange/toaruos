/**
 * @brief Panel Logout Widget
 *
 * Shows a button that presents a menu with the option to log out.
 */
#include <toaru/yutani.h>
#include <toaru/graphics.h>
#include <toaru/menu.h>
#include <toaru/text.h>
#include <toaru/panel.h>
static struct MenuList * logout_menu;
static sprite_t * sprite_logout;

static int widget_draw_logout(struct PanelWidget * this, gfx_context_t * ctx) {
	draw_sprite_alpha_paint(ctx, sprite_logout, 0, 2, 1.0, (logout_menu->window ? HILIGHT_COLOR : ICON_COLOR)); /* Logout button */
	return 0;
}

static int widget_click_logout(struct PanelWidget * this, struct yutani_msg_window_mouse_event * evt) {
	if (!logout_menu->window) {
		menu_prepare(logout_menu, yctx);
		if (logout_menu->window) {
			yutani_window_move(yctx, logout_menu->window, width - logout_menu->window->width - X_PAD, DROPDOWN_OFFSET);
			yutani_flip(yctx, logout_menu->window);
		}
	}
	return 1;
}

struct PanelWidget * widget_init_logout(void) {
	sprite_logout = malloc(sizeof(sprite_t));
	load_sprite(sprite_logout, "/usr/share/icons/panel-shutdown.png");
	logout_menu = menu_create();
	logout_menu->flags |= MENU_FLAG_BUBBLE_RIGHT;
	menu_insert(logout_menu, menu_create_normal("exit", "log-out", "Log Out", launch_application_menu));

	struct PanelWidget * widget = widget_new();
	widget->width = 36;
	widget->draw = widget_draw_logout;
	widget->click = widget_click_logout;
	list_insert(widgets_enabled, widget);

	return widget;
}

