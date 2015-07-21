/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2014 Kevin Lange
 */
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

static yutani_t * yctx;
/* XXX ttk_app_t ? */
static hashmap_t * ttk_wids_to_windows;

void cairo_rounded_rectangle(cairo_t * cr, double x, double y, double width, double height, double radius) {
	double degrees = M_PI / 180.0;

	cairo_new_sub_path(cr);
	cairo_arc (cr, x + width - radius, y + radius, radius, -90 * degrees, 0 * degrees);
	cairo_arc (cr, x + width - radius, y + height - radius, radius, 0 * degrees, 90 * degrees);
	cairo_arc (cr, x + radius, y + height - radius, radius, 90 * degrees, 180 * degrees);
	cairo_arc (cr, x + radius, y + radius, radius, 180 * degrees, 270 * degrees);
	cairo_close_path(cr);
}


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

void _ttk_draw_button_hover(cairo_t * cr, int x, int y, int width, int height, char * title) {
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

void _ttk_draw_button_disabled(cairo_t * cr, int x, int y, int width, int height, char * title) {
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
		_ttk_draw_button_hover(cr, 4 + (window->width / 2), TTK_MENU_HEIGHT + 48 + 4, (window->width / 2) - 8, 40, "Hover Button");

		_ttk_draw_button_select(cr, 4, TTK_MENU_HEIGHT + 2 * 48 + 4, (window->width / 2) - 8, 40, "Selected");
		_ttk_draw_button_disabled(cr, 4 + (window->width / 2), TTK_MENU_HEIGHT + 2 * 48 + 4, (window->width / 2) - 8, 40, "Disabled Button");

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
	yutani_flip(yctx, window->core_window);
}

void ttk_move_callback(ttk_window_t * window_ttk, int x, int y) {
	if (!window_ttk) {
		return;
	}

	/* XXX Do we want to do things with the old position? */

	window_ttk->x = x;
	window_ttk->y = y;

}

void ttk_resize_callback(ttk_window_t * window_ttk, int width, int height) {
	if (!window_ttk) {
		fprintf(stderr, "[ttk] received window callback for a window not registered with TTK, ignoring.\n");
		return;
	}

	yutani_window_resize_accept(yctx, window_ttk->core_window, width, height);

	/* Update window size */
	window_ttk->width  = width  - decor_width();
	window_ttk->height = height - decor_height();

	/* Reinitialize graphics context */
	reinit_graphics_yutani(window_ttk->core_context, window_ttk->core_window);

	ttk_window_draw(window_ttk);

	yutani_window_resize_done(yctx, window_ttk->core_window);
	yutani_flip(yctx, window_ttk->core_window);
}

void ttk_focus_callback(ttk_window_t * window_ttk, int focused) {
	if (!window_ttk) {
		fprintf(stderr, "[ttk] received window callback for a window not registered with TTK, ignoring.\n");
		return;
	}

	window_ttk->core_window->focused = focused;
	ttk_window_draw(window_ttk);
}


void ttk_initialize() {
	/* Connect to the windowing server */
	yctx = yutani_init();

	/* Initialize the decoration library */
	init_decorations();

	ttk_wids_to_windows = hashmap_create_int(5); /* wids are ints, ergo... */
}

ttk_window_t * ttk_window_new(char * title, uint16_t width, uint16_t height) {
	ttk_window_t * new_win = malloc(sizeof(ttk_window_t));
	new_win->title  = strdup(title);
	new_win->width  = width;
	new_win->height = height;
	new_win->off_x  = decor_left_width;
	new_win->off_y  = decor_top_height;

	new_win->core_window = yutani_window_create(yctx, new_win->width + decor_width(), new_win->height + decor_height());
	yutani_window_move(yctx, new_win->core_window, TTK_DEFAULT_X, TTK_DEFAULT_Y);
	assert(new_win->core_window && "Oh dear, I've failed to allocate a new window from the server. This is terrible.");

	/* XXX icon; also need to do this if we change the title later */
	yutani_window_advertise(yctx, new_win->core_window, new_win->title);

	new_win->core_context = init_graphics_yutani_double_buffer(new_win->core_window);
	draw_fill(new_win->core_context, rgb(TTK_BACKGROUND_DEFAULT));

	ttk_window_draw(new_win);

	hashmap_set(ttk_wids_to_windows, (void*)new_win->core_window->wid, new_win);
}

void ttk_quit() {
	list_t * windows = hashmap_values(ttk_wids_to_windows);

	foreach(node, windows) {
		ttk_window_t * win = node->value;
		yutani_close(yctx, win->core_window);
	}

	list_free(windows);
	free(windows);
	hashmap_free(ttk_wids_to_windows);
	free(ttk_wids_to_windows);
}

int ttk_run(ttk_window_t * window) {
	while (1) {
		yutani_msg_t * m = yutani_poll(yctx);
		if (m) {
			switch (m->type) {
				case YUTANI_MSG_KEY_EVENT:
					{
						struct yutani_msg_key_event * ke = (void*)m->data;
						if (ke->event.action == KEY_ACTION_DOWN && ke->event.keycode == 'q') {
							goto done;
						}
					}
					break;
				case YUTANI_MSG_WINDOW_FOCUS_CHANGE:
					{
						struct yutani_msg_window_focus_change * fc = (void*)m->data;
						ttk_focus_callback(hashmap_get(ttk_wids_to_windows, (void*)fc->wid), fc->focused);
					}
					break;
				case YUTANI_MSG_RESIZE_OFFER:
					{
						struct yutani_msg_window_resize * wr = (void*)m->data;
						ttk_resize_callback(hashmap_get(ttk_wids_to_windows, (void*)wr->wid), wr->width, wr->height);
					}
					break;
				case YUTANI_MSG_WINDOW_MOVE:
					{
						struct yutani_msg_window_move * wm = (void*)m->data;
						ttk_move_callback(hashmap_get(ttk_wids_to_windows, (void*)wm->wid), wm->x, wm->y);
					}
					break;
				case YUTANI_MSG_WINDOW_MOUSE_EVENT:
					{
						struct yutani_msg_window_mouse_event * me = (void*)m->data;
						if (decor_handle_event(yctx, m) == DECOR_CLOSE) {
							goto done;
						}
						if (me->command == YUTANI_MOUSE_EVENT_DOWN && me->buttons & YUTANI_MOUSE_BUTTON_LEFT) {
							ttk_window_t * win = hashmap_get(ttk_wids_to_windows, (void*)me->wid);
							if (win) {
								/* Do something */
							}
						}
					}
					break;
				case YUTANI_MSG_SESSION_END:
					goto done;
				default:
					break;
			}
			free(m);
		}
	}

done:
	ttk_quit();
	return 0;
}

/* }}} end TTK */
