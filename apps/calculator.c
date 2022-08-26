/**
 * @file apps/calculator.c
 * @brief Four-function calculator app.
 *
 * This calculator app is intended to be a more straightforward playground
 * for building out a widget toolkit. The calculator presents buttons in
 * a grid layout alongside a text input box and a menubar.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021 K. Lange
 */
#include <toaru/yutani.h>
#include <toaru/graphics.h>
#include <toaru/decorations.h>
#include <toaru/menu.h>
#include <toaru/button.h>
#include <toaru/text.h>
#include <toaru/markup_text.h>
#include <kuroko/kuroko.h>
#include <kuroko/vm.h>
#include <kuroko/util.h>

static struct menu_bar menu_bar = {0};
static struct menu_bar_entries menu_entries[] = {
	{"File", "file"},
	{"Help", "help"},
	{NULL, NULL},
};

static yutani_t * yctx;
static yutani_window_t * window = NULL;
static gfx_context_t * ctx = NULL;

static int32_t width = 600;
static int32_t height = 240;

static char * title_str = "Calculator";

static int textInputIsAccumulatorValue = 0;
static char accumulator[1024] = {0};
static char textInput[1024] = {0};

struct CalculatorButton {
	struct TTKButton ttkButton;
	char * label;
	void (*onClick)(struct CalculatorButton *);
};

static void clear_result(void) {
	if (textInputIsAccumulatorValue) {
		textInputIsAccumulatorValue = 0;
		*textInput = '\0';
		*accumulator = '\0';
	}
}

static void calc_numeric(char * text) {
	clear_result();
	strcat(textInput, text);
}

static void calc_func(char * txt) {
	clear_result();
	strcat(accumulator, textInput);
	strcat(accumulator, txt);
	*textInput = '\0';
	textInputIsAccumulatorValue = 0;
}

static void calc_backspace(void) {
	if (textInputIsAccumulatorValue) {
		clear_result();
	} else if (!*textInput) {
		size_t l = strlen(accumulator);
		if (l) {
			accumulator[l-1] = '\0';
		}
	} else {
		size_t l = strlen(textInput);
		if (l) {
			textInput[l-1] = '\0';
		}
	}
}

static void btn_numeric(struct CalculatorButton * self) { calc_numeric(self->label); }
static void btn_func_div(struct CalculatorButton * self) { calc_func("/"); }
static void btn_func_mul(struct CalculatorButton * self) { calc_func("*"); }
static void btn_func_sub(struct CalculatorButton * self) { calc_func("-"); }
static void btn_func_add(struct CalculatorButton * self) { calc_func("+"); }
static void btn_func_pct(struct CalculatorButton * self) { calc_func("%"); }
static void btn_func_opr(struct CalculatorButton * self) { calc_func("("); }
static void btn_func_cpr(struct CalculatorButton * self) { calc_func(")"); }
static void btn_func_clr(struct CalculatorButton * self) {
	if (!*textInput) {
		*accumulator = '\0';
	} else {
		*textInput = '\0';
	}
}
static void btn_func_equ(struct CalculatorButton * self) {
	if (textInputIsAccumulatorValue) return;
	if (*textInput) {
		strcat(accumulator, textInput);
		*textInput = '\0';
	}

	KrkValue result = krk_interpret(accumulator, "<stdin>");
	if (!IS_NONE(result)) {
		krk_attachNamedValue(&vm.builtins->fields, "_", result);
		krk_push(result);
		krk_push(krk_stringFromFormat("%R", result));
		krk_swap(1);
		krk_pop();
		if (IS_STRING(krk_peek(0))) {
			snprintf(textInput, 1024, "%s", AS_CSTRING(krk_peek(0)));
		}
		krk_pop();
	} else if (krk_currentThread.flags & KRK_THREAD_HAS_EXCEPTION) {
		strcat(textInput, "Error.");
	} else {
		strcat(textInput, "*");
	}
	krk_resetStack();

	textInputIsAccumulatorValue = 1;
}

