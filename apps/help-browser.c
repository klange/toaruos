/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018-2020 K. Lange
 *
 * help-browser - Display documentation.
 *
 * This is a work-in-progress reimplementation of the help browser
 * from mainline ToaruOS. It is currently incomplete.
 *
 * Eventually, this should be a rich text document browser, almost
 * akin to a web browser. Right now it just says "Hello, world."
 */
#include <stdio.h>
#include <unistd.h>

#include <toaru/yutani.h>
#include <toaru/graphics.h>
#include <toaru/decorations.h>
#include <toaru/menu.h>
#include <toaru/sdf.h>
#include <toaru/markup.h>

#define APPLICATION_TITLE "Help Browser"
#define HELP_DIR "/usr/share/help"

static yutani_t * yctx;
static yutani_window_t * main_window;
static gfx_context_t * ctx;

static int application_running = 1;

static gfx_context_t * contents = NULL;
static sprite_t * contents_sprite = NULL;

static char * current_topic = NULL;

/* Markup Renderer { */
#define BASE_X 0
#define BASE_Y 0
#define LINE_HEIGHT 20
#define HEAD_HEIGHT 28

static gfx_context_t * nctx = NULL;
static int cursor_y = 0;
static int cursor_x = 0;
static list_t * state = NULL;
static int current_state = 0;

struct Char {
	char c; /* TODO: unicode */
	char state;
};

//static list_t * lines = NULL;
static list_t * buffer = NULL;

