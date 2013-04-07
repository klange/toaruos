/*
 * draw
 *
 * Windowed graphical drawing tool.
 * Simple painting application.
 *
 * This is also the playground for the work-in-progress
 * ToaruToolKit GUI toolkit.
 */
#include <stdlib.h>

#include "gui/ttk/ttk.h"
#include "lib/list.h"

/* XXX TOOLKIT FUNCTIONS */

gfx_context_t * ctx;
window_t * wina;

/* Active TTK window XXX */
static window_t * ttk_window = NULL;

/* TTK Window's objects XXX */
static list_t *   ttk_objects = NULL;

#define TTK_BUTTON_TYPE      0x00000001
#define TTK_RAW_SURFACE_TYPE 0x00000002

#define TTK_BUTTON_STATE_NORMAL  0
#define TTK_BUTTON_STATE_DOWN    1

/*
 * Core TTK GUI object
 */
typedef struct {
	uint32_t type; /* Object type indicator (for introspection) */
	int32_t  x;    /* Coordinates */
	int32_t  y;
	int32_t  width;  /* Sizes */
	int32_t  height;
	void (*render_func)(void *, cairo_t * cr); /* (Internal) function to render the object */
	void (*click_callback)(void *, w_mouse_t *); /* Callback function for clicking */
} ttk_object;

/* TTK Button */
typedef struct {
	ttk_object _super;   /* Parent type (Object -> Button) */
	char * title;        /* Button text */
	uint32_t fill_color; /* Fill color */
	uint32_t fore_color; /* Text color */
	int button_state;
} ttk_button;

typedef struct {
	ttk_object _super;
	gfx_context_t * surface;
} ttk_raw_surface;

void ttk_render_button(void * s, cairo_t * cr) {
	ttk_object * self = (ttk_object *)s;

	if (((ttk_button *)self)->button_state == TTK_BUTTON_STATE_DOWN) {
		_ttk_draw_button_select(cr, self->x, self->y, self->width, self->height, ((ttk_button *)self)->title);
	} else {
		_ttk_draw_button(cr, self->x, self->y, self->width, self->height, ((ttk_button *)self)->title);
	}
#if 0
	/* Fill the button */
	for (uint16_t y = self->y + 1; y < self->y + self->height; y++) {
		draw_line(ctx, self->x, self->x + self->width, y, y, ((ttk_button *)self)->fill_color);
	}
	/* Then draw the border */
	uint32_t border_color = rgb(0,0,0);
	draw_line(ctx, self->x, self->x + self->width, self->y, self->y, border_color);
	draw_line(ctx, self->x, self->x,  self->y, self->y + self->height, border_color);
	draw_line(ctx, self->x + self->width, self->x + self->width, self->y, self->y + self->height, border_color);
	draw_line(ctx, self->x, self->x + self->width, self->y + self->height, self->y + self->height, border_color);
	/* button-specific stuff */
	uint32_t w = draw_string_width(((ttk_button * )self)->title);
	uint16_t offset = (self->width - w) / 2;
	draw_string(ctx, self->x + offset, self->y + self->height - 3, ((ttk_button *)self)->fore_color, ((ttk_button * )self)->title);
#endif
}

void ttk_render_raw_surface(void * s, cairo_t * cr) {
	ttk_object * self = (ttk_object *)s;

	gfx_context_t * surface = ((ttk_raw_surface *)self)->surface;

	{
		cairo_save(cr);
		int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, surface->width);
		cairo_surface_t * internal_surface = cairo_image_surface_create_for_data(surface->backbuffer, CAIRO_FORMAT_ARGB32, surface->width, surface->height, stride);

		cairo_set_source_surface(cr, internal_surface, self->x, self->y);
		cairo_paint(cr);

		cairo_surface_destroy(internal_surface);
		cairo_restore(cr);
	}
}