#define N(n) {{0},#n,btn_numeric}
#define F(n,func) {{0},n,btn_func_ ## func}

#define BTN_ROWS 4
#define BTN_COLS 5

struct CalculatorButton buttons[] = {
	N(7), N(8), N(9),         F("÷",div), F("(",opr),
	N(4), N(5), N(6),         F("×",mul), F(")",cpr),
	N(1), N(2), N(3),         F("-",sub), F("C",clr),
	N(0), N(.), F("mod",pct), F("+",add), F("=",equ),
};

static void redraw(void) {
	struct decor_bounds bounds;
	decor_get_bounds(window, &bounds);
	draw_fill(ctx, rgb(204,204,204));

	draw_rectangle_solid(ctx, bounds.left_width, bounds.top_height + MENU_BAR_HEIGHT + 4, window->width - bounds.width, 42, rgb(255,255,255));

	struct MarkupState * renderer = markup_setup_renderer(ctx, bounds.left_width + 5, bounds.top_height + MENU_BAR_HEIGHT + 14, rgb(0,0,0), 0);
	markup_set_base_font_size(renderer, 10);
	markup_set_base_state(renderer, MARKUP_TEXT_STATE_MONO);
	markup_push_raw_string(renderer, accumulator);
	if (!textInputIsAccumulatorValue && !textInput[0]) markup_push_raw_string(renderer, "_");
	markup_finish_renderer(renderer);

	renderer = markup_setup_renderer(ctx, bounds.left_width + 5, bounds.top_height + MENU_BAR_HEIGHT + 35, rgb(0,0,0), 0);
	markup_set_base_font_size(renderer, 16);
	markup_set_base_state(renderer, (textInputIsAccumulatorValue ? MARKUP_TEXT_STATE_BOLD : 0) | MARKUP_TEXT_STATE_MONO);
	markup_push_raw_string(renderer, textInput);
	if (!textInputIsAccumulatorValue && textInput[0]) markup_push_raw_string(renderer, "_");
	markup_finish_renderer(renderer);

	for (int i = 0; i < (BTN_ROWS * BTN_COLS); ++i) {
		ttk_button_draw(ctx, &buttons[i].ttkButton);
	}

	menu_bar_render(&menu_bar, ctx);

	render_decorations(window, ctx, title_str);
	flip(ctx);
	yutani_flip(yctx, window);
}

static void redraw_window_callback(struct menu_bar * self) {
	(void)self;
	redraw();
}

int in_button(struct TTKButton * button, struct yutani_msg_window_mouse_event * me) {
	if (me->new_y >= button->y && me->new_y < button->y  + button->height) {
		if (me->new_x >= button->x && me->new_x < button->x + button->width) {
			return 1;
		}
	}
	return 0;
}

#define BASE_TOP 50

void setup_buttons(void) {
	struct decor_bounds bounds;
	decor_get_bounds(window, &bounds);

	menu_bar.x = bounds.left_width;
	menu_bar.y = bounds.top_height;
	menu_bar.width = ctx->width - bounds.width;
	menu_bar.window = window;

	size_t ind = 0;

	int aWidth     = ctx->width - bounds.width - 10;
	int baseWidth  = aWidth / BTN_COLS;
	int extraWidth = aWidth - baseWidth * BTN_COLS;

	int aHeight     = ctx->height - bounds.height - 10 - MENU_BAR_HEIGHT - BASE_TOP;
	int baseHeight  = aHeight / BTN_ROWS;
	int extraHeight = aHeight - baseHeight * BTN_ROWS;

	for (int row = 0; row < BTN_ROWS; ++row) {
		for (int col = 0; col < BTN_COLS; ++col, ++ind) {
			buttons[ind].ttkButton.title  = buttons[ind].label;
			buttons[ind].ttkButton.width  = ((col + 1 < BTN_COLS) ? baseWidth : (baseWidth + extraWidth)) - 5;
			buttons[ind].ttkButton.height = ((row + 1 < BTN_ROWS) ? baseHeight : (baseHeight + extraHeight)) - 5;
			buttons[ind].ttkButton.x = 5 + bounds.left_width + baseWidth * col;
			buttons[ind].ttkButton.y = MENU_BAR_HEIGHT + BASE_TOP + 5 + bounds.top_height + baseHeight * row;
		}
	}

}

