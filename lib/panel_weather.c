/**
 * @brief Panel Weather Widget
 */
#include <toaru/yutani.h>
#include <toaru/yutani-internal.h>
#include <toaru/graphics.h>
#include <toaru/hashmap.h>
#include <toaru/menu.h>
#include <toaru/text.h>
#include <toaru/panel.h>

struct MenuList * weather;

static struct MenuEntry_Normal * weather_title_entry;
static struct MenuEntry_Normal * weather_updated_entry;
static struct MenuEntry_Normal * weather_conditions_entry;
static struct MenuEntry_Normal * weather_humidity_entry;
static struct MenuEntry_Normal * weather_clouds_entry;
static struct MenuEntry_Normal * weather_pressure_entry;
static char * weather_title_str;
static char * weather_updated_str;
static char * weather_conditions_str;
static char * weather_humidity_str;
static char * weather_clouds_str;
static char * weather_pressure_str;
static char * weather_temp_str;
static int weather_status_valid = 0;
static hashmap_t * weather_icons = NULL;
static sprite_t * weather_icon = NULL;
static int widgets_weather_enabled = 0;

static int widget_update_weather(struct PanelWidget * this, int * force_updates) {
	FILE * f = fopen("/tmp/weather-parsed.conf","r");
	if (!f) {
		weather_status_valid = 0;
		if (widgets_weather_enabled) {
			widgets_weather_enabled = 0;
			this->width = 0;
			return 1;
		}
		return 0;
	}

	weather_status_valid = 1;

	if (weather_title_str) free(weather_title_str);
	if (weather_updated_str) free(weather_updated_str);
	if (weather_conditions_str) free(weather_conditions_str);
	if (weather_humidity_str) free(weather_humidity_str);
	if (weather_clouds_str) free(weather_clouds_str);
	if (weather_pressure_str) free(weather_pressure_str);
	if (weather_temp_str) free(weather_temp_str);

	/* read the entire status file */
	fseek(f, 0, SEEK_END);
	size_t size = ftell(f);
	fseek(f, 0, SEEK_SET);
	char * data = malloc(size + 1);
	fread(data, size, 1, f);
	data[size] = 0;
	fclose(f);

	/* Find relevant pieces */
	char * t = data;
	char * temp = t;
	t = strstr(t, "\n"); *t = '\0'; t++;
	char * temp_r = t;
	t = strstr(t, "\n"); *t = '\0'; t++;
	char * conditions = t;
	t = strstr(t, "\n"); *t = '\0'; t++;
	char * icon = t;
	t = strstr(t, "\n"); *t = '\0'; t++;
	char * humidity = t;
	t = strstr(t, "\n"); *t = '\0'; t++;
	char * clouds = t;
	t = strstr(t, "\n"); *t = '\0'; t++;
	char * city = t;
	t = strstr(t, "\n"); *t = '\0'; t++;
	char * updated = t;
	t = strstr(t, "\n"); *t = '\0'; t++;
	char * pressure = t;
	t = strstr(t, "\n"); if (t) { *t = '\0'; t++; }

	if (!weather_icons) {
		weather_icons = hashmap_create(10);
	}

	if (!hashmap_has(weather_icons, icon)) {
		sprite_t * tmp = malloc(sizeof(sprite_t));
		char path[512];
		sprintf(path,"/usr/share/icons/weather/%s.png", icon);
		load_sprite(tmp, path);
		hashmap_set(weather_icons, icon, tmp);
	}

	weather_icon = hashmap_get(weather_icons, icon);

	char tmp[300];
	sprintf(tmp, "Weather for <b>%s</b>", city);
	weather_title_str = strdup(tmp);
	sprintf(tmp, "<small><i>%s</i></small>", updated);
	weather_updated_str = strdup(tmp);
	sprintf(tmp, "<b>%s°</b> - %s", temp, conditions);
	weather_conditions_str = strdup(tmp);
	sprintf(tmp, "<b>Humidity:</b> %s%%", humidity);
	weather_humidity_str = strdup(tmp);
	sprintf(tmp, "<b>Clouds:</b> %s%%", clouds);
	weather_clouds_str = strdup(tmp);
	sprintf(tmp, "<b>Pressure:</b> %s hPa", pressure);
	weather_pressure_str = strdup(tmp);

	sprintf(tmp, "%s°", temp_r);
	weather_temp_str = strdup(tmp);

	free(data);

	if (!widgets_weather_enabled) {
		widgets_weather_enabled = 1;
		this->width = 60;
		return 1;
	}

	return 0;
}

