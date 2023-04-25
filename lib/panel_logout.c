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
	panel_highlight_widget(this,ctx,!!logout_menu->window);
	draw_sprite_alpha_paint(ctx, sprite_logout, (ctx->width - sprite_logout->width) / 2, 2, 1.0, (logout_menu->window ? this->pctx->color_text_hilighted : this->pctx->color_icon_normal)); /* Logout button */
	return 0;
}

static int widget_click_logout(struct PanelWidget * this, struct yutani_msg_window_mouse_event * evt) {
	if (!logout_menu->window) {
		panel_menu_show(this,logout_menu);
		return 1;
	}
	return 0;
}

struct PanelWidget * widget_init_logout(void) {
	sprite_logout = malloc(sizeof(sprite_t));
	load_sprite(sprite_logout, "/usr/share/icons/panel-shutdown.png");
	logout_menu = menu_create();
	logout_menu->flags |= MENU_FLAG_BUBBLE_RIGHT;
	menu_insert(logout_menu, menu_create_normal("exit", "log-out", "Log Out", launch_application_menu));

	struct PanelWidget * widget = widget_new();

	widget->width = sprite_logout->width + widget->pctx->extra_widget_spacing;
	widget->draw = widget_draw_logout;
	widget->click = widget_click_logout;
	list_insert(widgets_enabled, widget);

	return widget;
}

