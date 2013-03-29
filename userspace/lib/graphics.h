/* vim: tabstop=4 shiftwidth=4 noexpandtab
 */
#ifndef LIB_GRAPHICS_H
#define LIB_GRAPHICS_H

#include <syscall.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GFX_W(ctx)  ((ctx)->width)			/* Display width */
#define GFX_H(ctx)  ((ctx)->height)			/* Display height */
#define GFX_B(ctx)  ((ctx)->depth / 8)		/* Display byte depth */

#define _RED(color) ((color & 0x00FF0000) / 0x10000)
#define _GRE(color) ((color & 0x0000FF00) / 0x100)
#define _BLU(color) ((color & 0x000000FF) / 0x1)
#define _ALP(color) ((color & 0xFF000000) / 0x1000000)

/*
 * Macros make verything easier.
 */
#define GFX(ctx,x,y) *((uint32_t *)&((ctx)->backbuffer)[(GFX_W(ctx) * (y) + (x)) * GFX_B(ctx)])
#define SPRITE(sprite,x,y) sprite->bitmap[sprite->width * (y) + (x)]
#define SMASKS(sprite,x,y) sprite->masks[sprite->width * (y) + (x)]

typedef struct sprite {
	uint16_t width;
	uint16_t height;
	uint32_t * bitmap;
	uint32_t * masks;
	uint32_t blank;
	uint8_t  alpha;
} sprite_t;

typedef struct context {
	uint16_t width;
	uint16_t height;
	uint16_t depth;
	uint32_t size;
	char *   buffer;
	char *   backbuffer;
} gfx_context_t;

gfx_context_t * init_graphics_fullscreen();
gfx_context_t * init_graphics_fullscreen_double_buffer();

#define ALPHA_OPAQUE   0
#define ALPHA_MASK     1
#define ALPHA_EMBEDDED 2
#define ALPHA_INDEXED  3

uint32_t rgb(uint8_t r, uint8_t g, uint8_t b);
uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
uint32_t alpha_blend(uint32_t bottom, uint32_t top, uint32_t mask);
uint32_t alpha_blend_rgba(uint32_t bottom, uint32_t top);

void flip(gfx_context_t * ctx);
void clear_buffer(gfx_context_t * ctx);

gfx_context_t * init_graphics_sprite(sprite_t * sprite);
sprite_t * create_sprite(size_t width, size_t height, int alpha);

void blur_context(gfx_context_t * _dst, gfx_context_t * _src, double amount);
void sprite_free(sprite_t * sprite);

void load_sprite(sprite_t * sprite, char * filename);
int load_sprite_png(sprite_t * sprite, char * file);
void draw_sprite(gfx_context_t * ctx, sprite_t * sprite, int32_t x, int32_t y);
void draw_line(gfx_context_t * ctx, int32_t x0, int32_t x1, int32_t y0, int32_t y1, uint32_t color);
void draw_line_thick(gfx_context_t * ctx, int32_t x0, int32_t x1, int32_t y0, int32_t y1, uint32_t color, char thickness);
void draw_fill(gfx_context_t * ctx, uint32_t color);

void draw_sprite_scaled(gfx_context_t * ctx, sprite_t * sprite, int32_t x, int32_t y, uint16_t width, uint16_t height);

void context_to_png(FILE * file, gfx_context_t * ctx);

uint32_t premultiply(uint32_t color);


#endif
