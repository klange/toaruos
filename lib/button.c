/**
 * @brief Button "widget"
 *
 * Really just a function to render a button...
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 */
#include <toaru/graphics.h>
#include <toaru/button.h>
#include <toaru/text.h>
#include <toaru/icon_cache.h>

static struct TT_Font * _tt_font_thin = NULL;

void ttk_button_draw(gfx_context_t * ctx, struct TTKButton * button) {
	if (button->width == 0) {
		return;
	}

	int hilight = button->hilight & 0xFF;
	int disabled = button->hilight & 0x100;

	/* Dark edge */
	if (hilight < 3) {
		struct gradient_definition edge = {button->height, button->y, rgb(166,166,166), rgb(136,136,136)};
		draw_rounded_rectangle_pattern(ctx, button->x, button->y, button->width, button->height, 4, gfx_vertical_gradient_pattern, &edge);
	}

	/* Sheen */
	if (hilight < 2) {
		draw_rounded_rectangle(ctx, button->x + 1, button->y + 1, button->width - 2, button->height - 2, 3, rgb(238,238,238));
	/* Button face - this should normally be a gradient */
		if (hilight == 1) {
			struct gradient_definition face = {button->height-3, button->y + 2, rgb(240,240,240), rgb(230,230,230)};
			draw_rounded_rectangle_pattern(ctx, button->x + 2, button->y + 2, button->width - 4, button->height - 3, 2, gfx_vertical_gradient_pattern, &face);
		} else {
			struct gradient_definition face = {button->height-3, button->y + 2, rgb(219,219,219), rgb(204,204,204)};
			draw_rounded_rectangle_pattern(ctx, button->x + 2, button->y + 2, button->width - 4, button->height - 3, 2, gfx_vertical_gradient_pattern, &face);
		}
	} else if (hilight == 2) {
		struct gradient_definition face = {button->height-2, button->y + 1, rgb(180,180,180), rgb(160,160,160)};
		draw_rounded_rectangle_pattern(ctx, button->x + 1, button->y + 1, button->width - 2, button->height - 2, 3, gfx_vertical_gradient_pattern, &face);
	}

	if (button->title[0] != '\033') {
		if (!_tt_font_thin) {
			_tt_font_thin = tt_font_from_shm("sans-serif");
		}
		tt_set_size(_tt_font_thin, 13);
		int label_width = tt_string_width(_tt_font_thin, button->title);
		int centered = (button->width - label_width) / 2;

		int centered_y = (button->height - 16) / 2;
		tt_draw_string(ctx, _tt_font_thin, button->x + centered + (hilight == 2), button->y + centered_y + (hilight == 2) + 13, button->title, disabled ? rgb(120,120,120) : rgb(0,0,0));
	} else {
		sprite_t * icon = icon_get_16(button->title+1);
		int centered = button->x + (button->width - icon->width) / 2 + (hilight == 2);
		int centered_y = button->y + (button->height - icon->height) / 2 + (hilight == 2);
		if (disabled) {
			draw_sprite_alpha(ctx, icon, centered, centered_y, 0.5);
		} else {
			draw_sprite(ctx, icon, centered, centered_y);
		}
	}

}

