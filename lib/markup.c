/**
 * @brief HTML-ish markup parser.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 */
#include <stdio.h>
#include <toaru/markup.h>

struct markup_state {
	int state;
	void * user;
	markup_callback_tag_open  callback_tag_open;
	markup_callback_tag_close callback_tag_close;
	markup_callback_data  callback_data;

	/* Private stuff */
	struct markup_tag tag;
	size_t len;
	char data[64];
	char * attr;
};

struct markup_state * markup_init(void * user, markup_callback_tag_open open, markup_callback_tag_close close, markup_callback_data data) {
	struct markup_state * out = malloc(sizeof(struct markup_state));

	out->state = 0;
	out->user = user;
	out->len = 0;

	out->callback_tag_open  = open;
	out->callback_tag_close = close;
	out->callback_data  = data;

	return out;
}

static void _dump_buffer(struct markup_state * state) {
	if (state->len) {
		state->data[state->len] = '\0';
		state->callback_data(state, state->user, state->data);
		state->data[0] = '\0';
		state->len = 0;
	}
}

static void _finish_name(struct markup_state * state) {
	state->data[state->len] = '\0';
	state->tag.name = strdup(state->data);
	state->tag.options = hashmap_create(5);
	state->data[0] = '\0';
	state->len = 0;
	state->state = 2;
}

static void _finish_close(struct markup_state * state) {
	state->data[state->len] = '\0';
	state->callback_tag_close(state, state->user, state->data);
	state->data[0] = '\0';
	state->len = 0;
	state->state = 0;
}

static void _finish_tag(struct markup_state * state) {
	state->callback_tag_open(state, state->user, &state->tag);
	state->state = 0;
}

static void _finish_bare_attr(struct markup_state * state) {
	state->data[state->len] = '\0';
	hashmap_set(state->tag.options, state->data, strdup(state->data));
	state->data[0] = '\0';
	state->len = 0;
}

static void _finish_attr(struct markup_state * state) {
	state->data[state->len] = '\0';
	state->attr = strdup(state->data);
	state->data[0] = '\0';
	state->len = 0;
	state->state = 4;
}

static void _finish_attr_value(struct markup_state * state) {
	state->data[state->len] = '\0';
	hashmap_set(state->tag.options, state->attr, strdup(state->data));
	free(state->attr);
	state->data[0] = '\0';
	state->len = 0;
	state->state = 2;
}

int markup_free_tag(struct markup_tag * tag) {
	free(tag->name);
	list_t * keys = hashmap_keys(tag->options);
	if (keys->length) {
		foreach(node, keys) {
			free(hashmap_get(tag->options, node->value));
		}
	}
	list_free(keys);
	free(keys);
	hashmap_free(tag->options);
	free(tag->options);
	return 0;
}

int markup_parse(struct markup_state * state, char c) {
	switch (state->state) {
		case 0: /* STATE_NORMAL */
			if (state->len == 63) {
				_dump_buffer(state);
			}
			switch (c) {
				case '<':
					_dump_buffer(state);
					state->state = 1;
					return 0;
				default:
					state->data[state->len] = c;
					state->len++;
					return 0;
			}
			break;
		case 1: /* STATE_TAG_OPEN */
			switch (c) {
				case '/':
					if (state->len) {
						fprintf(stderr, "syntax error\n");
						return 1;
					}
					state->state = 3; /* STATE_TAG_CLOSE */
					return 0;
				case '>':
					_finish_name(state);
					_finish_tag(state);
					return 0;
				case ' ':
					_finish_name(state);
					return 0;
				default:
					state->data[state->len] = c;
					state->len++;
					return 0;
			}
			break;
		case 2: /* STATE_TAG_ATTRIB */
			switch (c) {
				case ' ': /* attribute has no value, end it and append it with = self */
					_finish_bare_attr(state);
					return 0;
				case '>':
					_finish_bare_attr(state);
					_finish_tag(state);
					return 0;
				case '=': /* attribute has a value, go to next mode */
					_finish_attr(state);
					return 0;
				default:
					state->data[state->len] = c;
					state->len++;
					return 0;
			}
			return 0;
		case 3: /* STATE_TAG_CLOSE */
			switch (c) {
				case '>':
					_finish_close(state);
					return 0;
				default:
					state->data[state->len] = c;
					state->len++;
					return 0;
			}
			break;
		case 4: /* STATE_ATTR_VALUE */
			switch (c) {
				case ' ':
					_finish_attr_value(state);
					return 0;
				case '>':
					_finish_attr_value(state);
					_finish_tag(state);
					return 0;
				default:
					state->data[state->len] = c;
					state->len++;
					return 0;
			}
			break;
		default:
			fprintf(stderr, "parser in unknown state\n");
			return 1;
	}
	return 0;
}

int markup_finish(struct markup_state * state) {
	if (state->state != 0) {
		fprintf(stderr, "unexpected end of data\n");
		return 1;
	} else {
		_dump_buffer(state);
		free(state);
		return 0;
	}
}

