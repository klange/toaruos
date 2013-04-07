/*
 *
 * Toolkit Demo and Development Application
 *
 */
#include <stdlib.h>
#include <assert.h>
#include <math.h>

#include "gui/ttk/ttk.h"

/* TTK {{{ */

void cairo_rounded_rectangle(cairo_t * cr, double x, double y, double width, double height, double radius) {
	double degrees = M_PI / 180.0;

	cairo_new_sub_path(cr);
	cairo_arc (cr, x + width - radius, y + radius, radius, -90 * degrees, 0 * degrees);
	cairo_arc (cr, x + width - radius, y + height - radius, radius, 0 * degrees, 90 * degrees);
	cairo_arc (cr, x + radius, y + height - radius, radius, 90 * degrees, 180 * degrees);
	cairo_arc (cr, x + radius, y + radius, radius, 180 * degrees, 270 * degrees);
	cairo_close_path(cr);
}

list_t * ttk_window_list;

void ttk_redraw_borders(ttk_window_t * window) {
	render_decorations(window->core_window, window->core_context, window->title);
}

void _ttk_draw_button(cairo_t * cr, int x, int y, int width, int height, char * title) {
	cairo_save(cr);

	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

	cairo_rounded_rectangle(cr, 2 + x, 2 + y, width - 4, height - 4, 2.0);
	cairo_set_source_rgba(cr, 44.0/255.0, 71.0/255.0, 91.0/255.0, 29.0/255.0);
	cairo_set_line_width(cr, 4);
	cairo_stroke(cr);

	cairo_rounded_rectangle(cr, 2 + x, 2 + y, width - 4, height - 4, 2.0);
	cairo_set_source_rgba(cr, 158.0/255.0, 169.0/255.0, 177.0/255.0, 1.0);
	cairo_set_line_width(cr, 2);
	cairo_stroke(cr);

	{
		cairo_pattern_t * pat = cairo_pattern_create_linear(2 + x, 2 + y, 2 + x, 2 + y + height - 4);
		cairo_pattern_add_color_stop_rgba(pat, 0, 1, 1, 1, 1);
		cairo_pattern_add_color_stop_rgba(pat, 1, 241.0/255.0, 241.0/255.0, 244.0/255.0, 1);
		cairo_rounded_rectangle(cr, 2 + x, 2 + y, width - 4, height - 4, 2.0);
		cairo_set_source(cr, pat);
		cairo_fill(cr);
		cairo_pattern_destroy(pat);
	}

	{
		cairo_pattern_t * pat = cairo_pattern_create_linear(3 + x, 3 + y, 3 + x, 3 + y + height - 4);
		cairo_pattern_add_color_stop_rgba(pat, 0, 252.0/255.0, 252.0/255.0, 254.0/255.0, 1);
		cairo_pattern_add_color_stop_rgba(pat, 1, 223.0/255.0, 225.0/255.0, 230.0/255.0, 1);
		cairo_rounded_rectangle(cr, 3 + x, 3 + y, width - 5, height - 5, 2.0);
		cairo_set_source(cr, pat);
		cairo_fill(cr);
		cairo_pattern_destroy(pat);
	}

	{
		cairo_surface_t * surface = cairo_get_target(cr);
		gfx_context_t fake_context = {
			.width = cairo_image_surface_get_width(surface),
			.height = cairo_image_surface_get_height(surface),
			.depth = 32,
			.buffer = NULL,
			.backbuffer = cairo_image_surface_get_data(surface)
		};

		set_font_face(FONT_SANS_SERIF);
		set_font_size(13);

		int str_width = draw_string_width(title);
		draw_string(&fake_context, (width - str_width) / 2 + x, y + (height) / 2 + 4, rgb(49,49,49), title);
	}

	cairo_restore(cr);
}

