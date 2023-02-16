/**
 * @brief Marked up text label renderer.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021 K. Lange
 */
#include <math.h>
#include <toaru/markup.h>
#include <toaru/list.h>
#include <toaru/graphics.h>
#include <toaru/text.h>
#include <toaru/decodeutf8.h>
#include "toaru/markup_text.h"

static struct TT_Font * dejaVuSans = NULL;
static struct TT_Font * dejaVuSans_Bold = NULL;
static struct TT_Font * dejaVuSans_Oblique = NULL;
static struct TT_Font * dejaVuSans_BoldOblique = NULL;
static struct TT_Font * dejaVuSansMono = NULL;
static struct TT_Font * dejaVuSansMono_Bold = NULL;
static struct TT_Font * dejaVuSansMono_Oblique = NULL;
static struct TT_Font * dejaVuSansMono_BoldOblique = NULL;

struct MarkupState {
	struct markup_state * parser;
	list_t * state;
	int current_state;
	int cursor_x;
	int cursor_y;
	int initial_left;
	uint32_t color;
	gfx_context_t * ctx;
	int max_cursor_x;
	list_t * colors;
	int sizes[3];
	int dryrun;
};

static void push_state(struct MarkupState * state, int val) {
	list_insert(state->state, (void*)(uintptr_t)state->current_state);
	state->current_state |= val;
}

static void pop_state(struct MarkupState * state) {
	node_t * nstate = list_pop(state->state);
	state->current_state = (int)(uintptr_t)nstate->value;
	free(nstate);
}

static uint32_t parseColor(const char * c) {
	if (*c != '#' || strlen(c) != 7) return rgba(0,0,0,255);

	char r[3] = {c[1],c[2],'\0'};
	char g[3] = {c[3],c[4],'\0'};
	char b[3] = {c[5],c[6],'\0'};

	return rgba(strtoul(r,NULL,16),strtoul(g,NULL,16),strtoul(b,NULL,16),255);
}

static int parser_open(struct markup_state * self, void * user, struct markup_tag * tag) {
	struct MarkupState * state = (struct MarkupState*)user;
	if (!strcmp(tag->name, "b")) {
		push_state(state, MARKUP_TEXT_STATE_BOLD);
	} else if (!strcmp(tag->name, "i")) {
		push_state(state, MARKUP_TEXT_STATE_OBLIQUE);
	} else if (!strcmp(tag->name, "h1")) {
		push_state(state, MARKUP_TEXT_STATE_HEADING);
	} else if (!strcmp(tag->name, "small")) {
		push_state(state, MARKUP_TEXT_STATE_SMALL);
	} else if (!strcmp(tag->name, "mono")) {
		push_state(state, MARKUP_TEXT_STATE_MONO);
	} else if (!strcmp(tag->name, "br")) {
		state->cursor_x = state->initial_left;
		state->cursor_y += 20; /* state->line_height? */
	} else if (!strcmp(tag->name, "color")) {
		/* get options */
		list_t * args = hashmap_keys(tag->options);
		if (args->length == 1) {
			list_insert(state->colors, (void*)(uintptr_t)state->color);
			state->color = parseColor((char*)args->head->value);
		}
		free(args);
	}
	markup_free_tag(tag);
	return 0;
}

static int parser_close(struct markup_state * self, void * user, char * tag_name) {
	struct MarkupState * state = (struct MarkupState*)user;
	if (!strcmp(tag_name, "b")) {
		pop_state(state);
	} else if (!strcmp(tag_name, "i")) {
		pop_state(state);
	} else if (!strcmp(tag_name, "h1")) {
		pop_state(state);
	} else if (!strcmp(tag_name, "small")) {
		pop_state(state);
	} else if (!strcmp(tag_name, "mono")) {
		pop_state(state);
	} else if (!strcmp(tag_name, "color")) {
		node_t * ncolor = list_pop(state->colors);
		state->color = (uint32_t)(uintptr_t)ncolor->value;
		free(ncolor);
	}
	return 0;
}

