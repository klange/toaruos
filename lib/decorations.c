/**
 * @brief Client-side Window Decoration library
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2012-2018 K. Lange
 */
#include <stdint.h>
#include <math.h>
#include <dlfcn.h>

#include <toaru/graphics.h>
#include <toaru/yutani.h>
#include <toaru/decorations.h>
#include <toaru/menu.h>
#include <toaru/text.h>

#define TEXT_OFFSET_X 10
#define TEXT_OFFSET_Y 3

#define BORDERCOLOR rgb(59,59,59)
#define BORDERCOLOR_INACTIVE rgb(30,30,30)
#define TEXTCOLOR rgb(230,230,230)
#define TEXTCOLOR_INACTIVE rgb(140,140,140)

void (*decor_render_decorations)(yutani_window_t *, gfx_context_t *, char *, int) = NULL;
int  (*decor_check_button_press)(yutani_window_t *, int x, int y) = NULL;
int  (*decor_get_bounds)(yutani_window_t *, struct decor_bounds *) = NULL;

static void (*callback_close)(yutani_window_t *) = NULL;
static void (*callback_resize)(yutani_window_t *) = NULL;
static void (*callback_maximize)(yutani_window_t *) = NULL;

static int close_enough(struct yutani_msg_window_mouse_event * me) {
	return (me->command == YUTANI_MOUSE_EVENT_RAISE &&
			sqrt(pow(me->new_x - me->old_x, 2.0) + pow(me->new_y - me->old_y, 2.0)) < 10.0);
}

static struct TT_Font * tt_font = NULL;

static void render_decorations_simple(yutani_window_t * window, gfx_context_t * ctx, char * title, int decors_active) {

	uint32_t color = BORDERCOLOR;
	if (decors_active == DECOR_INACTIVE) {
		color = BORDERCOLOR_INACTIVE;
	}


	for (int i = 0; i < (int)window->height; ++i) {
		GFX(ctx, 0, i) = color;
		GFX(ctx, window->width - 1, i) = color;
	}

	for (int i = 1; i < (int)24; ++i) {
		for (int j = 1; j < (int)window->width - 1; ++j) {
			GFX(ctx, j, i) = color;
		}
	}

	tt_set_size(tt_font, 12);
	uint32_t textcolor = (decors_active == DECOR_INACTIVE) ? TEXTCOLOR_INACTIVE : TEXTCOLOR;
	tt_draw_string(ctx, tt_font, TEXT_OFFSET_X, TEXT_OFFSET_Y + 12, title, textcolor);
	tt_draw_string(ctx, tt_font, window->width - 20, TEXT_OFFSET_Y + 12, "x", textcolor);

	for (uint32_t i = 0; i < window->width; ++i) {
		GFX(ctx, i, 0) = color;
		GFX(ctx, i, 24 - 1) = color;
		GFX(ctx, i, window->height - 1) = color;
	}
}

static int check_button_press_simple(yutani_window_t * window, int x, int y) {
	if (x >= (int)window->width - 20 && x <= (int)window->width - 2 && y >= 2) {
		return DECOR_CLOSE;
	}

	return 0;
}

static int get_bounds_simple(yutani_window_t * window, struct decor_bounds * bounds) {
	/* Does not change with window state */
	bounds->top_height = 24;
	bounds->bottom_height = 1;
	bounds->left_width = 1;
	bounds->right_width = 1;

	bounds->width = bounds->left_width + bounds->right_width;
	bounds->height = bounds->top_height + bounds->bottom_height;

	return 0;
}

static void initialize_simple() {
	decor_render_decorations = render_decorations_simple;
	decor_check_button_press = check_button_press_simple;
	decor_get_bounds         = get_bounds_simple;
	tt_font = tt_font_from_shm("sans-serif");
}

void render_decorations(yutani_window_t * window, gfx_context_t * ctx, char * title) {
	if (!window) return;
	window->decorator_flags |= DECOR_FLAG_DECORATED;
	if (window->focused || !hashmap_is_empty(menu_get_windows_hash())) {
		decor_render_decorations(window, ctx, title, DECOR_ACTIVE);
	} else {
		decor_render_decorations(window, ctx, title, DECOR_INACTIVE);
	}
}

void render_decorations_inactive(yutani_window_t * window, gfx_context_t * ctx, char * title) {
	if (!window) return;
	window->decorator_flags |= DECOR_FLAG_DECORATED;
	decor_render_decorations(window, ctx, title, DECOR_INACTIVE);
}

static void _decor_maximize(yutani_t * yctx, yutani_window_t * window) {
	if (callback_maximize) {
		callback_maximize(window);
	} else {
		yutani_special_request(yctx, window, YUTANI_SPECIAL_REQUEST_MAXIMIZE);
	}
}

static void _decor_minimize(yutani_t * yctx, yutani_window_t * window) {
	yutani_special_request(yctx, window, YUTANI_SPECIAL_REQUEST_MINIMIZE);
}

static yutani_window_t * _decor_menu_owner_window = NULL;
static struct MenuList * _decor_menu = NULL;

