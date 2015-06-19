#ifndef _TTK_H
#define _TTK_H

#include <stdlib.h>
#include <assert.h>
#include <math.h>
#include <cairo.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_CACHE_H

#include "lib/yutani.h"
#include "lib/graphics.h"
#include "lib/decorations.h"
#include "lib/shmemfonts.h"
#include "lib/hashmap.h"

typedef struct ttk_window {
	yutani_window_t        * core_window;
	gfx_context_t   * core_context;
	char            * title;
	cairo_surface_t * cairo_surface;
	uint16_t          width; /* internal space */
	uint16_t          height;
	uint16_t          off_x; /* decor_left_width */
	uint16_t          off_y; /* decor_top_height */

	int32_t           x;
	int32_t           y;
} ttk_window_t;

#define TTK_BACKGROUND_DEFAULT 204,204,204
#define TTK_DEFAULT_X 300
#define TTK_DEFAULT_Y 300

void cairo_rounded_rectangle(cairo_t * cr, double x, double y, double width, double height, double radius);
void ttk_redraw_borders(ttk_window_t * window);
void _ttk_draw_button(cairo_t * cr, int x, int y, int width, int height, char * title);
void _ttk_draw_button_hover(cairo_t * cr, int x, int y, int width, int height, char * title);
void _ttk_draw_button_select(cairo_t * cr, int x, int y, int width, int height, char * title);
void _ttk_draw_button_disabled(cairo_t * cr, int x, int y, int width, int height, char * title);
void _ttk_draw_menu(cairo_t * cr, int x, int y, int width);
void ttk_window_draw(ttk_window_t * window);
void ttk_initialize();
ttk_window_t * ttk_window_new(char * title, uint16_t width, uint16_t height);
void ttk_quit();
int ttk_run(ttk_window_t * window);


#endif
