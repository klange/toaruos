/**
 * @brief Marked up text label renderer.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021 K. Lange
 */
#include <toaru/markup.h>
#include <toaru/list.h>
#include <toaru/graphics.h>
#include <toaru/text.h>

static struct TT_Font * dejaVuSans = NULL;
static struct TT_Font * dejaVuSans_Bold = NULL;
static struct TT_Font * dejaVuSans_Oblique = NULL;
static struct TT_Font * dejaVuSans_BoldOblique = NULL;

struct MarkupState {
	list_t * state;
	int current_state;
	int cursor_x;
	int cursor_y;
	int initial_left;
	uint32_t color;
	gfx_context_t * ctx;
	int max_cursor_x;
	list_t * colors;
};

#define STATE_BOLD     (1 << 0)
#define STATE_OBLIQUE  (1 << 1)
#define STATE_HEADING  (1 << 2)
#define STATE_SMALL    (1 << 3)

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
		push_state(state, STATE_BOLD);
	} else if (!strcmp(tag->name, "i")) {
		push_state(state, STATE_OBLIQUE);
	} else if (!strcmp(tag->name, "h1")) {
		push_state(state, STATE_HEADING);
	} else if (!strcmp(tag->name, "small")) {
		push_state(state, STATE_SMALL);
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
	} else if (!strcmp(tag_name, "color")) {
		node_t * ncolor = list_pop(state->colors);
		state->color = (uint32_t)(uintptr_t)ncolor->value;
		free(ncolor);
	}
	return 0;
}

static struct TT_Font * fontForState(struct MarkupState * state) {
	if (state->current_state & (1 << 0)) {
		if (state->current_state & (1 << 1)) {
			return dejaVuSans_BoldOblique;
		}
		return dejaVuSans_Bold;
	} else if (state->current_state & (1 << 1)) {
		return dejaVuSans_Oblique;
	}
	return dejaVuSans;
}

static int sizeForState(struct MarkupState * state) {
	if (state->current_state & STATE_HEADING) return 18;
	if (state->current_state & STATE_SMALL) return 10;
	return 13;
}

static int parser_data(struct markup_state * self, void * user, char * data) {
	struct MarkupState * state = (struct MarkupState*)user;
	struct TT_Font * font = fontForState(state);
	tt_set_size(font, sizeForState(state));
	state->cursor_x += tt_draw_string(state->ctx, font, state->cursor_x, state->cursor_y, data, state->color);
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

int markup_string_width(const char * str) {
	struct MarkupState state = {list_create(), 0, 0, 0, 0, 0, NULL, 0, list_create()};
	struct markup_state * parser = markup_init(&state, parser_open, parser_close, parser_dryrun);
	while (*str) {
		if (markup_parse(parser, *str++)) {
			break;
		}
	}
	markup_finish(parser);
	list_free(state.state);
	list_free(state.colors);
	return state.max_cursor_x - state.initial_left;
}

int markup_string_height(const char * str) {
	struct MarkupState state = {list_create(), 0, 0, 0, 0, 0, NULL, 0, list_create()};
	struct markup_state * parser = markup_init(&state, parser_open, parser_close, parser_dryrun);
	while (*str) {
		if (markup_parse(parser, *str++)) {
			break;
		}
	}
	markup_finish(parser);
	list_free(state.state);
	list_free(state.colors);
	return state.cursor_y;
}

int markup_draw_string(gfx_context_t * ctx, int x, int y, const char * str, uint32_t color) {
	struct MarkupState state = {list_create(), 0, x, y, x, color, ctx, x, list_create()};
	struct markup_state * parser = markup_init(&state, parser_open, parser_close, parser_data);
	while (*str) {
		if (markup_parse(parser, *str++)) {
			break;
		}
	}
	markup_finish(parser);
	list_free(state.state);
	list_free(state.colors);
	return state.max_cursor_x - state.initial_left;
}

void markup_text_init(void) {
	dejaVuSans             = tt_font_from_shm("sans-serif");
	dejaVuSans_Bold        = tt_font_from_shm("sans-serif.bold");
	dejaVuSans_Oblique     = tt_font_from_shm("sans-serif.italic");
	dejaVuSans_BoldOblique = tt_font_from_shm("sans-serif.bolditalic");
}

