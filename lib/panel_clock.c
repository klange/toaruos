/**
 * @brief Panel clock widget
 */
#include <time.h>
#include <math.h>
#include <sys/time.h>
#include <toaru/yutani.h>
#include <toaru/graphics.h>
#include <toaru/menu.h>
#include <toaru/text.h>
#include <toaru/panel.h>
static struct MenuList * clockmenu;
static sprite_t * watchface = NULL;

static void watch_draw_line(gfx_context_t * ctx, int offset, double r, double a, double b, uint32_t color, float thickness) {
	double theta = (a / b) * 2.0 * M_PI;
	draw_line_aa(ctx,
		70 + 4,
		70 + 4 + sin(theta) * r,
		70 + offset,
		70 + offset - cos(theta) * r, color, thickness);
}

void _menu_draw_MenuEntry_Clock(gfx_context_t * ctx, struct MenuEntry * self, int offset) {
	self->offset = offset;

	draw_sprite(ctx, watchface, 4, offset);

	struct timeval now;
	struct tm * timeinfo;
	gettimeofday(&now, NULL);
	timeinfo = localtime((time_t *)&now.tv_sec);

	double sec = timeinfo->tm_sec + (double)now.tv_usec / 1000000.0;
	double min = timeinfo->tm_min + sec / 60.0;
	double hour = (timeinfo->tm_hour % 12) + min / 60.0;

	watch_draw_line(ctx, offset, 40, hour, 12, rgb(0,0,0), 2.0);
	watch_draw_line(ctx, offset, 60, min, 60, rgb(0,0,0), 1.5);
	watch_draw_line(ctx, offset, 65, sec, 60, rgb(240,0,0), 1.0);

}

static struct MenuEntryVTable clock_vtable = {
	.methods = 3,
	.renderer = _menu_draw_MenuEntry_Clock,
};

struct MenuEntry * menu_create_clock(void) {
	struct MenuEntry * out = menu_create_separator(); /* Steal some defaults */

	out->_type = -1; /* Special */
	out->height = 140;
	out->rwidth = 148;
	out->vtable = &clock_vtable;
	return out;
}

static int widget_draw_clock(struct PanelWidget * this, gfx_context_t * ctx) {
	struct timeval now;
	struct tm * timeinfo;

	/* Get the current time for the clock */
	gettimeofday(&now, NULL);
	timeinfo = localtime((time_t *)&now.tv_sec);

	/* Hours : Minutes : Seconds */
	char time[80];
	strftime(time, 80, "%H:%M:%S", timeinfo);
	tt_set_size(font, 16);
	tt_draw_string(ctx, font, 0, 20, time, clockmenu->window ? HILIGHT_COLOR : TEXT_COLOR);

	return 0;
}

static int widget_click_clock(struct PanelWidget * this, struct yutani_msg_window_mouse_event * evt) {
	if (!clockmenu->window) {
		panel_menu_show(this,clockmenu);
		return 1;
	}
	return 0;
}

static int widget_update_clock(struct PanelWidget * this) {
	if (clockmenu && clockmenu->window) {
		menu_force_redraw(clockmenu);
	}
	return 0;
}

struct PanelWidget * widget_init_clock(void) {
	watchface = malloc(sizeof(sprite_t));
	load_sprite(watchface, "/usr/share/icons/watchface.png");
	clockmenu = menu_create();
	clockmenu->flags |= MENU_FLAG_BUBBLE_RIGHT;
	menu_insert(clockmenu, menu_create_clock());

	struct PanelWidget * widget = widget_new();
	widget->width = 80; /* TODO what */
	widget->draw = widget_draw_clock;
	widget->click = widget_click_clock;
	widget->update = widget_update_clock;
	list_insert(widgets_enabled, widget);
	return widget;
}
