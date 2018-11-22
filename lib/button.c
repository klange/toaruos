/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 *
 * ttk button widget
 */
#include <toaru/graphics.h>
#include <toaru/button.h>
#include <toaru/sdf.h>

struct gradient_definition {
	int height;
	int y;
	uint32_t top;
	uint32_t bottom;
};

static uint32_t gradient_pattern(int32_t x, int32_t y, double alpha, void * extra) {
	struct gradient_definition * gradient = extra;
	int base_r = _RED(gradient->top), base_g = _GRE(gradient->top), base_b = _BLU(gradient->top);
	int last_r = _RED(gradient->bottom), last_g = _GRE(gradient->bottom), last_b = _GRE(gradient->bottom);
	double gradpoint = (double)(y - (gradient->y)) / (double)gradient->height;

	return premultiply(rgba(
		base_r * (1.0 - gradpoint) + last_r * (gradpoint),
		base_g * (1.0 - gradpoint) + last_g * (gradpoint),
		base_b * (1.0 - gradpoint) + last_b * (gradpoint),
		alpha * 255));
}

void ttk_button_draw(gfx_context_t * ctx, struct TTKButton * button) {
	if (button->width == 0) {
		return;
	}
	/* Dark edge */
	struct gradient_definition edge = {button->height, button->y, rgb(166,166,166), rgb(136,136,136)};
	draw_rounded_rectangle_pattern(ctx, button->x, button->y, button->width, button->height, 4, gradient_pattern, &edge);
	/* Sheen */
	if (button->hilight < 2) {
		draw_rounded_rectangle(ctx, button->x + 1, button->y + 1, button->width - 2, button->height - 2, 3, rgb(238,238,238));
	/* Button face - this should normally be a gradient */
		if (button->hilight == 1) {
			struct gradient_definition face = {button->height-3, button->y + 2, rgb(240,240,240), rgb(230,230,230)};
			draw_rounded_rectangle_pattern(ctx, button->x + 2, button->y + 2, button->width - 4, button->height - 3, 2, gradient_pattern, &face);
		} else {
			struct gradient_definition face = {button->height-3, button->y + 2, rgb(219,219,219), rgb(204,204,204)};
			draw_rounded_rectangle_pattern(ctx, button->x + 2, button->y + 2, button->width - 4, button->height - 3, 2, gradient_pattern, &face);
		}
	} else if (button->hilight == 2) {
		struct gradient_definition face = {button->height-2, button->y + 1, rgb(180,180,180), rgb(160,160,160)};
		draw_rounded_rectangle_pattern(ctx, button->x + 1, button->y + 1, button->width - 2, button->height - 2, 3, gradient_pattern, &face);
	}

	int label_width = draw_sdf_string_width(button->title, 16, SDF_FONT_THIN);
	int centered = (button->width - label_width) / 2;

	int centered_y = (button->height - 16) / 2;
	draw_sdf_string(ctx, button->x + centered + (button->hilight == 2), button->y + centered_y + (button->hilight == 2), button->title, 16, rgb(0,0,0), SDF_FONT_THIN);

}

