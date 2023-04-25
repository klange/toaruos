/**
 * @brief Panel date widget
 */
#include <time.h>
#include <sys/time.h>
#include <toaru/yutani.h>
#include <toaru/graphics.h>
#include <toaru/menu.h>
#include <toaru/text.h>
#include <toaru/panel.h>
static struct MenuList * calmenu;
static int date_widget_width = 48;
#define CALENDAR_LINE_HEIGHT 22
#define CALENDAR_BASE_HEIGHT 45
#define CALENDAR_PAD_HEIGHT  2

static int days_in_month(struct tm * timeinfo) {
	static int days_in_months[] = {
		31, 0, 31, 30, 31, 30, 31,
		31, 30, 31, 30, 31,
	};
	if (timeinfo->tm_mon != 1) return days_in_months[timeinfo->tm_mon];
	/* How many days in February? */
	struct tm tmp;
	memcpy(&tmp, timeinfo, sizeof(struct tm));
	tmp.tm_mday = 29;
	tmp.tm_hour = 12;
	time_t tmp3 = mktime(&tmp);
	struct tm * tmp2 = localtime(&tmp3);
	return tmp2->tm_mday == 29 ? 29 : 28;
}

static int weeks_in_month(struct tm * timeinfo) {
	int line = 0;
	int wday = (36 + timeinfo->tm_wday - timeinfo->tm_mday) % 7;
	for (int day = 1; day <= days_in_month(timeinfo); day++, (wday = (wday + 1) % 7)) {
		if (wday == 6) {
			line++;
		}
	}
	return (wday ? line + 1 : line);
}

void _menu_draw_MenuEntry_Calendar(gfx_context_t * ctx, struct MenuEntry * self, int offset) {
	self->offset = offset;

	struct timeval now;
	gettimeofday(&now, NULL);

	struct tm actual;
	struct tm * timeinfo;
	timeinfo = localtime((time_t *)&now.tv_sec);
	memcpy(&actual, timeinfo, sizeof(struct tm));
	timeinfo = &actual;

	struct TT_Font * font = ((struct PanelWidget*)self->_private)->pctx->font;
	struct TT_Font * font_bold = ((struct PanelWidget*)self->_private)->pctx->font_bold;

	/* Render heading with Month Year */
	{
		char month[20];
		strftime(month, 20, "%B %Y", timeinfo);

		tt_set_size(font_bold, 16);
		tt_draw_string(ctx, font_bold, (self->width - tt_string_width(font_bold, month)) / 2, self->offset + 16, month, rgb(0,0,0));
	}

	/* Get ready to draw a table... */
	int cell_size = self->width / 7;
	int base_left = (self->width - cell_size * 7) / 2;

	/* Render weekday abbreviations */
	const char * weekdays[] = {"Su","Mo","Tu","We","Th","Fr","Sa",NULL};
	int left = base_left;
	tt_set_size(font, 11);
	for (const char ** w = weekdays; *w; w++) {
		tt_draw_string(ctx, font, left + (cell_size - tt_string_width(font,*w)) / 2,
			self->offset + 22 + 13, *w, rgb(0,0,0));
		left += cell_size;
	}

	int weeks = weeks_in_month(timeinfo);
	self->height = CALENDAR_LINE_HEIGHT * weeks + CALENDAR_BASE_HEIGHT + CALENDAR_PAD_HEIGHT;

	/* The 1st was a... */
	int wday = (36 + timeinfo->tm_wday - timeinfo->tm_mday) % 7;

	int line = 0;
	left = base_left + cell_size * wday;
	tt_set_size(font, 13);
	for (int day = 1; day <= days_in_month(timeinfo); day++, (wday = (wday + 1) % 7)) {
		char date[12];
		snprintf(date, 11, "%d", day);
		/* Is this the cell for today? */
		if (day == timeinfo->tm_mday) {
			draw_rounded_rectangle(ctx, left - 1, self->offset + CALENDAR_BASE_HEIGHT + line * CALENDAR_LINE_HEIGHT - 2, cell_size + 2, CALENDAR_LINE_HEIGHT, 12, ((struct PanelWidget*)self->_private)->pctx->color_special);
			tt_draw_string(ctx, font, left + (cell_size - tt_string_width(font, date)) / 2,
				self->offset + CALENDAR_BASE_HEIGHT + 13 + line * CALENDAR_LINE_HEIGHT, date, rgb(255,255,255));
		} else {
			tt_draw_string(ctx, font, left + (cell_size - tt_string_width(font, date)) / 2,
				self->offset + CALENDAR_BASE_HEIGHT + 13 + line * CALENDAR_LINE_HEIGHT, date, (wday == 0 || wday == 6) ? rgba(0,0,0,120) : rgb(0,0,0));
		}
		if (wday == 6) {
			left = base_left;
			line++;
		} else {
			left += cell_size;
		}
	}
}