void resize_finish(int w, int h) {
	if (w < 300 || h < 240) {
		yutani_window_resize_offer(yctx, window, w < 300 ? 300 : w, h < 240 ? 240 : h);
		return;
	}
	yutani_window_resize_accept(yctx, window, w, h);
	reinit_graphics_yutani(ctx, window);
	width  = w;
	height = h;
	setup_buttons();
	redraw();
	yutani_window_resize_done(yctx, window);
}

static void clear_highlights(int *changed) {
	for (int i = 0; i < BTN_ROWS * BTN_COLS; ++i) {
		if (buttons[i].ttkButton.hilight) {
			*changed = 1;
			buttons[i].ttkButton.hilight = 0;
		}
	}
}

void set_hilight(struct TTKButton * button, int hilight) {
	int changed = 0;
	if (!button) {
		clear_highlights(&changed);
	} else if (button && (button->hilight != hilight)) {
		changed = 1;
		clear_highlights(&changed);
		button->hilight = hilight;
	}
	if (changed) redraw();
}

static void update_buttons(struct yutani_msg_window_mouse_event * me, int hilight) {
	struct TTKButton * inButton = NULL;
	for (int i = 0; i < BTN_ROWS * BTN_COLS; ++i) {
		if (in_button(&buttons[i].ttkButton, me)) {
			inButton = &buttons[i].ttkButton;
			break;
		}
	}
	set_hilight(inButton, inButton ? hilight : 0);
}

static void _menu_action_exit(struct MenuEntry * entry) {
	exit(0);
}

static void _menu_action_help(struct MenuEntry * entry) {
	system("help-browser calculator.trt &");
	redraw();
}

static void _menu_action_about(struct MenuEntry * entry) {
	/* Show About dialog */
	char about_cmd[1024] = "\0";
	strcat(about_cmd, "about \"About Calculator\" /usr/share/icons/48/calculator.png \"Calculator\" \"© 2021 K. Lange\n-\nPart of ToaruOS, which is free software\nreleased under the NCSA/University of Illinois\nlicense.\n-\n%https://toaruos.org\n%https://github.com/klange/toaruos\" ");
	char coords[100];
	sprintf(coords, "%d %d &", (int)window->x + (int)window->width / 2, (int)window->y + (int)window->height / 2);
	strcat(about_cmd, coords);
	system(about_cmd);
	redraw();
}