static void _decor_start_move(struct MenuEntry * self) {
	if (!_decor_menu_owner_window)
		return;
	yutani_focus_window(_decor_menu_owner_window->ctx, _decor_menu_owner_window->wid);
	yutani_window_drag_start(_decor_menu_owner_window->ctx, _decor_menu_owner_window);
}

static void _decor_start_maximize(struct MenuEntry * self) {
	if (!_decor_menu_owner_window)
		return;
	_decor_maximize(_decor_menu_owner_window->ctx, _decor_menu_owner_window);
	yutani_focus_window(_decor_menu_owner_window->ctx, _decor_menu_owner_window->wid);
}

static void _decor_start_minimize(struct MenuEntry * self) {
	if (!_decor_menu_owner_window)
		return;
	_decor_minimize(_decor_menu_owner_window->ctx, _decor_menu_owner_window);
}

static void _decor_close(struct MenuEntry * self) {
	if (!_decor_menu_owner_window)
		return;

	yutani_special_request(_decor_menu_owner_window->ctx, _decor_menu_owner_window, YUTANI_SPECIAL_REQUEST_PLEASE_CLOSE);
}

yutani_window_t * decor_show_default_menu(yutani_window_t * window, int x, int y) {
	if (_decor_menu->window) return NULL;
	_decor_menu_owner_window = window;
	menu_show_at(_decor_menu, window, x - window->x, y - window->y);
	return _decor_menu->window;
}

void init_decorations() {
	char * tmp = getenv("WM_THEME");
	char * theme = tmp ? strdup(tmp) : NULL;

	_decor_menu = menu_create();
	menu_insert(_decor_menu, menu_create_normal(NULL, NULL, "Maximize", _decor_start_maximize));
	menu_insert(_decor_menu, menu_create_normal(NULL, NULL, "Minimize", _decor_start_minimize));
	menu_insert(_decor_menu, menu_create_normal(NULL, NULL, "Move", _decor_start_move));
	menu_insert(_decor_menu, menu_create_separator());
	menu_insert(_decor_menu, menu_create_normal(NULL, NULL, "Close", _decor_close));

	if (!theme || !strcmp(theme, "simple")) {
		initialize_simple();
	} else {
		char * options = strchr(theme,',');
		if (options) {
			*options = '\0';
			options++;
		}
		char lib_name[100];
		sprintf(lib_name, "libtoaru_decor-%s.so", theme);
		void * theme_lib = dlopen(lib_name, 0);
		if (!theme_lib) {
			goto _theme_error;
		}
		void (*theme_init)(char *) = dlsym(theme_lib, "decor_init");
		if (!theme_init) {
			goto _theme_error;
		}
		theme_init(options);
		return;

_theme_error:
			fprintf(stderr, "decorations: could not load theme `%s`: %s\n", theme, dlerror());
			initialize_simple();
	}
}

void decor_set_close_callback(void (*callback)(yutani_window_t *)) {
	callback_close = callback;
}

void decor_set_resize_callback(void (*callback)(yutani_window_t *)) {
	callback_resize = callback;
}

void decor_set_maximize_callback(void (*callback)(yutani_window_t *)) {
	callback_maximize = callback;
}

static int within_decors(yutani_window_t * window, int x, int y) {
	struct decor_bounds bounds;
	decor_get_bounds(window, &bounds);

	if ((x <= (int)bounds.left_width || x >= (int)window->width - (int)bounds.right_width) && (x > 0 && x < (int)window->width)) return 1;
	if ((y <= (int)bounds.top_height || y >= (int)window->height - (int)bounds.bottom_height) && (y > 0 && y < (int)window->height)) return 1;
	return 0;
}

#define LEFT_SIDE (me->new_x <= (int)bounds.left_width)
#define RIGHT_SIDE (me->new_x >= (int)window->width - (int)bounds.right_width)
#define TOP_SIDE (me->new_y <= (int)bounds.top_height)
#define BOTTOM_SIDE (me->new_y >= (int)window->height - (int)bounds.bottom_height)

static yutani_scale_direction_t check_resize_direction(struct yutani_msg_window_mouse_event * me, yutani_window_t * window) {
	struct decor_bounds bounds;
	decor_get_bounds(window, &bounds);
	yutani_scale_direction_t resize_direction = SCALE_NONE;

	if (LEFT_SIDE && !TOP_SIDE && !BOTTOM_SIDE) {
		resize_direction = SCALE_LEFT;
	} else if (RIGHT_SIDE && !TOP_SIDE && !BOTTOM_SIDE) {
		resize_direction = SCALE_RIGHT;
	} else if (BOTTOM_SIDE && !LEFT_SIDE && !RIGHT_SIDE) {
		resize_direction = SCALE_DOWN;
	} else if (BOTTOM_SIDE && LEFT_SIDE) {
		resize_direction = SCALE_DOWN_LEFT;
	} else if (BOTTOM_SIDE && RIGHT_SIDE) {
		resize_direction = SCALE_DOWN_RIGHT;
	} else if (TOP_SIDE && LEFT_SIDE) {
		resize_direction = SCALE_UP_LEFT;
	} else if (TOP_SIDE && RIGHT_SIDE) {
		resize_direction = SCALE_UP_RIGHT;
	} else if (TOP_SIDE && (me->new_y < 5)) {
		resize_direction = SCALE_UP;
	}
	return resize_direction;
}

