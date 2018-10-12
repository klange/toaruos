#pragma once

#include <_cheader.h>
#include <stdint.h>
#include <stdbool.h>

#include <toaru/hashmap.h>
#include <toaru/graphics.h>

_Begin_C_Header

struct TR_Font {
	int typeface; /* Should probably be more flexible than int, but tough luck for now. */
	int size;
	uint32_t color;
	/* TODO shadow - we had built-in support for this in the old setup, not sure I want to do it here */
};

/* TODO This should probably all use wchar_t, but the font library needs to support that as well. */

extern int tr_font_get_width(struct TR_Font * font, char * string);
extern int tr_font_write(struct TR_Font * font, gfx_context_t * ctx, int x, int y, char * string);

struct TR_TextUnit {
	char * string;
	int unit_type;
	int width; /* calculated on creation */

	struct TR_Font * font; /* not a pointer */
	hashmap_t * extra; /* extra properties in hashmap if present */
	list_t * tag_group; /* tag group membership if present */
};

extern void tr_textunit_set_tag_group(struct TR_TextUnit * self, list_t * tag_group);
extern void tr_textunit_set_font(struct TR_TextUnit * self, struct TR_Font * font);
extern void tr_textunit_set_extra(struct TR_TextUnit * self, char * key, void * data);

struct TR_TextRegion {
	int x;
	int y;

	int width;
	int height;

	struct TR_Font * font;

	char * text;
	list_t * lines;
	int align;
	int valign;
	int line_height; /* TODO should be property of lines */

	struct TR_TextUnit * text_units; /* array */
	int scroll;
	char * ellipsis; /* blank by default */
	bool one_line; /* False by default */

	char * base_dir; /* Used for links and images */
	bool break_all; /* False by default */
	char * title; /* blank by default */
	int max_lines; /* 0 is None */
};

struct TR_Offset {
	struct TR_TextUnit * unit;
	int line;
	int left;
	int right;
	int index;
};

extern void tr_textregion_set_alignment(struct TR_TextRegion * self, int align);
extern void tr_textregion_set_valignment(struct TR_TextRegion * self, int align);
extern void tr_textregion_set_max_lines(struct TR_TextRegion * self, int max_lines);
extern int tr_textregion_get_visible_lines(struct TR_TextRegion * self); /* height / line_height */
extern void tr_textregion_reflow(struct TR_TextRegion * self);
extern list_t * tr_textregion_units_from_text(struct TR_TextRegion * self, char * text, struct TR_Font * font, bool whitespace);

extern void tr_textregion_set_one_line(struct TR_TextRegion * self, bool one_line);
extern void tr_textregion_set_ellipsis(struct TR_TextRegion * self, char * ellipsis);
extern void tr_textregion_set_text(struct TR_TextRegion* self, char * text);
extern void tr_textregion_set_font(struct TR_TextRegion* self, struct TR_Font * font);
extern void tr_textregion_set_line_height(struct TR_TextRegion * self, int line_height);
extern void tr_textregion_resize(struct TR_TextRegion * self, int width, int height);
extern void tr_textregion_move(struct TR_TextRegion * self, int x, int y);

extern void tr_textregion_get_offset_at_index(struct TR_TextRegion* self, int index, struct TR_Offset * out);
extern void tr_textregion_pick(struct TR_TextRegion * self, int x, int y, struct TR_Offset * out);
extern struct TR_TextUnit * tr_textregion_click(struct TR_TextRegion * self, int x, int y);
extern void tr_textregion_draw(struct TR_TextRegion * self, gfx_context_t * ctx);

_End_C_Header