ttk_button * ttk_button_new(char * title, void (*callback)(void *, w_mouse_t *)) {
	ttk_button * out = malloc(sizeof(ttk_button));
	out->title = title;
	out->fill_color   = rgb(100,100,100);
	out->button_state = TTK_BUTTON_STATE_NORMAL;

	/* Standard */
	ttk_object * obj = (ttk_object *)out;
	obj->click_callback = callback;;
	obj->render_func = ttk_render_button;
	obj->type = TTK_BUTTON_TYPE;
	obj->x = 0;
	obj->y = 0;
	obj->width  = 20;
	obj->height = 20;

	list_insert(ttk_objects, obj);
	return out;
}

ttk_raw_surface * ttk_raw_surface_new(int width, int height) {
	ttk_raw_surface * out = malloc(sizeof(ttk_raw_surface));

	ttk_object * obj = (ttk_object *)out;

	out->surface = malloc(sizeof(gfx_context_t));
	out->surface->width  = width;
	out->surface->height = height;
	out->surface->depth  = 32;
	out->surface->buffer = malloc(sizeof(uint32_t) * width * height);
	out->surface->backbuffer = out->surface->buffer;

	draw_fill(out->surface, rgb(255,255,255));

	obj->width = width;
	obj->height = height;
	obj->x = 10;
	obj->y = 10;

	obj->click_callback = NULL;
	obj->type = TTK_RAW_SURFACE_TYPE;

	obj->render_func = ttk_render_raw_surface;

	list_insert(ttk_objects, obj);
	return out;
}

/*
 * Reposition a TTK object
 */
void ttk_position(ttk_object * obj, int x, int y, int width, int height) {
	obj->x = x;
	obj->y = y;
	obj->width = width;
	obj->height = height;
}

int ttk_within(ttk_object * obj, w_mouse_t * evt) {
	if (evt->new_x >= obj->x && evt->new_x < obj->x + obj->width &&
		evt->new_y >= obj->y && evt->new_y < obj->y + obj->height) {
		return 1;
	}
	return 0;
}

void ttk_check_click(w_mouse_t * evt) {
	if (evt->command == WE_MOUSECLICK) {
		foreach(node, ttk_objects) {
			ttk_object * obj = (ttk_object *)node->value;
			if (ttk_within(obj, evt)) {
				if (obj->click_callback) {
					obj->click_callback(obj, evt);
				}
			}
		}
	}
}

void ttk_render() {
	/* XXX */
	ttk_window_t _window;
	ttk_window_t * window = &_window;
	window->core_context = ctx;
	window->core_window  = wina;
	window->width        = ctx->width; // - decor_width();
	window->height       = ctx->height; //- decor_height();
	window->off_x        = 0; //decor_left_width;
	window->off_y        = 0; //decor_top_height;
	window->title        = "Draw!";

	draw_fill(ctx, rgb(TTK_BACKGROUND_DEFAULT));
	ttk_redraw_borders(window);


	{
		int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, window->core_window->width);
		cairo_surface_t * core_surface = cairo_image_surface_create_for_data(window->core_context->backbuffer, CAIRO_FORMAT_ARGB32, window->core_window->width, window->core_window->height, stride);
		cairo_t * cr_main = cairo_create(core_surface);

		/* TODO move this surface to a ttk_frame_t or something; GUIs man, go look at some Qt or GTK APIs! */
		cairo_surface_t * internal_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, window->width, window->height);
		cairo_t * cr = cairo_create(internal_surface);

		foreach(node, ttk_objects) {
			ttk_object * obj = (ttk_object *)node->value;
			if (obj->render_func) {
				obj->render_func(obj, cr);
			}
		}

		/* Paint the window's internal surface onto the backbuffer */
		cairo_set_source_surface(cr_main, internal_surface, (double)window->off_x, (double)window->off_y);
		cairo_paint(cr_main);
		cairo_surface_flush(internal_surface);
		cairo_destroy(cr);
		cairo_surface_destroy(internal_surface);

		/* In theory, we don't actually want to destroy much of any of this; maybe the cairo_t */
		cairo_surface_flush(core_surface);
		cairo_destroy(cr_main);
		cairo_surface_destroy(core_surface);
	}

	flip(window->core_context);
}

