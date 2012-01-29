/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Window Compositor
 */

#include <stdio.h>
#include <stdlib.h>
#include <syscall.h>
#include <stdint.h>
#include "lib/graphics.h"
#include "lib/list.h"

typedef struct {
	uint32_t wid; /* Window identifier */
	uint32_t owner; /* Owning process */

	uint16_t width;  /* Buffer width in pixels */
	uint16_t height; /* Buffer height in pixels */

	int32_t  x; /* X coordinate of upper-left corner */
	int32_t  y; /* Y coordinate of upper-left corner */
	uint16_t z; /* Stack order */

	void * buffer; /* Window buffer */
} window_t;

#define TO_WINDOW_OFFSET(x,y) (((x) - window->x) + ((y) - window->y) * window->width)
#define DIRECT_OFFSET(x,y) ((x) + (y) * window->width)

list_t * window_list;

int32_t min(int32_t a, int32_t b) {
	return (a < b) ? a : b;
}

int32_t max(int32_t a, int32_t b) {
	return (a > b) ? a : b;
}

uint8_t is_between(int32_t lo, int32_t hi, int32_t val) {
	if (val >= lo && val < hi) return 1;
	return 0;
}

uint8_t is_top(window_t *window, uint16_t x, uint16_t y) {
	uint16_t index = window->z;
	foreach(node, window_list) {
		window_t * win = (window_t *)node->value;
		if (win == window)  continue;
		if (win->z < index) continue;
		if (is_between(win->x, win->x + win->width, x) && is_between(win->y, win->y + win->height, y)) {
			return 0;
		}
	}
	return 1;
}

void redraw_window(window_t *window) {
	uint16_t _lo_x = max(window->x, 0);
	uint16_t _hi_x = min(window->x + window->width, graphics_width);
	uint16_t _lo_y = max(window->y, 0);
	uint16_t _hi_y = min(window->y + window->height, graphics_height);

	for (uint16_t y = _lo_y; y < _hi_y; ++y) {
		for (uint16_t x = _lo_x; x < _hi_x; ++x) {
			if (is_top(window, x, y)) {
				GFX(x,y) = ((uint32_t *)window->buffer)[TO_WINDOW_OFFSET(x,y)];
			}
		}
	}
}

void init_window(window_t *window, int32_t x, int32_t y, uint16_t width, uint16_t height, uint16_t index) {
	window->width  = width;
	window->height = height;
	window->x = x;
	window->y = y;
	window->z = index;
	/* XXX */
	window->buffer = (void *)malloc(sizeof(uint32_t) * window->width * window->height);
}

void window_set_point(window_t * window, uint16_t x, uint16_t y, uint32_t color) {
	((uint32_t *)window->buffer)[DIRECT_OFFSET(x,y)] = color;
}

void window_draw_line(window_t * window, uint16_t x0, uint16_t x1, uint16_t y0, uint16_t y1, uint32_t color) {
	int deltax = abs(x1 - x0);
	int deltay = abs(y1 - y0);
	int sx = (x0 < x1) ? 1 : -1;
	int sy = (y0 < y1) ? 1 : -1;
	int error = deltax - deltay;
	while (1) {
		window_set_point(window, x0, y0, color);
		if (x0 == x1 && y0 == y1) break;
		int e2 = 2 * error;
		if (e2 > -deltay) {
			error -= deltay;
			x0 += sx;
		}
		if (e2 < deltax) {
			error += deltax;
			y0 += sy;
		}
	}
}

void window_fill(window_t *window, uint32_t color) {
	for (uint16_t i = 0; i < window->height; ++i) {
		for (uint16_t j = 0; j < window->width; ++j) {
			((uint32_t *)window->buffer)[DIRECT_OFFSET(j,i)] = color;
		}
	}
}

int main(int argc, char * argv[]) {
	init_graphics();
	window_list = list_create();

	window_t wina, winb, root, panel;

	init_window(&root, 0, 0, graphics_width, graphics_height, 0);
	list_insert(window_list, &root);
	window_fill(&root, rgb(20,20,20));

	init_window(&panel, 0, 0, graphics_width, 24, -1);
	list_insert(window_list, &panel);
	window_fill(&panel, rgb(20,40,60));

	init_window(&wina, 10, 10, 300, 300, 1);
	list_insert(window_list, &wina);
	window_fill(&wina, rgb(0,255,0));

	init_window(&winb, 120, 120, 300, 300, 2);
	list_insert(window_list, &winb);
	window_fill(&winb, rgb(0,0,255));

	redraw_window(&root); /* We only need to redraw root if things move around */
	redraw_window(&panel);

	int16_t direction_x = 1;
	int16_t direction_y = 1;

	while (1) {
		window_draw_line(&wina, rand() % 300, rand() % 300, rand() % 300, rand() % 300, rgb(rand() % 255,rand() % 255,rand() % 255));
		window_draw_line(&winb, rand() % 300, rand() % 300, rand() % 300, rand() % 300, rgb(rand() % 255,rand() % 255,rand() % 255));
		redraw_window(&wina);
		redraw_window(&winb);
		redraw_window(&root);

		winb.x += direction_x;
		winb.y += direction_y;
		if (winb.x >= graphics_width - winb.width) {
			direction_x = -1;
		}
		if (winb.y >= graphics_height - winb.height) {
			direction_y = -1;
		}
		if (winb.x <= 0) {
			direction_x = 1;
		}
		if (winb.y <= 0) {
			direction_y = 1;
		}
	}
}
