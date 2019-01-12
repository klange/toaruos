/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 *
 * Compositor Cairo renderer backend
 */

#include <math.h>
#include <cairo.h>
#include <toaru/yutani-server.h>

struct cairo_renderer {
	cairo_t * framebuffer_ctx;
	cairo_surface_t * framebuffer_surface;
	cairo_t * real_ctx;
	cairo_surface_t * real_surface;
};

int renderer_alloc(yutani_globals_t * yg) {
	struct cairo_renderer * c = malloc(sizeof(struct cairo_renderer));
	c->framebuffer_ctx = NULL;
	c->framebuffer_surface = NULL;
	c->real_ctx = NULL;
	c->real_surface = NULL;
	yg->renderer_ctx = c;
	return 0;
}

int renderer_init(yutani_globals_t * yg) {
	struct cairo_renderer * c = yg->renderer_ctx;

	int stride = yg->backend_ctx->stride;
	c->framebuffer_surface = cairo_image_surface_create_for_data(
			yg->backend_framebuffer, CAIRO_FORMAT_ARGB32, yg->width, yg->height, stride);
	c->framebuffer_ctx = cairo_create(c->framebuffer_surface);

	c->real_surface = cairo_image_surface_create_for_data(
			(unsigned char *)yg->backend_ctx->buffer, CAIRO_FORMAT_ARGB32, yg->width, yg->height, stride);
	c->real_ctx = cairo_create(c->real_surface);

	return 0;
}

int renderer_add_clip(yutani_globals_t * yg, double x, double y, double w, double h) {
	struct cairo_renderer * c = yg->renderer_ctx;
	cairo_rectangle(c->framebuffer_ctx, x, y, w, h);
	if (yg->width > 2490) {
		x = 0;
		w = yg->width;
	}
	cairo_rectangle(c->real_ctx, x, y, w, h);
	return 0;
}

int renderer_set_clip(yutani_globals_t * yg) {
	struct cairo_renderer * c = yg->renderer_ctx;
	cairo_clip(c->framebuffer_ctx);
	cairo_clip(c->real_ctx);
	return 0;
}

int renderer_push_state(yutani_globals_t * yg) {
	struct cairo_renderer * c = yg->renderer_ctx;
	cairo_save(c->framebuffer_ctx);
	cairo_save(c->real_ctx);
	return 0;
}

int renderer_pop_state(yutani_globals_t * yg) {
	struct cairo_renderer * c = yg->renderer_ctx;
	cairo_restore(c->framebuffer_ctx);
	cairo_restore(c->real_ctx);
	return 0;
}

int renderer_destroy(yutani_globals_t * yg) {
	struct cairo_renderer * c = yg->renderer_ctx;
	cairo_destroy(c->framebuffer_ctx);
	cairo_surface_destroy(c->framebuffer_surface);
	cairo_destroy(c->real_ctx);
	cairo_surface_destroy(c->real_surface);
	return 0;
}

int renderer_blit_screen(yutani_globals_t * yg) {
	struct cairo_renderer * c = yg->renderer_ctx;
	cairo_set_operator(c->real_ctx, CAIRO_OPERATOR_SOURCE);
	cairo_translate(c->real_ctx, 0, 0);
	cairo_set_source_surface(c->real_ctx, c->framebuffer_surface, 0, 0);
	cairo_paint(c->real_ctx);
	return 0;
}