void setup_ttk(window_t * window) {
	ttk_window = window;
	ttk_objects = list_create();
	init_shmemfonts();
}

uint32_t drawing_color = 0;
uint16_t quit = 0;

ttk_button * button_red;
ttk_button * button_green;
ttk_button * button_blue;

ttk_button * button_thick;
ttk_button * button_thin;
ttk_raw_surface * drawing_surface;
int thick = 0;

static void set_color(void * button, w_mouse_t * event) {
	ttk_button * self = (ttk_button *)button;

	if (button_blue  != self) button_blue->button_state  = TTK_BUTTON_STATE_NORMAL;
	if (button_red   != self) button_red->button_state   = TTK_BUTTON_STATE_NORMAL;
	if (button_green != self) button_green->button_state = TTK_BUTTON_STATE_NORMAL;

	self->button_state = TTK_BUTTON_STATE_DOWN;
	drawing_color = self->fill_color;

	ttk_render();
}

static void quit_app(void * button, w_mouse_t * event) {
	quit = 1;
}

static void set_thickness_thick(void * button, w_mouse_t * event) {
#if 0
	button_thick->fill_color = rgb(127,127,127);
	button_thick->fore_color = rgb(255,255,255);
	button_thin->fill_color = rgb(40,40,40);
	button_thin->fore_color = rgb(255,255,255);
#endif
	button_thick->button_state = TTK_BUTTON_STATE_DOWN;
	button_thin->button_state = TTK_BUTTON_STATE_NORMAL;
	thick = 1;
	ttk_render();
}

static void set_thickness_thin(void * button, w_mouse_t * event) {
#if 0
	button_thin->fill_color = rgb(127,127,127);
	button_thin->fore_color = rgb(255,255,255);
	button_thick->fill_color = rgb(40,40,40);
	button_thick->fore_color = rgb(255,255,255);
#endif
	button_thin->button_state = TTK_BUTTON_STATE_DOWN;
	button_thick->button_state = TTK_BUTTON_STATE_NORMAL;
	thick = 0;
	ttk_render();
}

void resize_callback(window_t * window) {
	reinit_graphics_window(ctx, wina);
	ttk_render();
}

void focus_callback(window_t * window) {
	ttk_render();
}

void keep_drawing(w_mouse_t * mouse) { 
	double thickness = thick ? 2.0 : 0.5;;

	int old_x = mouse->old_x - ((ttk_object *)drawing_surface)->x;
	int old_y = mouse->old_y - ((ttk_object *)drawing_surface)->y;

	int new_x = mouse->new_x - ((ttk_object *)drawing_surface)->x;
	int new_y = mouse->new_y - ((ttk_object *)drawing_surface)->y;

	{
		int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, drawing_surface->surface->width);
		cairo_surface_t * internal_surface = cairo_image_surface_create_for_data(drawing_surface->surface->backbuffer, CAIRO_FORMAT_ARGB32, drawing_surface->surface->width, drawing_surface->surface->height, stride);
		cairo_t * cr = cairo_create(internal_surface);

		cairo_set_source_rgb(cr, _RED(drawing_color) / 255.0, _GRE(drawing_color) / 255.0, _BLU(drawing_color) / 255.0);
		cairo_set_line_width(cr, thickness);

		cairo_move_to(cr, old_x, old_y);
		cairo_line_to(cr, new_x, new_y);

		cairo_stroke(cr);

		cairo_destroy(cr);
		cairo_surface_destroy(internal_surface);
	}

}

