/**
 * @brief help-browser - Display documentation.
 *
 * This is a work-in-progress reimplementation of the help browser
 * from mainline ToaruOS. It is currently incomplete.
 *
 * Eventually, this should be a rich text document browser, almost
 * akin to a web browser. Right now it just says "Hello, world."
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018-2020 K. Lange
 */
#include <stdio.h>
#include <unistd.h>
#include <dirent.h>

#include <toaru/yutani.h>
#include <toaru/graphics.h>
#include <toaru/decorations.h>
#include <toaru/menu.h>
#include <toaru/text.h>
#include <toaru/markup.h>

#define APPLICATION_TITLE "Help Browser"
#define HELP_DIR "/usr/share/help"

static yutani_t * yctx;
static yutani_window_t * main_window;
static gfx_context_t * ctx;

static int application_running = 1;

static gfx_context_t * contents = NULL;
static sprite_t * contents_sprite = NULL;
static int contents_width = 0;

static char * current_topic = NULL;
static int scroll_offset;

/* Markup Renderer { */
#define BASE_X 2
#define BASE_Y 2
#define LINE_HEIGHT 20
#define HEAD_HEIGHT 28

static int cursor_y = 0;
static int cursor_x = 0;
static list_t * state = NULL;
static int current_state = 0;

static struct TT_Font * tt_font_thin = NULL;
static struct TT_Font * tt_font_bold = NULL;
static struct TT_Font * tt_font_oblique = NULL;
static struct TT_Font * tt_font_bold_oblique = NULL;
static struct TT_Font * tt_font_mono = NULL;

struct Char {
	char c; /* TODO: unicode */
	char state;
};

//static list_t * lines = NULL;
static list_t * buffer = NULL;

static struct TT_Font * state_to_font(int current_state) {
	if (current_state & (1 << 3)) {
		return tt_font_mono;
	}
	if (current_state & (1 << 0 | 1 << 2) ) {
		if (current_state & (1 << 1)) {
			return tt_font_bold_oblique;
		}
		return tt_font_bold;
	} else if (current_state & (1 << 1)) {
		return tt_font_oblique;
	}
	return tt_font_thin;
}

static int current_size(void) {
	if (current_state & (1 << 2)) {
		return 22;
	}
	return 13;
}

static int buffer_width(list_t * buffer) {
	int out = 0;
	foreach(node, buffer) {
		struct Char * c = node->value;

		char tmp[2] = {c->c, '\0'};

		tt_set_size(state_to_font(c->state), current_size());
		out += tt_string_width(state_to_font(c->state), tmp);
	}
	return out;
}

