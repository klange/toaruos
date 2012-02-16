/* vim: tabstop=4 shiftwidth=4 noexpandtab
 */
#ifndef LIB_GRAPHICS_H
#define LIB_GRAPHICS_H

#include <syscall.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GFX_W  graphics_width			/* Display width */
#define GFX_H  graphics_height			/* Display height */
#define GFX_B  (graphics_depth / 8)		/* Display byte depth */

#define _RED(color) ((color & 0x00FF0000) / 0x10000)
#define _GRE(color) ((color & 0x0000FF00) / 0x100)
#define _BLU(color) ((color & 0x000000FF) / 0x1)

/*
 * Macros make verything easier.
 */
#define GFX(x,y) *((uint32_t *)&frame_mem[(GFX_W * (y) + (x)) * GFX_B])
#define SPRITE(sprite,x,y) sprite->bitmap[sprite->width * (y) + (x)]
#define SMASKS(sprite,x,y) sprite->masks[sprite->width * (y) + (x)]


DECL_SYSCALL0(getgraphicsaddress);
DECL_SYSCALL1(kbd_mode, int);
DECL_SYSCALL0(kbd_get);
DECL_SYSCALL1(setgraphicsoffset, int);

DECL_SYSCALL0(getgraphicswidth);
DECL_SYSCALL0(getgraphicsheight);
DECL_SYSCALL0(getgraphicsdepth);

uint16_t graphics_width;
uint16_t graphics_height;
uint16_t graphics_depth;

/* Pointer to graphics memory */
uint8_t * gfx_mem;
uint8_t  * frame_mem;

typedef struct sprite {
	uint16_t width;
	uint16_t height;
	uint32_t * bitmap;
	uint32_t * masks;
	uint32_t blank;
	uint8_t  alpha;
} sprite_t;


void init_graphics();
void init_graphics_double_buffer();

uint32_t rgb(uint8_t r, uint8_t g, uint8_t b);
uint32_t alpha_blend(uint32_t bottom, uint32_t top, uint32_t mask);

void flip();

void load_sprite(sprite_t * sprite, char * filename);
void draw_sprite(sprite_t * sprite, int32_t x, int32_t y);
void draw_line(uint16_t x0, uint16_t x1, uint16_t y0, uint16_t y1, uint32_t color);


#endif