void _ttk_draw_button_hover(cairo_t * cr, int x, int y, int width, int height) {
	cairo_save(cr);

	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

	cairo_rounded_rectangle(cr, 2 + x, 2 + y, width - 4, height - 4, 2.0);
	cairo_set_source_rgba(cr, 44.0/255.0, 71.0/255.0, 91.0/255.0, 29.0/255.0);
	cairo_set_line_width(cr, 4);
	cairo_stroke(cr);

	cairo_rounded_rectangle(cr, 2 + x, 2 + y, width - 4, height - 4, 2.0);
	cairo_set_source_rgba(cr, 158.0/255.0, 169.0/255.0, 177.0/255.0, 1.0);
	cairo_set_line_width(cr, 2);
	cairo_stroke(cr);

	{
		cairo_pattern_t * pat = cairo_pattern_create_linear(2 + x, 2 + y, 2 + x, 2 + y + height - 4);
		cairo_pattern_add_color_stop_rgba(pat, 0, 1, 1, 1, 1);
		cairo_pattern_add_color_stop_rgba(pat, 1, 229.0/255.0, 229.0/255.0, 246.0/255.0, 1);
		cairo_rounded_rectangle(cr, 2 + x, 2 + y, width - 4, height - 4, 2.0);
		cairo_set_source(cr, pat);
		cairo_fill(cr);
		cairo_pattern_destroy(pat);
	}

	{
		cairo_pattern_t * pat = cairo_pattern_create_linear(3 + x, 3 + y, 3 + x, 3 + y + height - 4);
		cairo_pattern_add_color_stop_rgba(pat, 0, 252.0/255.0, 252.0/255.0, 254.0/255.0, 1);
		cairo_pattern_add_color_stop_rgba(pat, 1, 212.0/255.0, 223.0/255.0, 251.0/255.0, 1);
		cairo_rounded_rectangle(cr, 3 + x, 3 + y, width - 5, height - 5, 2.0);
		cairo_set_source(cr, pat);
		cairo_fill(cr);
		cairo_pattern_destroy(pat);
	}

	{
		cairo_surface_t * surface = cairo_get_target(cr);
		gfx_context_t fake_context = {
			.width = cairo_image_surface_get_width(surface),
			.height = cairo_image_surface_get_height(surface),
			.depth = 32,
			.buffer = NULL,
			.backbuffer = cairo_image_surface_get_data(surface)
		};

		set_font_face(FONT_SANS_SERIF);
		set_font_size(13);

		char * title = "Button with Hover Highlight";
		int str_width = draw_string_width(title);
		draw_string(&fake_context, (width - str_width) / 2 + x, y + (height) / 2 + 4, rgb(49,49,49), title);
	}

	cairo_restore(cr);
	
}

void _ttk_draw_button_select(cairo_t * cr, int x, int y, int width, int height, char * title) {
	cairo_save(cr);

	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

	cairo_rounded_rectangle(cr, 2 + x, 2 + y, width - 4, height - 4, 2.0);
	cairo_set_source_rgba(cr, 134.0/255.0, 173.0/255.0, 201.0/255.0, 1.0);
	cairo_set_line_width(cr, 2);
	cairo_stroke(cr);

	cairo_rounded_rectangle(cr, 2 + x, 2 + y, width - 4, height - 4, 2.0);
	cairo_set_source_rgba(cr, 202.0/255.0, 211.0/255.0, 232.0/255.0, 1.0);
	cairo_fill(cr);

	{
		cairo_surface_t * surface = cairo_get_target(cr);
		gfx_context_t fake_context = {
			.width = cairo_image_surface_get_width(surface),
			.height = cairo_image_surface_get_height(surface),
			.depth = 32,
			.buffer = NULL,
			.backbuffer = cairo_image_surface_get_data(surface)
		};

		set_font_face(FONT_SANS_SERIF);
		set_font_size(13);

		int str_width = draw_string_width(title);
		draw_string(&fake_context, (width - str_width) / 2 + x, y + (height) / 2 + 4, rgb(49,49,49), title);
	}

	cairo_restore(cr);
	
}