static yutani_scale_direction_t old_resize_direction = SCALE_NONE;
int decor_hover_button = 0;
yutani_window_t * decor_hover_window = NULL;

int decor_handle_event(yutani_t * yctx, yutani_msg_t * m) {
	if (m) {
		switch (m->type) {
			case YUTANI_MSG_WINDOW_MOUSE_EVENT:
				{
					struct yutani_msg_window_mouse_event * me = (void*)m->data;
					yutani_window_t * window = hashmap_get(yctx->windows, (void*)(uintptr_t)me->wid);
					struct decor_bounds bounds;
					decor_get_bounds(window, &bounds);
					if (!window) return 0;
					if (!(window->decorator_flags & DECOR_FLAG_DECORATED)) return 0;
					if (me->command == YUTANI_MOUSE_EVENT_LEAVE && decor_hover_window == window) {
						decor_hover_window = NULL;
						decor_hover_button = 0;
						yutani_internal_refocus(yctx, window);
						return DECOR_REDRAW;
					}
					if (within_decors(window, me->new_x, me->new_y)) {
						int button = decor_check_button_press(window, me->new_x, me->new_y);
						if (me->command == YUTANI_MOUSE_EVENT_DOWN && me->buttons & YUTANI_MOUSE_BUTTON_LEFT) {
							if (!button || button == DECOR_OTHER) {
								/* Resize edges */
								yutani_scale_direction_t resize_direction = check_resize_direction(me, window);

								if (resize_direction != SCALE_NONE) {
									yutani_window_resize_start(yctx, window, resize_direction);
								}

								if (me->new_y < (int)bounds.top_height && resize_direction == SCALE_NONE) {
									yutani_window_drag_start(yctx, window);
								}
								return DECOR_OTHER;
							}
						}
						if (!button && (me->buttons & YUTANI_MOUSE_BUTTON_RIGHT)) {
							return DECOR_RIGHT;
						}
						if (me->command == YUTANI_MOUSE_EVENT_MOVE) {
							if (!button) {
								/* Resize edges */
								yutani_scale_direction_t resize_direction = check_resize_direction(me, window);
								if (resize_direction != old_resize_direction) {
									if (resize_direction == SCALE_NONE) {
										yutani_window_show_mouse(yctx, window, YUTANI_CURSOR_TYPE_RESET);
									} else {
										switch (resize_direction) {
											case SCALE_UP:
											case SCALE_DOWN:
												yutani_window_show_mouse(yctx, window, YUTANI_CURSOR_TYPE_RESIZE_VERTICAL);
												break;
											case SCALE_LEFT:
											case SCALE_RIGHT:
												yutani_window_show_mouse(yctx, window, YUTANI_CURSOR_TYPE_RESIZE_HORIZONTAL);
												break;
											case SCALE_DOWN_RIGHT:
											case SCALE_UP_LEFT:
												yutani_window_show_mouse(yctx, window, YUTANI_CURSOR_TYPE_RESIZE_UP_DOWN);
												break;
											case SCALE_DOWN_LEFT:
											case SCALE_UP_RIGHT:
												yutani_window_show_mouse(yctx, window, YUTANI_CURSOR_TYPE_RESIZE_DOWN_UP);
												break;
											case SCALE_AUTO:
											case SCALE_NONE:
												break;
										}
									}
									old_resize_direction = resize_direction;
								}
							} else if (old_resize_direction != SCALE_NONE) {
								yutani_window_show_mouse(yctx, window, YUTANI_CURSOR_TYPE_RESET);
								old_resize_direction = SCALE_NONE;
							}
						}
						if (me->command == YUTANI_MOUSE_EVENT_CLICK || close_enough(me)) {
							/* Determine if we clicked on a button */
							switch (button) {
								case DECOR_CLOSE:
									if (callback_close) callback_close(window);
									break;
								case DECOR_RESIZE:
									if (callback_resize) callback_resize(window);
									break;
								case DECOR_MAXIMIZE:
									_decor_maximize(yctx, window);
									break;
								case DECOR_MINIMIZE:
									_decor_minimize(yctx, window);
									break;
								default:
									break;
							}
							decor_hover_window = NULL;
							decor_hover_button = 0;
							yutani_internal_refocus(yctx, window);
							return button;
						}
						if (button != decor_hover_button || window != decor_hover_window) {
							decor_hover_button = button;
							decor_hover_window = window;
							yutani_internal_refocus(yctx, window);
							return DECOR_REDRAW;
						}
					} else {
						if (old_resize_direction != SCALE_NONE) {
							yutani_window_show_mouse(yctx, window, YUTANI_CURSOR_TYPE_RESET);
							old_resize_direction = SCALE_NONE;
						}
						if (decor_hover_window == window) {
							decor_hover_button = 0;
							decor_hover_window = NULL;
							yutani_internal_refocus(yctx, window);
							return DECOR_REDRAW;
						}
					}
				}
				break;
		}
	}
	return 0;
}