static struct MenuEntryVTable calendar_vtable = {
	.methods = 3,
	.renderer = _menu_draw_MenuEntry_Calendar,
};

/*
 * Special menu entry to display a calendar
 */
struct MenuEntry * menu_create_calendar(struct PanelWidget * this) {
	struct MenuEntry * out = menu_create_separator(); /* Steal some defaults */

	out->_type = -1; /* Special */

	struct timeval now;
	gettimeofday(&now, NULL);
	out->height = CALENDAR_LINE_HEIGHT * weeks_in_month(localtime((time_t *)&now.tv_sec)) + CALENDAR_BASE_HEIGHT + CALENDAR_PAD_HEIGHT;

	out->rwidth = 200;
	out->vtable = &calendar_vtable;
	out->_private = this;
	return out;
}


static int weekday_width, date_width;
static char weekday[80], date[80];

static void update_date_widget(struct PanelWidget * this) {
	struct timeval now;
	struct tm * timeinfo;

	struct TT_Font * font = this->pctx->font;
	struct TT_Font * font_bold = this->pctx->font_bold;

	/* Get the current time for the clock */
	gettimeofday(&now, NULL);
	timeinfo = localtime((time_t *)&now.tv_sec);

	strftime(weekday, 80, "%A", timeinfo);
	strftime(date, 80, "%B %e", timeinfo);

	tt_set_size(font, 11);
	tt_set_size(font_bold, 11);

	/* Update date_widget_width */
	weekday_width = tt_string_width(font, weekday);
	date_width = tt_string_width(font_bold, date);

	/* Uh, we need to calculate this elsewhere */
	date_widget_width = (weekday_width > date_width ? weekday_width : date_width) + 24; /* A bit of padding... */
}

static int widget_draw_date(struct PanelWidget * this, gfx_context_t * ctx) {
	update_date_widget(this);

	panel_highlight_widget(this,ctx,!!calmenu->window);

	struct TT_Font * font = this->pctx->font;
	struct TT_Font * font_bold = this->pctx->font_bold;

	/* Day-of-week */
	int t = (this->width - weekday_width) / 2;
	tt_draw_string(ctx, font, t, 13, weekday,  calmenu->window ? this->pctx->color_text_hilighted : this->pctx->color_text_normal);

	/* Month Day */
	t = (this->width - date_width) / 2;
	tt_draw_string(ctx, font_bold, t, 23, date,  calmenu->window ? this->pctx->color_text_hilighted : this->pctx->color_text_normal);

	return 0;
}

static int widget_click_date(struct PanelWidget * this, struct yutani_msg_window_mouse_event * evt) {
	if (!calmenu->window) {
		panel_menu_show(this,calmenu);
		return 1;
	}
	return 0;
}

static int widget_update_date(struct PanelWidget * this, int * force_updates) {
	int width_before = date_widget_width;
	update_date_widget(this);
	this->width = date_widget_width;
	return width_before != date_widget_width;
}

struct PanelWidget * widget_init_date(void) {
	struct PanelWidget * widget = widget_new();

	calmenu = menu_create();
	calmenu->flags |= MENU_FLAG_BUBBLE_CENTER;
	menu_insert(calmenu, menu_create_calendar(widget));

	widget->width = 92; /* TODO calculate correct width */
	widget->draw = widget_draw_date;
	widget->click = widget_click_date;
	widget->update = widget_update_date;
	list_insert(widgets_enabled, widget);
	return widget;
}

