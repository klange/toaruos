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

#include "lib/list.h"

DEFN_SYSCALL0(getgraphicsaddress, 11);
DEFN_SYSCALL1(setgraphicsoffset, 16, int);

DEFN_SYSCALL0(getgraphicswidth,  18);
DEFN_SYSCALL0(getgraphicsheight, 19);
DEFN_SYSCALL0(getgraphicsdepth,  20);

typedef struct sprite {
	uint16_t width;
	uint16_t height;
	uint32_t * bitmap;
	uint32_t * masks;
	uint32_t blank;
	uint8_t  alpha;
} sprite_t;

uint16_t graphics_width  = 0;
uint16_t graphics_height = 0;
uint16_t graphics_depth  = 0;

#define GFX_W  graphics_width
#define GFX_H  graphics_height
#define GFX_B  (graphics_depth / 8)    /* Display byte depth */
#define GFX(x,y) *((uint32_t *)&frame_mem[(GFX_W * (y) + (x)) * GFX_B])
#define SPRITE(sprite,x,y) sprite->bitmap[sprite->width * (y) + (x)]
#define SMASKS(sprite,x,y) sprite->masks[sprite->width * (y) + (x)]

uint8_t  * gfx_mem;
uint8_t  * frame_mem;
uint32_t   gfx_size;
sprite_t * sprites[128];

uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
	return (r * 0x10000) + (g * 0x100) + (b * 0x1);
}

uint32_t flip_offset;

void flip() {
	memcpy(gfx_mem, frame_mem, gfx_size);
	memset(frame_mem, 0, GFX_H * GFX_W * GFX_B);
}

void
load_sprite(sprite_t * sprite, char * filename) {
	/* Open the requested binary */
	FILE * image = fopen(filename, "r");
	size_t image_size= 0;

	fseek(image, 0, SEEK_END);
	image_size = ftell(image);
	fseek(image, 0, SEEK_SET);

	/* Alright, we have the length */
	char * bufferb = malloc(image_size);
	fread(bufferb, image_size, 1, image);
	uint16_t x = 0; /* -> 212 */
	uint16_t y = 0; /* -> 68 */
	/* Get the width / height of the image */
	signed int *bufferi = (signed int *)((uintptr_t)bufferb + 2);
	uint32_t width  = bufferi[4];
	uint32_t height = bufferi[5];
	uint16_t bpp    = bufferi[6] / 0x10000;
	uint32_t row_width = (bpp * width + 31) / 32 * 4;
	/* Skip right to the important part */
	size_t i = bufferi[2];

	sprite->width = width;
	sprite->height = height;
	sprite->bitmap = malloc(sizeof(uint32_t) * width * height);

	for (y = 0; y < height; ++y) {
		for (x = 0; x < width; ++x) {
			if (i > image_size) return;
			/* Extract the color */
			uint32_t color;
			if (bpp == 24) {
				color =	bufferb[i   + 3 * x] +
						bufferb[i+1 + 3 * x] * 0x100 +
						bufferb[i+2 + 3 * x] * 0x10000;
			} else if (bpp == 32) {
				color =	bufferb[i   + 4 * x] * 0x1000000 +
						bufferb[i+1 + 4 * x] * 0x100 +
						bufferb[i+2 + 4 * x] * 0x10000 +
						bufferb[i+3 + 4 * x] * 0x1;
			}
			/* Set our point */
			sprite->bitmap[(height - y - 1) * width + x] = color;
		}
		i += row_width;
	}
	free(bufferb);
}

#define _RED(color) ((color & 0x00FF0000) / 0x10000)
#define _GRE(color) ((color & 0x0000FF00) / 0x100)
#define _BLU(color) ((color & 0x000000FF) / 0x1)

uint32_t alpha_blend(uint32_t bottom, uint32_t top, uint32_t mask) {
	float a = _RED(mask) / 256.0;
	uint8_t red = _RED(bottom) * (1.0 - a) + _RED(top) * a;
	uint8_t gre = _GRE(bottom) * (1.0 - a) + _GRE(top) * a;
	uint8_t blu = _BLU(bottom) * (1.0 - a) + _BLU(top) * a;
	return rgb(red,gre,blu);
}

void draw_sprite(sprite_t * sprite, uint16_t x, uint16_t y) {
	for (uint16_t _y = 0; _y < sprite->height; ++_y) {
		for (uint16_t _x = 0; _x < sprite->width; ++_x) {
			if (sprite->alpha) {
				GFX(x + _x, y + _y) = alpha_blend(GFX(x + _x, y + _y), SPRITE(sprite, _x, _y), SMASKS(sprite, _x, _y));
			} else {
				if (SPRITE(sprite,_x,_y) != sprite->blank) {
					GFX(x + _x, y + _y) = SPRITE(sprite, _x, _y);
				}
			}
		}
	}
}

void waitabit() {
	int x = time(NULL);
	while (time(NULL) < x + 1) {
		// Do nothing.
	}
}

void draw_line(uint16_t x0, uint16_t x1, uint16_t y0, uint16_t y1, uint32_t color) {
	int deltax = abs(x1 - x0);
	int deltay = abs(y1 - y0);
	int sx = (x0 < x1) ? 1 : -1;
	int sy = (y0 < y1) ? 1 : -1;
	int error = deltax - deltay;
	while (1) {
		GFX(x0, y0) = color;
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
	//void draw_line(uint16_t x0, uint16_t x1, uint16_t y0, uint16_t y1, uint32_t color) {
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

void display() {
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
	item->func();
	progress += item->time;
}

int main(int argc, char ** argv) {

	/* Initialize graphics setup */
	graphics_width  = syscall_getgraphicswidth();
	graphics_height = syscall_getgraphicsheight();
	graphics_depth  = syscall_getgraphicsdepth();
	flip_offset = GFX_H;
	gfx_size = GFX_B * GFX_H * GFX_W;
	gfx_mem = (void *)syscall_getgraphicsaddress();
	frame_mem = (void *)((uintptr_t)gfx_mem + sizeof(uint32_t) * GFX_W * GFX_H);

	/* Load sprites */
	init_sprite(0, "/usr/share/bs.bmp", NULL);
	display();

	/* Count startup items */
	startup_items = list_create();
	for (uint32_t i = 0; i < 1000; ++i) {
		add_startup_item("test", test, 1);
	}

	foreach(node, startup_items) {
		run_startup_item((startup_item *)node->value);
		display();
	}

	char * tokens[] = {
		"/bin/terminal",
		"-f",
		NULL
	};
	int i = execve(tokens[0], tokens, NULL);

	return 0;
}