int main (int argc, char ** argv) {
	int left = 30;
	int top  = 30;

	int width  = 450;
	int height = 450;

	setup_windowing();

	resize_window_callback = resize_callback;
	focus_changed_callback = focus_callback;

	/* Do something with a window */
	wina = window_create(left, top, width, height);
	ctx = init_graphics_window_double_buffer(wina);
	draw_fill(ctx, rgb(255,255,255));
	init_decorations();

	win_sane_events();

	setup_ttk(wina);
	button_blue = ttk_button_new("Blue", set_color);
	ttk_position((ttk_object *)button_blue, decor_left_width + 3, decor_top_height + 3, 100, 20);
	button_blue->fill_color = rgb(0,0,255);
	button_blue->fore_color = rgb(255,255,255);

	button_green = ttk_button_new("Green", set_color);
	ttk_position((ttk_object *)button_green, decor_left_width + 106, decor_top_height + 3, 100, 20);
	button_green->fill_color = rgb(0,255,0);
	button_green->fore_color = rgb(0,0,0);

	button_red = ttk_button_new("Red", set_color);
	ttk_position((ttk_object *)button_red, decor_left_width + 209, decor_top_height + 3, 100, 20);
	button_red->fill_color = rgb(255,0,0);
	button_red->fore_color = rgb(255,255,255);

	button_thick = ttk_button_new("Thick", set_thickness_thick);
	ttk_position((ttk_object *)button_thick, decor_left_width + 312, decor_top_height + 3, 50, 20);
	button_thick->fill_color = rgb(40,40,40);
	button_thick->fore_color = rgb(255,255,255);

	button_thin = ttk_button_new("Thin", set_thickness_thin);
	ttk_position((ttk_object *)button_thin, decor_left_width + 362, decor_top_height + 3, 50, 20);
	button_thin->fill_color = rgb(127,127,127);
	button_thin->fore_color = rgb(255,255,255);

	ttk_button * button_quit = ttk_button_new("X", quit_app);
	ttk_position((ttk_object *)button_quit, width - 33, 12, 20, 20);
	button_quit->fill_color = rgb(255,0,0);
	button_quit->fore_color = rgb(255,255,255);

	drawing_surface = ttk_raw_surface_new(width - 30, height - 70);
	((ttk_object *)drawing_surface)->y = 60;

	drawing_color = rgb(255,0,0);

	ttk_render();

	while (!quit) {
		wins_packet_t * event = get_window_events();
		window_t * window = NULL;

		switch (event->command_type & WE_GROUP_MASK) {
			case WE_WINDOW_EVT: {
				w_window_t * evt = (w_window_t *)((uintptr_t)event + sizeof(wins_packet_t));
				switch (event->command_type) {
					case WE_FOCUSCHG:
						window = wins_get_window(evt->wid);
						if (window) {
							window->focused = evt->left;
							focus_changed_callback(window);
						}
						break;
					case WE_RESIZED:
						window = wins_get_window(evt->wid);
						if (window) {
							resize_window_buffer_client(window, evt->left, evt->top, evt->width, evt->height);
							resize_window_callback(window);
						}
						break;
				}
				break;
			}
			case WE_MOUSE_EVT: {
				w_mouse_t * mouse = (w_mouse_t *)((uintptr_t)event + sizeof(wins_packet_t));
				if (event->command_type == WE_MOUSEMOVE && mouse->buttons & MOUSE_BUTTON_LEFT) {
					keep_drawing(mouse);
					ttk_render();
				} else {
					ttk_check_click(mouse);
				}
				break;
			}
			case WE_KEY_EVT: {
				w_keyboard_t * key = (w_keyboard_t *)((uintptr_t)event + sizeof(wins_packet_t));
				printf("key event, key=%c\n", key->key);
				switch (key->key) {
					case 'q':
						quit = 1;
						break;
				}
				break;
			}
			default: {
				printf("Incoming window event; command = 0x%x\n", event->command_type);
				break;
			}
		}

		free(event);
	}

	teardown_windowing();

	return 0;
}