static struct TT_Font * fontForState(struct MarkupState * state) {
	int bold = !!(state->current_state & MARKUP_TEXT_STATE_BOLD);
	int obli = !!(state->current_state & MARKUP_TEXT_STATE_OBLIQUE);
	int mono = !!(state->current_state & MARKUP_TEXT_STATE_MONO);
	if (mono) {
		if (bold && obli) return dejaVuSansMono_BoldOblique;
		if (bold) return dejaVuSansMono_Bold;
		if (obli) return dejaVuSansMono_Oblique;
		return dejaVuSansMono;
	} else {
		if (bold && obli) return dejaVuSans_BoldOblique;
		if (bold) return dejaVuSans_Bold;
		if (obli) return dejaVuSans_Oblique;
		return dejaVuSans;
	}
}

static int sizeForState(struct MarkupState * state) {
	if (state->current_state & MARKUP_TEXT_STATE_HEADING) return state->sizes[2];
	if (state->current_state & MARKUP_TEXT_STATE_SMALL) return state->sizes[1];
	return state->sizes[0];
}

struct GlyphCacheEntry {
	struct TT_Font * font;
	sprite_t * sprites[3];
	int xs[3];
	uint32_t size;
	uint32_t glyph;
	uint32_t color;
	int y;
};

static struct GlyphCacheEntry glyph_cache[1024];

static void draw_cached_glyph(gfx_context_t * ctx, struct TT_Font * _font, uint32_t size, int x, int y, uint32_t glyph, uint32_t fg, float xadj) {
	unsigned int hash = (((uintptr_t)_font >> 8) ^ (glyph * size)) & 1023;

	struct GlyphCacheEntry * entry = &glyph_cache[hash];

	if (entry->font != _font || entry->size != size || entry->glyph != glyph) {
		if (entry->sprites[0]) sprite_free(entry->sprites[0]);
		if (entry->sprites[1]) sprite_free(entry->sprites[1]);
		if (entry->sprites[2]) sprite_free(entry->sprites[2]);
		tt_set_size(_font, size);

		entry->font = _font;
		entry->size = size;
		entry->glyph = glyph;
		entry->color = _ALP(fg) == 255 ? fg : rgb(0,0,0);
		entry->sprites[0] = tt_bake_glyph(entry->font, entry->glyph, entry->color, &entry->xs[0], &entry->y, 0.0);
		entry->sprites[1] = tt_bake_glyph(entry->font, entry->glyph, entry->color, &entry->xs[1], &entry->y, 0.333);
		entry->sprites[2] = tt_bake_glyph(entry->font, entry->glyph, entry->color, &entry->xs[2], &entry->y, 0.666);
	}

	if (entry->sprites[0]) {
		int sprite = xadj < 0.166 ? 0 : xadj < 0.5 ? 1 : 2;
		if (entry->color != fg) {
			draw_sprite_alpha_paint(ctx, entry->sprites[sprite], x + entry->xs[sprite], y + entry->y, 1.0, fg);
		} else {
			draw_sprite(ctx, entry->sprites[sprite], x + entry->xs[sprite], y + entry->y);
		}
	}
}

static int string_draw_internal(gfx_context_t * ctx, struct TT_Font * font, int font_size, int x, int y, char * data, uint32_t color) {
	float x_offset = x;
	uint32_t cp = 0;
	uint32_t istate = 0;

	for (const unsigned char * c = (const unsigned char*)data; *c; ++c) {
		if (!decode(&istate, &cp, *c)) {
			unsigned int glyph = tt_glyph_for_codepoint(font, cp);
			draw_cached_glyph(ctx, font, font_size, (int)floor(x_offset), y, glyph, color, x_offset-floor(x_offset));
			x_offset += tt_glyph_width(font, glyph);
		}
	}

	return x_offset - x;
}

static int parser_data(struct markup_state * self, void * user, char * data) {
	struct MarkupState * state = (struct MarkupState*)user;
	struct TT_Font * font = fontForState(state);
	int size = sizeForState(state);
	tt_set_size(font, size);
	state->cursor_x += string_draw_internal(state->ctx, font, size, state->cursor_x, state->cursor_y, data, state->color);
	if (state->cursor_x > state->max_cursor_x) state->max_cursor_x = state->cursor_x;
	return 0;
}

