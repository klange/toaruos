/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 *
 * TODO: This is a work in progress
 *
 * Port of the original ToaruOS Python text_region library to C.
 *
 * Allows for the display of rich text with multiple varied formats,
 * as well as carat positioning, reflow, links, images, and so on.
 */

#include <stdio.h>
#include <toaru/textregion.h>
#include <toaru/sdf.h>

int tr_font_get_width(struct TR_Font * font, char * string) {
	return draw_sdf_string_width(string, font->size, font->typeface);
}

int tr_font_write(struct TR_Font * font, gfx_context_t * ctx, int x, int y, char * string) {
	return draw_sdf_string(ctx, x, y, string, font->size, font->color, font->typeface);
}

void tr_textunit_set_tag_group(struct TR_TextUnit * self, list_t * tag_group) {
	if (!self->tag_group) {
		self->tag_group = tag_group;
		list_insert(tag_group, self);
	} else {
		/* Already in a tag group, this is wrong */
	}
}

void tr_textunit_set_font(struct TR_TextUnit * self, struct TR_Font * font) {
	self->font = font;
	self->width = tr_font_get_width(font, self->string);
}

void tr_textunit_set_extra(struct TR_TextUnit * self, char * key, void * data) {
	if (!self->extra) {
		self->extra = hashmap_create(10);
	}
	hashmap_set(self->extra, key, data);
}

void tr_textregion_set_alignment(struct TR_TextRegion * self, int align) {
	self->align = align;
}

void tr_textregion_set_valignment(struct TR_TextRegion * self, int align) {
	self->valign = align;
}

void tr_textregion_set_max_lines(struct TR_TextRegion * self, int max_lines) {
	self->max_lines = max_lines;
	tr_textregion_reflow(self);
}

int tr_textregion_get_visible_lines(struct TR_TextRegion * self) {
	return self->height / self->line_height;
}

void tr_textregion_reflow(struct TR_TextRegion * self) {
	if (self->lines) {
		fprintf(stderr, "Need to clean out lines\n");
#if 0
		list_destroy(self->lines);
		list_free(self->lines);
		free(self->lines);
#endif
	}

#if 0
	self->lines = list_create();

	int current_width = 0;
	list_t * current_units = list_create();
	struct TR_TextUnit * leftover = NULL;

	int i = 0;
	while (i < self->text_units
#endif

}
