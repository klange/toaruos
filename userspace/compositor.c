/*
 * The ToAru Sample Game
 */

#include <stdio.h>
#include <stdint.h>
#include <syscall.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_CACHE_H

#include "lib/list.h"
#include "lib/graphics.h"

/* For terminal, not for us */
#define FREETYPE 1

sprite_t * sprites[128];

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

void window_draw_sprite(window_t * window, sprite_t * sprite, uint16_t x, uint16_t y) {
	for (uint16_t _y = 0; _y < sprite->height; ++_y) {
		for (uint16_t _x = 0; _x < sprite->width; ++_x) {
			if (sprite->alpha) {
				/* Technically, unsupported! */
				window_set_point(window, x + _x, y + _y, SPRITE(sprite, _x, _y));
			} else {
				if (SPRITE(sprite,_x,_y) != sprite->blank) {
					window_set_point(window, x + _x, y + _y, SPRITE(sprite, _x, _y));
				}
			}
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

void waitabit() {
	int x = time(NULL);
	while (time(NULL) < x + 1) {
		// Do nothing.
	}
}

sprite_t alpha_tmp;

void init_sprite(int i, char * filename, char * alpha) {
	sprites[i] = malloc(sizeof(sprite_t));
	load_sprite(sprites[i], filename);
	if (alpha) {
		sprites[i]->alpha = 1;
		load_sprite(&alpha_tmp, alpha);
		sprites[i]->masks = alpha_tmp.bitmap;
	} else {
		sprites[i]->alpha = 0;
	}
	sprites[i]->blank = 0x0;
}

int center_x(int x) {
	return (graphics_width - x) / 2;
}

int center_y(int y) {
	return (graphics_height - y) / 2;
}

static int progress = 0;
static int progress_width = 0;

#define PROGRESS_WIDTH  120
#define PROGRESS_HEIGHT 6
#define PROGRESS_OFFSET 50

void draw_progress() {
	int x = center_x(PROGRESS_WIDTH);
	int y = center_y(0);
	uint32_t color = rgb(0,120,230);
	uint32_t fill  = rgb(0,70,160);
	draw_line(x, x + PROGRESS_WIDTH, y + PROGRESS_OFFSET, y + PROGRESS_OFFSET, color);
	draw_line(x, x + PROGRESS_WIDTH, y + PROGRESS_OFFSET + PROGRESS_HEIGHT, y + PROGRESS_OFFSET + PROGRESS_HEIGHT, color);
	draw_line(x, x, y + PROGRESS_OFFSET, y + PROGRESS_OFFSET + PROGRESS_HEIGHT, color);
	draw_line(x + PROGRESS_WIDTH, x + PROGRESS_WIDTH, y + PROGRESS_OFFSET, y + PROGRESS_OFFSET + PROGRESS_HEIGHT, color);

	if (progress_width > 0) {
		int width = ((PROGRESS_WIDTH - 2) * progress) / progress_width;
		for (int8_t i = 0; i < PROGRESS_HEIGHT - 1; ++i) {
			draw_line(x + 1, x + 1 + width, y + PROGRESS_OFFSET + i + 1, y + PROGRESS_OFFSET + i + 1, fill);
		}
	}

}

uint32_t gradient_at(uint16_t j) {
	float x = j * 80;
	x = x / graphics_height;
	return rgb(0, 1 * x, 2 * x);
}

void display() {
	for (uint16_t j = 0; j < graphics_height; ++j) {
		draw_line(0, graphics_width, j, j, gradient_at(j));
	}
	draw_sprite(sprites[0], center_x(sprites[0]->width), center_y(sprites[0]->height));
	draw_progress();
	flip();
}


typedef struct {
	void (*func)();
	char * name;
	int  time;
} startup_item;

list_t * startup_items;

void add_startup_item(char * name, void (*func)(), int time) {
	progress_width += time;
	startup_item * item = malloc(sizeof(startup_item));

	item->name = name;
	item->func = func;
	item->time = time;

	list_insert(startup_items, item);
}

static void test() {
	/* Do Nothing */
}

void run_startup_item(startup_item * item) {
	printf("[compositor] Running startup item: %s\n", item->name);
	item->func();
	progress += item->time;
}

FT_Library   library;
FT_Face      face;
FT_Face      face_bold;
FT_Face      face_italic;
FT_Face      face_bold_italic;
FT_Face      face_extra;
FT_GlyphSlot slot;
FT_UInt      glyph_index;

char * loadMemFont(char * name, size_t * size) {
	FILE * f = fopen(name, "r");
	size_t s = 0;
	fseek(f, 0, SEEK_END);
	s = ftell(f);
	fseek(f, 0, SEEK_SET);
	char * font = malloc(s);
	fread(font, s, 1, f);
	fclose(f);
	*size = s;
	return font;
}

void _init_freetype() {
	int error;
	error = FT_Init_FreeType(&library);
}

#define FONT_SIZE 13

int error;

void _load_dejavu() {
	char * font;
	size_t s;
	font = loadMemFont("/usr/share/fonts/DejaVuSans.ttf", &s);
	error = FT_New_Memory_Face(library, font, s, 0, &face);
	error = FT_Set_Pixel_Sizes(face, FONT_SIZE, FONT_SIZE);
}

void _load_dejavubold() {
	char * font;
	size_t s;
	font = loadMemFont("/usr/share/fonts/DejaVuSans-Bold.ttf", &s);
	error = FT_New_Memory_Face(library, font, s, 0, &face_bold);
	error = FT_Set_Pixel_Sizes(face_bold, FONT_SIZE, FONT_SIZE);
}

void _load_dejavuitalic() {
	char * font;
	size_t s;
	font = loadMemFont("/usr/share/fonts/DejaVuSans-Oblique.ttf", &s);
	error = FT_New_Memory_Face(library, font, s, 0, &face_italic);
	error = FT_Set_Pixel_Sizes(face_italic, FONT_SIZE, FONT_SIZE);
}

void _load_dejavubolditalic() {
	char * font;
	size_t s;
	font = loadMemFont("/usr/share/fonts/DejaVuSans-BoldOblique.ttf", &s);
	error = FT_New_Memory_Face(library, font, s, 0, &face_bold_italic);
	error = FT_Set_Pixel_Sizes(face_bold_italic, FONT_SIZE, FONT_SIZE);
}

void _load_wallpaper() {
	init_sprite(1, "/usr/share/wallpaper.bmp", NULL);
}

int main(int argc, char ** argv) {

	/* Initialize graphics setup */
	init_graphics_double_buffer();

	/* Load sprites */
	init_sprite(0, "/usr/share/bs.bmp", "/usr/share/bs-alpha.bmp");
	display();

	/* Count startup items */
	startup_items = list_create();
	add_startup_item("Initializing FreeType", _init_freetype, 1);
	add_startup_item("Loading font: Deja Vu Sans", _load_dejavu, 2);
	add_startup_item("Loading font: Deja Vu Sans Bold", _load_dejavubold, 2);
	add_startup_item("Loading font: Deja Vu Sans Oblique", _load_dejavuitalic, 2);
	add_startup_item("Loading font: Deja Vu Sans Bold+Oblique", _load_dejavubolditalic, 2);
	add_startup_item("Loading wallpaper (/usr/share/wallpaper.bmp)", _load_wallpaper, 4);

	foreach(node, startup_items) {
		run_startup_item((startup_item *)node->value);
		display();
	}

#if 0
	/* Reinitialize for single buffering */
	init_graphics();
#endif

	window_list = list_create();

	window_t wina, winb, root, panel;

	init_window(&root, 0, 0, graphics_width, graphics_height, 0);
	list_insert(window_list, &root);
#if 0
	uint32_t odd = 0;
	uint32_t black = rgb(0,0,0);
	uint32_t white = rgb(255,255,255);
	for (uint16_t j = 0; j < root.height; ++j) {
		for (uint16_t i = 0; i < root.width; ++i) {
			odd++;
			if ((odd + j) % 2) {
				window_set_point(&root, i, j, black);
			} else {
				window_set_point(&root, i, j, white);
				
			}
		}
	}
#endif
	window_draw_sprite(&root, sprites[1], 0, 0);

	init_window(&panel, 0, 0, graphics_width, 24, -1);
	list_insert(window_list, &panel);
	window_fill(&panel, rgb(0,120,230));
	init_sprite(2, "/usr/share/panel.bmp", NULL);
	for (uint32_t i = 0; i < graphics_width; i += sprites[2]->width) {
		window_draw_sprite(&panel, sprites[2], i, 0);
	}

	init_window(&wina, 10, 10, 300, 300, 1);
	list_insert(window_list, &wina);
	window_fill(&wina, rgb(0,255,0));

	init_window(&winb, 120, 120, 300, 300, 2);
	list_insert(window_list, &winb);
	window_fill(&winb, rgb(0,0,255));

	redraw_window(&root); /* We only need to redraw root if things move around */
	redraw_window(&panel);
	redraw_window(&wina);
	redraw_window(&winb);

	flip();

	while (1) {
		window_draw_line(&wina, rand() % 300, rand() % 300, rand() % 300, rand() % 300, rgb(rand() % 255,rand() % 255,rand() % 255));
		window_draw_line(&winb, rand() % 300, rand() % 300, rand() % 300, rand() % 300, rgb(rand() % 255,rand() % 255,rand() % 255));
		redraw_window(&wina);
		redraw_window(&winb);
		redraw_window(&root);
		redraw_window(&panel);
		flip();
	}


	return 0;
}