static int parser_dryrun(struct markup_state * self, void * user, char * data) {
	struct MarkupState * state = (struct MarkupState*)user;
	struct TT_Font * font = fontForState(state);
	tt_set_size(font, sizeForState(state));
	state->cursor_x += tt_string_width(font, data);
	if (state->cursor_x > state->max_cursor_x) state->max_cursor_x = state->cursor_x;
	return 0;
}

struct MarkupState * markup_setup_renderer(gfx_context_t * ctx, int x, int y, uint32_t color, int dryrun) {
	struct MarkupState * state = malloc(sizeof(struct MarkupState));
	state->parser = markup_init(state, parser_open, parser_close, dryrun ? parser_dryrun : parser_data);
	state->state = list_create();
	state->current_state = 0;
	state->cursor_x = x;
	state->cursor_y = y;
	state->initial_left = x;
	state->color = color;
	state->ctx = ctx;
	state->max_cursor_x = x;
	state->colors = list_create();
	state->sizes[0] = 13;
	state->sizes[1] = 10;
	state->sizes[2] = 18;
	state->dryrun = dryrun;
	return state;
}

void markup_set_base_font_size(struct MarkupState * state, int size) {
	state->sizes[0] = size;
	state->sizes[1] = 10 * size / 13;
	state->sizes[2] = 18 * size / 13;
}

void markup_set_base_state(struct MarkupState * state, int mode) {
	state->current_state = mode;
}

int markup_push_string(struct MarkupState * state, const char * str) {
	while (*str) {
		if (markup_parse(state->parser, *str++)) {
			break;
		}
	}
	return state->max_cursor_x - state->initial_left;
}

int markup_push_raw_string(struct MarkupState * state, const char * str) {
	if (state->dryrun) {
		return parser_dryrun(state->parser, state, (char*)str);
	} else {
		return parser_data(state->parser, state, (char*)str);
	}
}

int markup_finish_renderer(struct MarkupState * state) {
	markup_finish(state->parser);
	list_free(state->state);
	list_free(state->colors);
	free(state->state);
	free(state->colors);
	int total = state->max_cursor_x - state->initial_left;
	free(state);
	return total;
}

int markup_string_width(const char * str) {
	struct MarkupState * state = markup_setup_renderer(NULL,0,0,0,1);
	while (*str) {
		if (markup_parse(state->parser, *str++)) {
			break;
		}
	}
	return markup_finish_renderer(state);
}

int markup_string_height(const char * str) {
	struct MarkupState * state = markup_setup_renderer(NULL,0,0,0,1);
	while (*str) {
		if (markup_parse(state->parser, *str++)) {
			break;
		}
	}
	int out = state->cursor_y;
	markup_finish_renderer(state);
	return out;
}

int markup_draw_string(gfx_context_t * ctx, int x, int y, const char * str, uint32_t color) {
	struct MarkupState * state = markup_setup_renderer(ctx,x,y,color,0);
	while (*str) {
		if (markup_parse(state->parser, *str++)) {
			break;
		}
	}
	return markup_finish_renderer(state);
}

void markup_text_init(void) {
	if (!dejaVuSans) {
		dejaVuSans             = tt_font_from_shm("sans-serif");
		dejaVuSans_Bold        = tt_font_from_shm("sans-serif.bold");
		dejaVuSans_Oblique     = tt_font_from_shm("sans-serif.italic");
		dejaVuSans_BoldOblique = tt_font_from_shm("sans-serif.bolditalic");
		dejaVuSansMono             = tt_font_from_shm("monospace");
		dejaVuSansMono_Bold        = tt_font_from_shm("monospace.bold");
		dejaVuSansMono_Oblique     = tt_font_from_shm("monospace.italic");
		dejaVuSansMono_BoldOblique = tt_font_from_shm("monospace.bolditalic");
	}
}