void _ttk_draw_button_disabled(cairo_t * cr, int x, int y, int width, int height) {
	cairo_save(cr);

	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

	cairo_rounded_rectangle(cr, 2 + x, 2 + y, width - 4, height - 4, 2.0);
	cairo_set_source_rgba(cr, 44.0/255.0, 71.0/255.0, 91.0/255.0, 29.0/255.0);
	cairo_set_line_width(cr, 4);
	cairo_stroke(cr);

	cairo_rounded_rectangle(cr, 2 + x, 2 + y, width - 4, height - 4, 2.0);
	cairo_set_source_rgba(cr, 152.0/255.0, 152.0/255.0, 152.0/255.0, 1.0);
	cairo_set_line_width(cr, 2);
	cairo_stroke(cr);

	{
		cairo_pattern_t * pat = cairo_pattern_create_linear(2 + x, 2 + y, 2 + x, 2 + y + height - 4);
		cairo_pattern_add_color_stop_rgba(pat, 0, 229.0/255.0, 229.0/255.0, 229.0/255.0, 1);
		cairo_pattern_add_color_stop_rgba(pat, 1, 178.0/255.0, 178.0/255.0, 178.0/255.0, 1);
		cairo_rounded_rectangle(cr, 2 + x, 2 + y, width - 4, height - 4, 2.0);
		cairo_set_source(cr, pat);
		cairo_fill(cr);
		cairo_pattern_destroy(pat);
	}

	{
		cairo_pattern_t * pat = cairo_pattern_create_linear(3 + x, 3 + y, 3 + x, 3 + y + height - 4);
		cairo_pattern_add_color_stop_rgba(pat, 0, 210.0/255.0, 210.0/255.0, 210.0/255.0, 1);
		cairo_pattern_add_color_stop_rgba(pat, 1, 165.0/255.0, 166.0/255.0, 170.0/255.0, 1);
		cairo_rounded_rectangle(cr, 3 + x, 3 + y, width - 5, height - 5, 2.0);
		cairo_set_source(cr, pat);
		cairo_fill(cr);
		cairo_pattern_destroy(pat);
	}

	{
		cairo_surface_t * surface = cairo_get_target(cr);
		gfx_context_t fake_context = {
			.width = cairo_image_surface_get_width(surface),
			.height = cairo_image_surface_get_height(surface),
			.depth = 32,
			.buffer = NULL,
			.backbuffer = cairo_image_surface_get_data(surface)
		};

		set_font_face(FONT_SANS_SERIF);
		set_font_size(13);

		char * title = "Disabled Button";
		int str_width = draw_string_width(title);
		draw_string(&fake_context, (width - str_width) / 2 + x, y + (height) / 2 + 4, rgb(100,100,100), title);
	}

	cairo_restore(cr);
}

#define TTK_MENU_HEIGHT 24

void _ttk_draw_menu(cairo_t * cr, int x, int y, int width) {
	cairo_save(cr);

	int height = TTK_MENU_HEIGHT;
	cairo_set_source_rgba(cr, 59.0/255.0, 59.0/255.0, 59.0/255.0, 1);
	cairo_rectangle(cr, x, y, width, height);
	cairo_fill(cr);

	{
		cairo_surface_t * surface = cairo_get_target(cr);
		gfx_context_t fake_context = {
			.width = cairo_image_surface_get_width(surface),
			.height = cairo_image_surface_get_height(surface),
			.depth = 32,
			.buffer = NULL,
			.backbuffer = cairo_image_surface_get_data(surface)
		};

		set_font_face(FONT_SANS_SERIF);
		set_font_size(13);

		draw_string(&fake_context, x + 8, y + height - 6, rgb(248,248,248), "File");
	}


	cairo_restore(cr);
}

void ttk_window_draw(ttk_window_t * window) {
	draw_fill(window->core_context, rgb(TTK_BACKGROUND_DEFAULT));
	ttk_redraw_borders(window);

	/* TODO actual drawing */
	{
		/* TODO move these surfaces into the ttk_window_t object */
		int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, window->core_window->width);
		cairo_surface_t * core_surface = cairo_image_surface_create_for_data(window->core_context->backbuffer, CAIRO_FORMAT_ARGB32, window->core_window->width, window->core_window->height, stride);
		cairo_t * cr_main = cairo_create(core_surface);

		/* TODO move this surface to a ttk_frame_t or something; GUIs man, go look at some Qt or GTK APIs! */
		cairo_surface_t * internal_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, window->width, window->height);
		cairo_t * cr = cairo_create(internal_surface);

		_ttk_draw_menu(cr, 0, 0, window->width);

		_ttk_draw_button(cr, 4, TTK_MENU_HEIGHT + 4, window->width - 8, 40, "Regular Button");

		_ttk_draw_button(cr, 4, TTK_MENU_HEIGHT + 48 + 4, (window->width / 2) - 8, 40, "Regular Button");
		_ttk_draw_button_hover(cr, 4 + (window->width / 2), TTK_MENU_HEIGHT + 48 + 4, (window->width / 2) - 8, 40);

		_ttk_draw_button_select(cr, 4, TTK_MENU_HEIGHT + 2 * 48 + 4, (window->width / 2) - 8, 40, "Selected");
		_ttk_draw_button_disabled(cr, 4 + (window->width / 2), TTK_MENU_HEIGHT + 2 * 48 + 4, (window->width / 2) - 8, 40);

		_ttk_draw_button(cr, 4, TTK_MENU_HEIGHT + 3 * 48 + 4, window->width - 8, window->height - (3 * 48) - TTK_MENU_HEIGHT - 8, "Regular Button");

		/* Paint the window's internal surface onto the backbuffer */
		cairo_set_source_surface(cr_main, internal_surface, (double)window->off_x, (double)window->off_y);
		cairo_paint(cr_main);
		cairo_surface_flush(internal_surface);
		cairo_destroy(cr);
		cairo_surface_destroy(internal_surface);

		/* In theory, we don't actually want to destroy much of any of this; maybe the cairo_t */
		cairo_surface_flush(core_surface);
		cairo_destroy(cr_main);
		cairo_surface_destroy(core_surface);
	}

	flip(window->core_context);
}