static int draw_buffer(list_t * buffer) {
	int x = 0;
	while (buffer->length) {
		node_t * node = list_dequeue(buffer);
		struct Char * c = node->value;
		char tmp[2] = { c->c, '\0' };
		tt_set_size(state_to_font(c->state), current_size());
		if (contents) {
			x += tt_draw_string(contents, state_to_font(c->state), cursor_x + x, cursor_y + current_size(), tmp, 0xFF000000);
		} else {
			x += tt_string_width(state_to_font(c->state), tmp);
		}
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
	if (buffer_width(buffer) + cursor_x > contents_width) {
		cursor_x = BASE_X;
		cursor_y += current_line_height();
	}
	cursor_x += draw_buffer(buffer);
}

static int parser_open(struct markup_state * self, void * user, struct markup_tag * tag) {
	if (!strcmp(tag->name, "b")) {
		list_insert(state, (void*)(uintptr_t)current_state);
		current_state |= (1 << 0);
	} else if (!strcmp(tag->name, "i")) {
		list_insert(state, (void*)(uintptr_t)current_state);
		current_state |= (1 << 1);
	} else if (!strcmp(tag->name, "h1")) {
		list_insert(state, (void*)(uintptr_t)current_state);
		current_state |= (1 << 2);
	} else if (!strcmp(tag->name, "mono")) {
		list_insert(state, (void*)(uintptr_t)current_state);
		current_state |= (1 << 3);
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
		current_state = (int)(uintptr_t)nstate->value;
		free(nstate);
	} else if (!strcmp(tag_name, "i")) {
		node_t * nstate = list_pop(state);
		current_state = (int)(uintptr_t)nstate->value;
		free(nstate);
	} else if (!strcmp(tag_name, "mono")) {
		node_t * nstate = list_pop(state);
		current_state = (int)(uintptr_t)nstate->value;
		free(nstate);
	} else if (!strcmp(tag_name, "h1")) {
		write_buffer();
		cursor_x = BASE_X;
		cursor_y += current_line_height();
		node_t * nstate = list_pop(state);
		current_state = (int)(uintptr_t)nstate->value;
		free(nstate);
	}
	return 0;
}

static int parser_data(struct markup_state * self, void * user, char * data) {
	char * c = data;
	while (*c) {
		if (*c == ' ' && !(current_state & (1 << 3))) {
			if (buffer->length) {
				write_buffer();
			}
		} else if (*c == '\n') {
			if (buffer->length) {
				write_buffer();
			}
			if (current_state & (1 << 3)) {
				cursor_x = BASE_X;
				cursor_y += current_line_height();
			}
		} else {
			int chr = *c;
			if (*c == '&') {
				if (c[1] == 'l' && c[2] == 't' && c[3] == ';') {
					c += 3;
					chr = '<';
				} else if (c[1] == 'g' && c[2] == 't' && c[3] == ';') {
					c += 3;
					chr = '>';
				}
			}
			struct Char * ch = malloc(sizeof(struct Char));
			ch->c = chr;
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

	struct decor_bounds bounds;
	decor_get_bounds(main_window, &bounds);
	contents = NULL;
	contents_width = main_window->width - bounds.width;

	{
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

	contents_sprite = create_sprite(contents_width, cursor_y + current_size() + 20, ALPHA_EMBEDDED);
	contents = init_graphics_sprite(contents_sprite);

	draw_fill(contents, rgb(255,255,255));

	{
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
	draw_sprite(ctx, contents_sprite, bounds.left_width, bounds.top_height + MENU_BAR_HEIGHT - scroll_offset);
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

static char * generate_index(void) {
	list_t * components = list_create();

	list_insert(components, strdup("<h1>Topics</h1>\n"));

	/* Open /usr/share/help */
	DIR * dirp = opendir("/usr/share/help");

	if (dirp) {
		for (struct dirent * ent = readdir(dirp); ent; ent = readdir(dirp)) {
			if (ent->d_name[0] == '.') continue;

			/* TODO: Extract the actual heading... */
			char tmp[1024];
			snprintf(tmp, 1024, " » %s<br>\n", ent->d_name);
			list_insert(components, strdup(tmp));
		}
		closedir(dirp);
	}

	size_t totalSize = 0;
	foreach(node, components) {
		char * s = node->value;
		totalSize += strlen(s);
	}

	char * out = malloc(totalSize + 1);
	char * ptr = out;
	foreach(node, components) {
		char * s = node->value;
		size_t len = strlen(s);
		memcpy(ptr, s, len);
		ptr += len;
		free(s);
	}
	*ptr = '\0';

	list_free(components);
	return out;
}

static void navigate(const char * t) {

	if (current_topic) free(current_topic);

	/* Is this a special file? */

	if (strstr(t, "special:") == t) {

		const char * pageName = t + 8;

		if (!strcmp(pageName, "contents")) {
			/* Oh boy... */
			current_topic = generate_index();
		} else {
			current_topic = strdup("File not found.");
		}

	} else {
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
	}

	reinitialize_contents();
	redraw_window();

}

#define SCROLL_AMOUNT 120
static void _scroll_up(void) {
	scroll_offset -= SCROLL_AMOUNT;
	if (scroll_offset < 0) {
		scroll_offset = 0;
	}
}

static void _scroll_down(void) {
	struct decor_bounds bounds;
	decor_get_bounds(main_window, &bounds);
	int available_height = main_window->height - bounds.height - MENU_BAR_HEIGHT;

	if (available_height > contents->height) {
		scroll_offset = 0;
	} else {
		scroll_offset += SCROLL_AMOUNT;
		if (scroll_offset > contents->height - available_height) {
			scroll_offset = contents->height - available_height;
		}
	}
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
	strcat(about_cmd, "about \"About Help Browser\" /usr/share/icons/48/help.png \"ToaruOS Help Browser\" \"© 2018-2020 K. Lange\n-\nPart of ToaruOS, which is free software\nreleased under the NCSA/University of Illinois\nlicense.\n-\n%https://toaruos.org\n%https://github.com/klange/toaruos\" ");
	char coords[100];
	sprintf(coords, "%d %d &", (int)main_window->x + (int)main_window->width / 2, (int)main_window->y + (int)main_window->height / 2);
	strcat(about_cmd, coords);
	system(about_cmd);
	redraw_window();
}

static void redraw_window_callback(struct menu_bar * self) {
	(void)self;
	redraw_window();
}

int main(int argc, char * argv[]) {

	yctx = yutani_init();
	init_decorations();
	main_window = yutani_window_create(yctx, 640, 480);
	yutani_window_move(yctx, main_window, yctx->display_width / 2 - main_window->width / 2, yctx->display_height / 2 - main_window->height / 2);
	ctx = init_graphics_yutani_double_buffer(main_window);

	tt_font_thin         = tt_font_from_shm("sans-serif");
	tt_font_bold         = tt_font_from_shm("sans-serif.bold");
	tt_font_oblique      = tt_font_from_shm("sans-serif.italic");
	tt_font_bold_oblique = tt_font_from_shm("sans-serif.bolditalic");
	tt_font_mono         = tt_font_from_shm("monospace");

	yutani_window_advertise_icon(yctx, main_window, APPLICATION_TITLE, "help");

	menu_bar.entries = menu_entries;
	menu_bar.redraw_callback = redraw_window_callback;

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
						yutani_window_t * win = hashmap_get(yctx->windows, (void*)(uintptr_t)wf->wid);
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
						yutani_window_t * win = hashmap_get(yctx->windows, (void*)(uintptr_t)me->wid);

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

							struct decor_bounds bounds;
							decor_get_bounds(main_window, &bounds);

							if (me->new_x >= 0 && me->new_x <= (int)main_window->width &&
								me->new_y > bounds.top_height + MENU_BAR_HEIGHT &&
								me->new_y < (int)main_window->height) {
								if (me->buttons & YUTANI_MOUSE_SCROLL_UP) {
									/* Scroll up */
									_scroll_up();
									redraw_window();
								} else if (me->buttons & YUTANI_MOUSE_SCROLL_DOWN) {
									_scroll_down();
									redraw_window();
								}
							}
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