int renderer_blit_window(yutani_globals_t * yg, yutani_server_window_t * window, int x, int y) {
	/* Obtain the previously initialized cairo contexts */
	struct cairo_renderer * c = yg->renderer_ctx;
	cairo_t * cr = c->framebuffer_ctx;

	/* Window stride is always 4 bytes per pixel... */
	int stride = window->width * 4;

	/* Initialize a cairo surface object for this window */
	cairo_surface_t * surf = cairo_image_surface_create_for_data(
			window->buffer, CAIRO_FORMAT_ARGB32, window->width, window->height, stride);

	/* Save cairo context */
	cairo_save(cr);

	/*
	 * Offset the rendering context appropriately for the position of the window
	 * based on the modifier paramters
	 */
	cairo_identity_matrix(cr);
	cairo_translate(cr, x, y);

	/* Top and bottom windows can not be rotated. */
	if (!yutani_window_is_top(yg, window) && !yutani_window_is_bottom(yg, window)) {
		/* Calcuate radians from degrees */

		if (window->rotation != 0) {
			double r = M_PI * (((double)window->rotation) / 180.0);

			/* Rotate the render context about the center of the window */
			cairo_translate(cr, (int)( window->width / 2), (int)( (int)window->height / 2));
			cairo_rotate(cr, r);
			cairo_translate(cr, (int)(-window->width / 2), (int)(-window->height / 2));

			/* Prefer faster filter when rendering rotated windows */
			cairo_pattern_t * p = cairo_get_source(cr);
			cairo_pattern_set_filter(p, CAIRO_FILTER_FAST);
		}

		if (window == yg->resizing_window) {
			double x_scale = (double)yg->resizing_w / (double)yg->resizing_window->width;
			double y_scale = (double)yg->resizing_h / (double)yg->resizing_window->height;
			if (x_scale < 0.00001) {
				x_scale = 0.00001;
			}
			if (y_scale < 0.00001) {
				y_scale = 0.00001;
			}
			cairo_translate(cr, (int)yg->resizing_offset_x, (int)yg->resizing_offset_y);
			cairo_scale(cr, x_scale, y_scale);
		}

	}
	if (window->anim_mode) {
		int frame = yutani_time_since(yg, window->anim_start);
		if (frame >= yutani_animation_lengths[window->anim_mode]) {
			if (window->anim_mode == YUTANI_EFFECT_FADE_OUT ||
				window->anim_mode == YUTANI_EFFECT_SQUEEZE_OUT) {
				list_insert(yg->windows_to_remove, window);
				goto draw_finish;
			}
			window->anim_mode = 0;
			window->anim_start = 0;
			goto draw_window;
		} else {
			switch (window->anim_mode) {
				case YUTANI_EFFECT_SQUEEZE_OUT:
				case YUTANI_EFFECT_FADE_OUT:
					{
						frame = yutani_animation_lengths[window->anim_mode] - frame;
					}
				case YUTANI_EFFECT_SQUEEZE_IN:
				case YUTANI_EFFECT_FADE_IN:
					{
						double time_diff = ((double)frame / (float)yutani_animation_lengths[window->anim_mode]);

						if (window->server_flags & YUTANI_WINDOW_FLAG_DIALOG_ANIMATION) {
							double x = time_diff;
							int t_y = (window->height * (1.0 -x)) / 2;
							cairo_translate(cr, 0, t_y);
							cairo_scale(cr, 1.0, x);
						} else if (!yutani_window_is_top(yg, window) && !yutani_window_is_bottom(yg, window) &&
							!(window->server_flags & YUTANI_WINDOW_FLAG_ALT_ANIMATION)) {
							double x = 0.75 + time_diff * 0.25;
							int t_x = (window->width * (1.0 - x)) / 2;
							int t_y = (window->height * (1.0 - x)) / 2;
							cairo_translate(cr, t_x, t_y);
							cairo_scale(cr, x, x);
						}

						cairo_set_source_surface(cr, surf, 0, 0);
						if (window->opacity != 255) {
							cairo_paint_with_alpha(cr, time_diff * (double)(window->opacity) / 255.0);
						} else {
							cairo_paint_with_alpha(cr, time_diff);
						}
					}
					break;
				default:
					goto draw_window;
					break;
			}
		}
	} else {
draw_window:
		/* Paint window */
		cairo_set_source_surface(cr, surf, 0, 0);

		if (window->opacity != 255) {
			cairo_paint_with_alpha(cr, (float)(window->opacity)/255.0);
		} else {
			cairo_paint(cr);
		}
	}

draw_finish:

	/* Clean up */
	cairo_surface_destroy(surf);

	/* Restore context stack */
	cairo_restore(cr);

#if YUTANI_DEBUG_WINDOW_BOUNDS
	/*
	 * If window bound debugging is enabled, we also draw a box
	 * representing the rectangular (possibly rotated) boundary
	 * for a window texture.
	 */
	if (yg->debug_bounds) {
		cairo_save(cr);

		int32_t t_x, t_y;
		int32_t s_x, s_y;
		int32_t r_x, r_y;
		int32_t q_x, q_y;

		yutani_window_to_device(window, 0, 0, &t_x, &t_y);
		yutani_window_to_device(window, window->width, window->height, &s_x, &s_y);
		yutani_window_to_device(window, 0, window->height, &r_x, &r_y);
		yutani_window_to_device(window, window->width, 0, &q_x, &q_y);

		uint32_t x = yutani_color_for_wid(window->wid);
		cairo_set_source_rgba(cr,
				_RED(x) / 255.0,
				_GRE(x) / 255.0,
				_BLU(x) / 255.0,
				0.7
		);

		cairo_move_to(cr, t_x, t_y);
		cairo_line_to(cr, r_x, r_y);
		cairo_line_to(cr, s_x, s_y);
		cairo_line_to(cr, q_x, q_y);
		cairo_fill(cr);

		cairo_restore(cr);
	}
#endif


	return 0;
}