static void weather_refresh(struct MenuEntry * self) {
	(void)self;
	system("weather-tool &");
}

static void weather_configure(struct MenuEntry * self) {
	(void)self;
	system("terminal sh -c \"sudo weather-configurator; weather-tool\" &");
}

static int widget_click_weather(struct PanelWidget * this, struct yutani_msg_window_mouse_event * evt) {
	if (!weather) {
		weather = menu_create();
		weather->flags |= MENU_FLAG_BUBBLE_LEFT;
		weather_title_entry = (struct MenuEntry_Normal *)menu_create_normal(NULL, NULL, "", NULL);
		menu_insert(weather, weather_title_entry);
		weather_updated_entry = (struct MenuEntry_Normal *)menu_create_normal(NULL, NULL, "", NULL);
		menu_insert(weather, weather_updated_entry);
		menu_insert(weather, menu_create_separator());
		weather_conditions_entry = (struct MenuEntry_Normal *)menu_create_normal(NULL, NULL, "", NULL);
		menu_insert(weather, weather_conditions_entry);
		weather_humidity_entry = (struct MenuEntry_Normal *)menu_create_normal("weather-humidity", NULL, "", NULL);
		menu_insert(weather, weather_humidity_entry);
		weather_clouds_entry = (struct MenuEntry_Normal *)menu_create_normal("weather-clouds", NULL, "", NULL);
		menu_insert(weather, weather_clouds_entry);
		weather_pressure_entry = (struct MenuEntry_Normal *)menu_create_normal("weather-pressure", NULL, "", NULL);
		menu_insert(weather, weather_pressure_entry);
		menu_insert(weather, menu_create_separator());
		menu_insert(weather, menu_create_normal("refresh", NULL, "Refresh...", weather_refresh));
		menu_insert(weather, menu_create_normal("config", NULL, "Configure...", weather_configure));
		menu_insert(weather, menu_create_separator());
		menu_insert(weather, menu_create_normal(NULL, NULL, "<small><i>Weather data provided by</i></small>", NULL));
		menu_insert(weather, menu_create_normal(NULL, NULL, "<b>OpenWeather™</b>", NULL));
	}
	if (weather_status_valid) {
		menu_update_title(weather_title_entry, weather_title_str);
		menu_update_title(weather_updated_entry, weather_updated_str);
		menu_update_title(weather_conditions_entry, weather_conditions_str);
		menu_update_title(weather_humidity_entry, weather_humidity_str);
		menu_update_title(weather_clouds_entry, weather_clouds_str);
		menu_update_title(weather_pressure_entry, weather_pressure_str);
	}
	if (!weather->window) {
		panel_menu_show(this,weather);
	}
	return 1;
}

static int widget_draw_weather(struct PanelWidget * this, gfx_context_t * ctx) {
	if (widgets_weather_enabled) {
		uint32_t color = (weather && weather->window) ? this->pctx->color_text_hilighted : this->pctx->color_icon_normal;
		panel_highlight_widget(this,ctx, (weather && weather->window));
		tt_set_size(this->pctx->font, 12);
		int t = tt_string_width(this->pctx->font, weather_temp_str);
		tt_draw_string(ctx, this->pctx->font, 4 + 24 + (24 - t) / 2, 6 + 12, weather_temp_str, color);
		draw_sprite_alpha_paint(ctx, weather_icon, 4, 1, 1.0, color);
	}
	return 0;
}

struct PanelWidget * widget_init_weather(void) {
	weather_refresh(NULL);

	struct PanelWidget * widget = widget_new();
	widget->width = 0;
	widget->draw = widget_draw_weather;
	widget->click = widget_click_weather;
	widget->update = widget_update_weather;
	list_insert(widgets_enabled, widget);
	return widget;
}