int main(int argc, char * argv[]) {
	int req_center_x, req_center_y;
	yctx = yutani_init();
	if (!yctx) {
		fprintf(stderr, "%s: failed to connect to compositor\n", argv[0]);
		return 1;
	}
	init_decorations();
	markup_text_init();

	struct decor_bounds bounds;
	decor_get_bounds(NULL, &bounds);

	window = yutani_window_create(yctx, width + bounds.width, height + bounds.height);
	req_center_x = yctx->display_width / 2;
	req_center_y = yctx->display_height / 2;

	yutani_window_move(yctx, window, req_center_x - window->width / 2, req_center_y - window->height / 2);

	yutani_window_advertise_icon(yctx, window, title_str, "calculator");

	ctx = init_graphics_yutani_double_buffer(window);

	menu_bar.entries = menu_entries;
	menu_bar.redraw_callback = redraw_window_callback;
	menu_bar.set = menu_set_create();

	struct MenuList * m = menu_create(); /* File */
	menu_insert(m, menu_create_normal("exit",NULL,"Exit", _menu_action_exit));
	menu_set_insert(menu_bar.set, "file", m);

	m = menu_create();
	menu_insert(m, menu_create_normal("help",NULL,"Contents",_menu_action_help));
	menu_insert(m, menu_create_separator());
	menu_insert(m, menu_create_normal("star",NULL,"About Calculator",_menu_action_about));
	menu_set_insert(menu_bar.set, "help", m);

	setup_buttons();
	redraw();

	struct TTKButton * _down_button = NULL;

	vm.binpath = strdup("/bin/calculator"); /* Just assume this so we can get module imports */
	krk_initVM(KRK_GLOBAL_CLEAN_OUTPUT);
	krk_startModule("__main__");

	int playing = 1;
	while (playing) {
		yutani_msg_t * m = yutani_poll(yctx);
		while (m) {
			if (menu_process_event(yctx, m)) {
				redraw();
			}
			switch (m->type) {
				case YUTANI_MSG_KEY_EVENT:
					{
						struct yutani_msg_key_event * ke = (void*)m->data;
						yutani_window_t * win = hashmap_get(yctx->windows, (void*)(uintptr_t)ke->wid);
						if (win == window) {
							if (ke->event.action == KEY_ACTION_DOWN) {
								if (ke->event.key == '\n') btn_func_equ(NULL);
								else if ((ke->event.key >= '0' && ke->event.key <= '9') || ke->event.key == '.') {
									char tmp[2] = {ke->event.key, '\0'};
									calc_numeric(tmp);
								} else if ((ke->event.key == KEY_BACKSPACE)) {
									calc_backspace();
								} else if ((ke->event.key)) {
									char tmp[2] = {ke->event.key, '\0'};
									calc_func(tmp);
								}
								redraw();
							}
						}
					}
					break;
				case YUTANI_MSG_WINDOW_FOCUS_CHANGE:
					{
						struct yutani_msg_window_focus_change * wf = (void*)m->data;
						yutani_window_t * win = hashmap_get(yctx->windows, (void*)(uintptr_t)wf->wid);
						if (win) {
							win->focused = wf->focused;
							redraw();
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
						struct yutani_msg_window_mouse_event * me = (void*)m->data;
						if (me->wid == window->wid) {
							int result = decor_handle_event(yctx, m);
							switch (result) {
								case DECOR_CLOSE:
									playing = 0;
									break;
								case DECOR_RIGHT:
									/* right click in decoration, show appropriate menu */
									decor_show_default_menu(window, window->x + me->new_x, window->y + me->new_y);
									break;
								default:
									/* Other actions */
									break;
							}

							menu_bar_mouse_event(yctx, window, &menu_bar, me, me->new_x, me->new_y);

							struct decor_bounds bounds;
							decor_get_bounds(window, &bounds);
							if (me->new_y > bounds.top_height) {

								if (me->command == YUTANI_MOUSE_EVENT_DOWN) {
									for (int i = 0; i < BTN_ROWS * BTN_COLS; ++i) {
										if (in_button(&buttons[i].ttkButton, me)) {
											set_hilight(&buttons[i].ttkButton, 2);
											_down_button = &buttons[i].ttkButton;
										}
									}
								} else if (me->command == YUTANI_MOUSE_EVENT_RAISE || me->command == YUTANI_MOUSE_EVENT_CLICK) {
									if (_down_button) {
										if (in_button(_down_button, me)) {
											((struct CalculatorButton*)_down_button)->onClick((struct CalculatorButton*)_down_button);
											_down_button->hilight = 0;
										}
									}
									_down_button = NULL;
								}

								if (!me->buttons & YUTANI_MOUSE_BUTTON_LEFT) {
									update_buttons(me, 1);
								} else if (_down_button) {
									if (in_button(_down_button, me)) {
										set_hilight(_down_button, 2);
									} else {
										set_hilight(NULL, 0);
									}
								}
							}
						}
					}
					break;
				case YUTANI_MSG_WINDOW_CLOSE:
				case YUTANI_MSG_SESSION_END:
					playing = 0;
					break;
				default:
					break;
			}
			free(m);
			m = yutani_poll_async(yctx);
		}
	}

	yutani_close(yctx, window);
	return 0;
}

