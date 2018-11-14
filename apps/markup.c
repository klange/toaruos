/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 *
 * marked up text demo
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <getopt.h>

#include <toaru/yutani.h>
#include <toaru/graphics.h>
#include <toaru/decorations.h>
#include <toaru/sdf.h>
#include <toaru/markup.h>

/* Pointer to graphics memory */
static yutani_t * yctx;
static yutani_window_t * window = NULL;
static gfx_context_t * ctx = NULL;

static int width = 500;
static int height = 500;

static int left = 200;
static int top = 200;

static int size = 16;

static void decors() {
	render_decorations(window, ctx, "Markup Demo");
}

static int cursor_x = 0;
static list_t * state = NULL;
static int current_state = 0;

static int parser_open(struct markup_state * self, void * user, struct markup_tag * tag) {
	if (!strcmp(tag->name, "b")) {
		list_insert(state, (void*)current_state);
		current_state |= (1 << 0);
	} else if (!strcmp(tag->name, "i")) {
		list_insert(state, (void*)current_state);
		current_state |= (1 << 1);
	}
	markup_free_tag(tag);
	return 0;
}

static int parser_close(struct markup_state * self, void * user, char * tag_name) {
	if (!strcmp(tag_name, "b")) {
		node_t * nstate = list_pop(state);
		current_state = (int)nstate->value;
		free(nstate);
	} else if (!strcmp(tag_name, "i")) {
		node_t * nstate = list_pop(state);
		current_state = (int)nstate->value;
		free(nstate);
	}
	return 0;
}

static int state_to_font(void) {
	if (current_state & (1 << 0)) {
		if (current_state & (1 << 1)) {
			return SDF_FONT_BOLD_OBLIQUE;
		}
		return SDF_FONT_BOLD;
	} else if (current_state & (1 << 1)) {
		return SDF_FONT_OBLIQUE;
	}
	return SDF_FONT_THIN;
}

static int parser_data(struct markup_state * self, void * user, char * data) {
	cursor_x += draw_sdf_string(ctx, cursor_x, 30, data, size, rgb(0,0,0), state_to_font());
	return 0;
}


void redraw() {
	draw_fill(ctx, rgb(255,255,255));

	decors();

	struct markup_state * parser = markup_init(NULL, parser_open, parser_close, parser_data);

	char * str = "<b>This <i foo=bar baz=qux>is</i> a test</b> with <i><data fun=123>data</data> at <b>the</b> end</i>";
	cursor_x = 20;
	state = list_create();

	while (*str) {
		//fprintf(stderr, "Parser state in: %d  Character: %c\n", parser->state, *str);
		if (markup_parse(parser, *str++)) {
			fprintf(stderr, "bailing\n");
			return;
		}
	}
	markup_finish(parser);
	list_free(state);
	free(state);

}

void resize_finish(int w, int h) {
	yutani_window_resize_accept(yctx, window, w, h);
	reinit_graphics_yutani(ctx, window);

	struct decor_bounds bounds;
	decor_get_bounds(window, &bounds);

	width  = w - bounds.left_width - bounds.right_width;
	height = h - bounds.top_height - bounds.bottom_height;

	redraw();

	yutani_window_resize_done(yctx, window);
	yutani_flip(yctx, window);
}


int main(int argc, char * argv[]) {

	yctx = yutani_init();
	if (!yctx) {
		fprintf(stderr, "%s: failed to connect to compositor\n", argv[0]);
		return 1;
	}
	init_decorations();

	struct decor_bounds bounds;
	decor_get_bounds(NULL, &bounds);

	window = yutani_window_create(yctx, width + bounds.width, height + bounds.height);
	yutani_window_move(yctx, window, left, top);

	yutani_window_advertise_icon(yctx, window, "SDF Demo", "sdf");

	ctx = init_graphics_yutani(window);

	redraw();
	yutani_flip(yctx, window);

	int playing = 1;
	while (playing) {
		yutani_msg_t * m = yutani_poll(yctx);
		if (m) {
			switch (m->type) {
				case YUTANI_MSG_KEY_EVENT:
					{
						struct yutani_msg_key_event * ke = (void*)m->data;
						if (ke->event.action == KEY_ACTION_DOWN && ke->event.keycode == 'q') {
							playing = 0;
						} else if (ke->event.action == KEY_ACTION_DOWN) {
							if (size <= 20) {
								size += 1;
							} else if (size > 20) {
								size += 5;
							}
							if (size > 100) {
								size = 1;
							}
							redraw();
							yutani_flip(yctx,window);
						}
					}
					break;
				case YUTANI_MSG_WINDOW_FOCUS_CHANGE:
					{
						struct yutani_msg_window_focus_change * wf = (void*)m->data;
						yutani_window_t * win = hashmap_get(yctx->windows, (void*)wf->wid);
						if (win) {
							win->focused = wf->focused;
							decors();
							yutani_flip(yctx, window);
						}
					}
					break;
				case YUTANI_MSG_RESIZE_OFFER:
					{
						struct yutani_msg_window_resize * wr = (void*)m->data;
						resize_finish(wr->width, wr->height);
					}
					break;
				case YUTANI_MSG_WINDOW_MOUSE_EVENT:
					{
						int result = decor_handle_event(yctx, m);
						switch (result) {
							case DECOR_CLOSE:
								playing = 0;
								break;
							default:
								/* Other actions */
								break;
						}
					}
					break;
				case YUTANI_MSG_SESSION_END:
					playing = 0;
					break;
				default:
					break;
			}
		}
		free(m);
	}

	yutani_close(yctx, window);

	return 0;
}

