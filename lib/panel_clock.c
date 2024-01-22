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

static void watch_draw_line(gfx_context_t * ctx, int offset, double r, double ir, double a, double b, uint32_t color, float thickness) {
	double theta = (a / b) * 2.0 * M_PI;

	struct gfx_point v = {74.0 + sin(theta) * ir, 70.0 + offset - cos(theta) * ir};
	struct gfx_point w = {74.0 + sin(theta) * r,  70.0 + offset - cos(theta) * r};

	draw_line_aa_points(ctx,&v,&w,color,thickness);
}

static double tick(double t) {
	double ts = t*t;
	double tc = ts*t;
	return (0.5*tc*ts + -8.0*ts*ts + 20.0*tc + -19.0*ts + 7.5*t);
}

void _menu_draw_MenuEntry_Clock(gfx_context_t * ctx, struct MenuEntry * self, int offset) {
	self->offset = offset;

	/* Draw the background */
	draw_rounded_rectangle(ctx, 4, offset, 140, 140, 70, rgb(0,0,0));
	draw_rounded_rectangle(ctx, 6, offset + 2, 136, 136, 68, rgb(255,255,255));

	for (int i = 0; i < 60; ++i) {
		watch_draw_line(ctx, offset, 68, i % 5 ? 65 : 60, i, 60, rgb(0,0,0), i % 5 ? 0.3 : 1.0);
	}

	static const char * digits[] = {"12","1","2","3","4","5","6","7","8","9","10","11"};

	struct TT_Font * font = ((struct PanelWidget*)self->_private)->pctx->font;
	tt_set_size(font, 12);
	for (int i = 0; i < 12; ++i) {
		int w = tt_string_width(font, digits[i]);
		double theta = (i / 12.0) * 2.0 * M_PI;
		double x = 74.0 + sin(theta) * 50.0;
		double y = 70.0 + offset - cos(theta) * 50.0;

		int _x = x - w / 2;
		int _y = y + 6;

		tt_draw_string(ctx, font, _x, _y, digits[i], rgb(0,0,0));
	}

	struct timeval now;
	struct tm * timeinfo;
	gettimeofday(&now, NULL);
	timeinfo = localtime((time_t *)&now.tv_sec);

	double sec = timeinfo->tm_sec + tick((double)now.tv_usec / 1000000.0) - 1.0;
	double min = timeinfo->tm_min + sec / 60.0;
	double hour = (timeinfo->tm_hour % 12) + min / 60.0;

	watch_draw_line(ctx, offset, 40, 0, hour, 12, rgb(0,0,0), 2.0);
	watch_draw_line(ctx, offset, 60, 0, min, 60, rgb(0,0,0), 1.5);
	watch_draw_line(ctx, offset, 65, -12, sec, 60, rgb(240,0,0), 0.5);
	watch_draw_line(ctx, offset, -4, -8, sec, 60, rgb(240,0,0), 2.0);

}

static struct MenuEntryVTable clock_vtable = {
	.methods = 3,
	.renderer = _menu_draw_MenuEntry_Clock,
};

struct MenuEntry * menu_create_clock(struct PanelWidget * this) {
	struct MenuEntry * out = menu_create_separator(); /* Steal some defaults */

	out->_type = -1; /* Special */
	out->height = 140;
	out->rwidth = 148;
	out->vtable = &clock_vtable;
	out->_private = this;
	return out;
}

static int widget_draw_clock(struct PanelWidget * this, gfx_context_t * ctx) {
	struct timeval now;
	struct tm * timeinfo;

	panel_highlight_widget(this,ctx,!!clockmenu->window);

	struct TT_Font * font = this->pctx->font;

	/* Get the current time for the clock */
	gettimeofday(&now, NULL);
	timeinfo = localtime((time_t *)&now.tv_sec);

	/* Hours : Minutes : Seconds */
	char time[80];
	strftime(time, 80, "%H:%M:%S", timeinfo);
	tt_set_size(font, 16);
	int w = tt_string_width(font, time);
	tt_draw_string(ctx, font, (ctx->width - w) / 2, 20, time, clockmenu->window ? this->pctx->color_text_hilighted : this->pctx->color_text_normal);

	return 0;
}

static int widget_click_clock(struct PanelWidget * this, struct yutani_msg_window_mouse_event * evt) {
	if (!clockmenu->window) {
		panel_menu_show(this,clockmenu);
		return 1;
	}
	return 0;
}

static int widget_update_clock(struct PanelWidget * this, int * force_updates) {
	if (clockmenu && clockmenu->window) {
		menu_force_redraw(clockmenu);
		*force_updates = 1;
	}
	return 0;
}

struct PanelWidget * widget_init_clock(void) {
	struct PanelWidget * widget = widget_new();

	clockmenu = menu_create();
	clockmenu->flags |= MENU_FLAG_BUBBLE_RIGHT;
	menu_insert(clockmenu, menu_create_clock(widget));

	widget->width = 90; /* TODO what */
	widget->draw = widget_draw_clock;
	widget->click = widget_click_clock;
	widget->update = widget_update_clock;
	list_insert(widgets_enabled, widget);
	return widget;
}
