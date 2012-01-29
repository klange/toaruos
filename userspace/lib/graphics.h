/* vim: tabstop=4 shiftwidth=4 noexpandtab
 */
#include <syscall.h>
#include <stdint.h>

#define GFX_W  graphics_width			/* Display width */
#define GFX_H  graphics_height			/* Display height */
#define GFX_B  (graphics_depth / 8)		/* Display byte depth */

#define _RED(color) ((color & 0x00FF0000) / 0x10000)
#define _GRE(color) ((color & 0x0000FF00) / 0x100)
#define _BLU(color) ((color & 0x000000FF) / 0x1)

/*
 * Macros make verything easier.
 */
#define GFX(x,y) *((uint32_t *)&gfx_mem[(GFX_W * (y) + (x)) * GFX_B])

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


void init_graphics();
uint32_t rgb(uint8_t r, uint8_t g, uint8_t b);
uint32_t alpha_blend(uint32_t bottom, uint32_t top, uint32_t mask);