void ttk_resize_callback(window_t * window) {
	ttk_window_t * window_ttk = NULL;

	foreach(node, ttk_window_list) {
		ttk_window_t * tmp = (ttk_window_t *)node->value;
		if (window->wid == tmp->core_window->wid) {
			window_ttk = tmp;
			break;
		}
	}

	if (!window_ttk) {
		fprintf(stderr, "[ttk] received window callback for a window not registered with TTK, ignoring.\n");
	}

	/* Update window size */
	window_ttk->width  = window->width  - decor_width();
	window_ttk->height = window->height - decor_height();

	/* Reinitialize graphics context */
	reinit_graphics_window(window_ttk->core_context, window_ttk->core_window);

	ttk_window_draw(window_ttk);
}

void ttk_focus_callback(window_t * window) {
	ttk_window_t * window_ttk = NULL;

	foreach(node, ttk_window_list) {
		ttk_window_t * tmp = (ttk_window_t *)node->value;
		if (window->wid == tmp->core_window->wid) {
			window_ttk = tmp;
			break;
		}
	}

	if (!window_ttk) {
		fprintf(stderr, "[ttk] received window callback for a window not registered with TTK, ignoring.\n");
	}

	ttk_window_draw(window_ttk);
}


void ttk_initialize() {
	/* Connect to the windowing server */
	/* TODO handle errors */
	setup_windowing();

	/* Set up TTK callbacks */
	resize_window_callback = ttk_resize_callback;
	focus_changed_callback = ttk_focus_callback;

	/* TODO more callbacks, keyboard, mouse */

	/* Initialize the decoration library */
	init_decorations();

	ttk_window_list = list_create();
}

ttk_window_t * ttk_window_new(char * title, uint16_t width, uint16_t height) {
	ttk_window_t * new_win = malloc(sizeof(ttk_window_t));
	new_win->title  = strdup(title);
	new_win->width  = width;
	new_win->height = height;
	new_win->off_x  = decor_left_width;
	new_win->off_y  = decor_top_height;

	new_win->core_window = window_create(TTK_DEFAULT_X, TTK_DEFAULT_Y, new_win->width + decor_width(), new_win->height + decor_height());
	assert(new_win->core_window && "Oh dear, I've failed to allocate a new window from the server. This is terrible.");

	new_win->core_context = init_graphics_window_double_buffer(new_win->core_window);
	draw_fill(new_win->core_context, rgb(TTK_BACKGROUND_DEFAULT));

	ttk_window_draw(new_win);

	list_insert(ttk_window_list, new_win);
}

void ttk_quit() {
	list_destroy(ttk_window_list);
	list_free(ttk_window_list);
	free(ttk_window_list);
	teardown_windowing();
}

int ttk_run(ttk_window_t * window) {
	while (1) {

		char ch = 0;
		w_keyboard_t * kbd;

		while (kbd = poll_keyboard_async()) {
			free(kbd);
		}

		kbd = poll_keyboard();
		ch = kbd->key;
		free(kbd);

		switch (ch) {
			case 'q':
				goto done;
			default:
				break;
		}
	}

done:
	ttk_quit();
	return 0;
}

/* }}} end TTK */
