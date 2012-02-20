/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Graphics library
 */

#include <syscall.h>
#include <stdint.h>
#include "graphics.h"

DEFN_SYSCALL0(getgraphicsaddress, 11);
DEFN_SYSCALL1(kbd_mode, 12, int);
DEFN_SYSCALL0(kbd_get, 13);
DEFN_SYSCALL1(setgraphicsoffset, 16, int);

DEFN_SYSCALL0(getgraphicswidth,  18);
DEFN_SYSCALL0(getgraphicsheight, 19);
DEFN_SYSCALL0(getgraphicsdepth,  20);


uint16_t graphics_width  __attribute__ ((aligned (16))) = 0;
uint16_t graphics_height __attribute__ ((aligned (16))) = 0;
uint16_t graphics_depth  __attribute__ ((aligned (16))) = 0;

/* Pointer to graphics memory */
uint8_t * gfx_mem = 0;
uint8_t  * frame_mem;
uint32_t   gfx_size;
uint32_t flip_offset;

void flip() {
	memcpy(gfx_mem, frame_mem, gfx_size);
	memset(frame_mem, 0, GFX_H * GFX_W * GFX_B);
}

void init_graphics() {
	graphics_width  = syscall_getgraphicswidth();
	graphics_height = syscall_getgraphicsheight();
	graphics_depth  = syscall_getgraphicsdepth();
	flip_offset = GFX_H;
	gfx_size = GFX_B * GFX_H * GFX_W;
	gfx_mem = (void *)syscall_getgraphicsaddress();
	frame_mem = gfx_mem;
}

void init_graphics_double_buffer() {
	init_graphics();
	frame_mem = (void *)((uintptr_t)gfx_mem + sizeof(uint32_t) * GFX_W * GFX_H);
}

uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
	return 0xFF000000 + (r * 0x10000) + (g * 0x100) + (b * 0x1);
}

uint32_t alpha_blend(uint32_t bottom, uint32_t top, uint32_t mask) {
	float a = _RED(mask) / 256.0;
	uint8_t red = _RED(bottom) * (1.0 - a) + _RED(top) * a;
	uint8_t gre = _GRE(bottom) * (1.0 - a) + _GRE(top) * a;
	uint8_t blu = _BLU(bottom) * (1.0 - a) + _BLU(top) * a;
	return rgb(red,gre,blu);
}

void load_sprite(sprite_t * sprite, char * filename) {
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
				color =	(bufferb[i   + 3 * x] & 0xFF) +
						(bufferb[i+1 + 3 * x] & 0xFF) * 0x100 +
						(bufferb[i+2 + 3 * x] & 0xFF) * 0x10000;
			} else if (bpp == 32) {
				color =	(bufferb[i   + 4 * x] & 0xFF) * 0x1000000 +
						(bufferb[i+1 + 4 * x] & 0xFF) * 0x100 +
						(bufferb[i+2 + 4 * x] & 0xFF) * 0x10000 +
						(bufferb[i+3 + 4 * x] & 0xFF) * 0x1;
			}
			/* Set our point */
			sprite->bitmap[(height - y - 1) * width + x] = color;
		}
		i += row_width;
	}
	free(bufferb);
}

static inline int32_t min(int32_t a, int32_t b) {
	return (a < b) ? a : b;
}

static inline int32_t max(int32_t a, int32_t b) {
	return (a > b) ? a : b;
}

void draw_sprite(sprite_t * sprite, int32_t x, int32_t y) {
	int32_t _left   = max(x, 0);
	int32_t _top    = max(y, 0);
	int32_t _right  = min(x + sprite->width,  graphics_width - 1);
	int32_t _bottom = min(y + sprite->height, graphics_height - 1);
	for (uint16_t _y = 0; _y < sprite->height; ++_y) {
		for (uint16_t _x = 0; _x < sprite->width; ++_x) {
			if (x + _x < _left || x + _x > _right || y + _y < _top || y + _y > _bottom)
				continue;
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