static int state_to_font(int current_state) {
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

static int current_size(void) {
	if (current_state & (1 << 2)) {
		return 24;
	}
	return 16;
}

static int buffer_width(list_t * buffer) {
	int out = 0;
	foreach(node, buffer) {
		struct Char * c = node->value;

		char tmp[2] = {c->c, '\0'};

		out += draw_sdf_string_width(tmp, current_size(), state_to_font(c->state));
	}
	return out;
}

static int draw_buffer(list_t * buffer) {
	int x = 0;
	while (buffer->length) {
		node_t * node = list_dequeue(buffer);
		struct Char * c = node->value;
		char tmp[2] = { c->c, '\0' };
		x += draw_sdf_string(nctx, cursor_x + x, cursor_y, tmp, current_size(), 0xFF000000, state_to_font(c->state));
		free(c);
		free(node);
	}
	x += 4;
	return x;
}

static int current_line_height(void) {
	if (current_state & (1 << 2)) {
		return HEAD_HEIGHT;
	} else {
		return LINE_HEIGHT;
	}
}

static void write_buffer(void) {
	if (buffer_width(buffer) + cursor_x > nctx->width) {
		cursor_x = BASE_X;
		cursor_y += current_line_height();
	}
	cursor_x += draw_buffer(buffer);
}

static int parser_open(struct markup_state * self, void * user, struct markup_tag * tag) {
	if (!strcmp(tag->name, "b")) {
		list_insert(state, (void*)current_state);
		current_state |= (1 << 0);
	} else if (!strcmp(tag->name, "i")) {
		list_insert(state, (void*)current_state);
		current_state |= (1 << 1);
	} else if (!strcmp(tag->name, "h1")) {
		list_insert(state, (void*)current_state);
		current_state |= (1 << 2);
	} else if (!strcmp(tag->name, "br")) {
		write_buffer();
		cursor_x = BASE_X;
		cursor_y += current_line_height();
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
	} else if (!strcmp(tag_name, "h1")) {
		write_buffer();
		cursor_x = BASE_X;
		cursor_y += current_line_height();
		node_t * nstate = list_pop(state);
		current_state = (int)nstate->value;
		free(nstate);
	}
	return 0;
}

static int parser_data(struct markup_state * self, void * user, char * data) {
	char * c = data;
	while (*c) {
		if (*c == ' ' || *c == '\n') {
			if (buffer->length) {
				write_buffer();
			}
		} else {
			struct Char * ch = malloc(sizeof(struct Char));
			ch->c = *c;
			ch->state = current_state;
			list_insert(buffer, ch);
		}
		c++;
	}
	return 0;
}

/* } End Markup Renderer */

static struct menu_bar menu_bar = {0};
static struct menu_bar_entries menu_entries[] = {
	{"File", "file"},
	{"Go", "go"},
	{"Help", "help"},
	{NULL, NULL},
};

static void _menu_action_exit(struct MenuEntry * entry) {
	application_running = 0;
}

static void reinitialize_contents(void) {
	if (contents) {
		free(contents);
	}

	if (contents_sprite) {
		sprite_free(contents_sprite);
	}

	/* Calculate height for current directory */
	int calculated_height = 200;

	struct decor_bounds bounds;
	decor_get_bounds(main_window, &bounds);

	contents_sprite = create_sprite(main_window->width - bounds.width, calculated_height, ALPHA_EMBEDDED);
	contents = init_graphics_sprite(contents_sprite);

	draw_fill(contents, rgb(255,255,255));

	nctx = contents;
	struct markup_state * parser = markup_init(NULL, parser_open, parser_close, parser_data);
	cursor_y = BASE_Y;
	cursor_x = BASE_X;
	state = list_create();
	buffer = list_create();

	char * str = current_topic;
	while (*str) {
		if (markup_parse(parser, *str++)) {
			fprintf(stderr,"There was an error.\n");
			return;
		}
	}

	markup_finish(parser);
	write_buffer();
	list_free(state);
	free(state);
	free(buffer);
}

static void redraw_window(void) {
	draw_fill(ctx, rgb(255,255,255));

	render_decorations(main_window, ctx, APPLICATION_TITLE);

	struct decor_bounds bounds;
	decor_get_bounds(main_window, &bounds);

	menu_bar.x = bounds.left_width;
	menu_bar.y = bounds.top_height;
	menu_bar.width = ctx->width - bounds.width;
	menu_bar.window = main_window;
	menu_bar_render(&menu_bar, ctx);

	gfx_clear_clip(ctx);
	gfx_add_clip(ctx, bounds.left_width, bounds.top_height + MENU_BAR_HEIGHT, ctx->width - bounds.width, ctx->height - MENU_BAR_HEIGHT - bounds.height);
	draw_sprite(ctx, contents_sprite, bounds.left_width, bounds.top_height + MENU_BAR_HEIGHT);
	gfx_clear_clip(ctx);
	gfx_add_clip(ctx, 0, 0, ctx->width, ctx->height);

	flip(ctx);
	yutani_flip(yctx, main_window);
}

static void resize_finish(int w, int h) {
	int height_changed = (main_window->width != (unsigned int)w);

	yutani_window_resize_accept(yctx, main_window, w, h);
	reinit_graphics_yutani(ctx, main_window);

	if (height_changed) {
		reinitialize_contents();
	}

	redraw_window();
	yutani_window_resize_done(yctx, main_window);

	yutani_flip(yctx, main_window);
}

static void navigate(const char * t) {

	if (current_topic) free(current_topic);

	char file_path[1024];

	if (t[0] == '/') {
		sprintf(file_path, "%s", t);
	} else {
		sprintf(file_path, "%s/%s", HELP_DIR, t);
	}

	FILE * f = fopen(file_path, "r");

	if (!f) {
		current_topic = strdup("File not found.");
	} else {
		fseek(f, 0, SEEK_END);
		size_t size = ftell(f);
		fseek(f, 0, SEEK_SET);

		current_topic = malloc(size+1);
		fread(current_topic, 1, size, f);
		current_topic[size] = '\0';

		fclose(f);
	}

	reinitialize_contents();
	redraw_window();

}

static void _menu_action_navigate(struct MenuEntry * entry) {
	/* go to entry->action */
	struct MenuEntry_Normal * _entry = (void*)entry;
	navigate(_entry->action);
}

#if 0
static void _menu_action_back(struct MenuEntry * entry) {
	/* go back */
}

static void _menu_action_forward(struct MenuEntry * entry) {
	/* go forward */
}
#endif 

static void _menu_action_about(struct MenuEntry * entry) {
	/* Show About dialog */
	char about_cmd[1024] = "\0";
	strcat(about_cmd, "about \"About Help Browser\" /usr/share/icons/48/help.png \"ToaruOS Help Browser\" \"(C) 2018-2020 K. Lange\n-\nPart of ToaruOS, which is free software\nreleased under the NCSA/University of Illinois\nlicense.\n-\n%https://toaruos.org\n%https://github.com/klange/toaruos\" ");
	char coords[100];
	sprintf(coords, "%d %d &", (int)main_window->x + (int)main_window->width / 2, (int)main_window->y + (int)main_window->height / 2);
	strcat(about_cmd, coords);
	system(about_cmd);
	redraw_window();
}

int main(int argc, char * argv[]) {

	yctx = yutani_init();
	init_decorations();
	main_window = yutani_window_create(yctx, 640, 480);
	yutani_window_move(yctx, main_window, yctx->display_width / 2 - main_window->width / 2, yctx->display_height / 2 - main_window->height / 2);
	ctx = init_graphics_yutani_double_buffer(main_window);

	yutani_window_advertise_icon(yctx, main_window, APPLICATION_TITLE, "help");

	menu_bar.entries = menu_entries;
	menu_bar.redraw_callback = redraw_window;

	menu_bar.set = menu_set_create();

	struct MenuList * m = menu_create(); /* File */
	menu_insert(m, menu_create_normal("exit",NULL,"Exit", _menu_action_exit));
	menu_set_insert(menu_bar.set, "file", m);

	m = menu_create(); /* Go */
	menu_insert(m, menu_create_normal("home","0_index.trt","Home",_menu_action_navigate));
	menu_insert(m, menu_create_normal("bookmark","special:contents","Topics",_menu_action_navigate));
#if 0
	/* TODO: History */
	menu_insert(m, menu_create_separator());
	menu_insert(m, menu_create_normal("back",NULL,"Back",_menu_action_back));
	menu_insert(m, menu_create_normal("forward",NULL,"Forward",_menu_action_forward));
#endif
	menu_set_insert(menu_bar.set, "go", m);

	m = menu_create();
	menu_insert(m, menu_create_normal("help","help-browser.trt","Contents",_menu_action_navigate));
	menu_insert(m, menu_create_separator());
	menu_insert(m, menu_create_normal("star",NULL,"About " APPLICATION_TITLE,_menu_action_about));
	menu_set_insert(menu_bar.set, "help", m);

	if (argc > 1) {
		navigate(argv[1]);
	} else {
		navigate("0_index.trt");
	}

	while (application_running) {
		yutani_msg_t * m = yutani_poll(yctx);
		while (m) {
			if (menu_process_event(yctx, m)) {
				redraw_window();
			}
			switch (m->type) {
				case YUTANI_MSG_KEY_EVENT:
					{
						struct yutani_msg_key_event * ke = (void*)m->data;
						if (ke->event.action == KEY_ACTION_DOWN && ke->wid == main_window->wid) {
							switch (ke->event.keycode) {
								case 'f':
									if (ke->event.modifiers & YUTANI_KEY_MODIFIER_ALT) {
										menu_bar_show_menu(yctx,main_window,&menu_bar,-1,&menu_entries[0]);
									}
									break;
								case 'g':
									if (ke->event.modifiers & YUTANI_KEY_MODIFIER_ALT) {
										menu_bar_show_menu(yctx,main_window,&menu_bar,-1,&menu_entries[1]);
									}
									break;
								case 'h':
									if (ke->event.modifiers & YUTANI_KEY_MODIFIER_ALT) {
										menu_bar_show_menu(yctx,main_window,&menu_bar,-1,&menu_entries[2]);
									}
									break;
								case 'q':
									_menu_action_exit(NULL);
									break;
							}
						}
					}
					break;
				case YUTANI_MSG_WINDOW_FOCUS_CHANGE:
					{
						struct yutani_msg_window_focus_change * wf = (void*)m->data;
						yutani_window_t * win = hashmap_get(yctx->windows, (void*)wf->wid);
						if (win == main_window) {
							win->focused = wf->focused;
							redraw_window();
						}
					}
					break;
				case YUTANI_MSG_RESIZE_OFFER:
					{
						struct yutani_msg_window_resize * wr = (void*)m->data;
						if (wr->wid == main_window->wid) {
							resize_finish(wr->width, wr->height);
						}
					}
					break;
				case YUTANI_MSG_WINDOW_MOUSE_EVENT:
					{
						struct yutani_msg_window_mouse_event * me = (void*)m->data;
						yutani_window_t * win = hashmap_get(yctx->windows, (void*)me->wid);

						if (win == main_window) {
							int result = decor_handle_event(yctx, m);
							switch (result) {
								case DECOR_CLOSE:
									_menu_action_exit(NULL);
									break;
								case DECOR_RIGHT:
									/* right click in decoration, show appropriate menu */
									decor_show_default_menu(main_window, main_window->x + me->new_x, main_window->y + me->new_y);
									break;
								default:
									/* Other actions */
									break;
							}

							/* Menu bar */
							menu_bar_mouse_event(yctx, main_window, &menu_bar, me, me->new_x, me->new_y);
						}
					}
					break;
				case YUTANI_MSG_WINDOW_CLOSE:
				case YUTANI_MSG_SESSION_END:
					_menu_action_exit(NULL);
					break;
				default:
					break;
			}
			free(m);
			m = yutani_poll_async(yctx);
		}
	}
}
