#pragma once

#include <_cheader.h>
#include <stdint.h>
#include <stddef.h>

_Begin_C_Header

#define GFX_W(ctx)  ((ctx)->width)			/* Display width */
#define GFX_H(ctx)  ((ctx)->height)			/* Display height */
#define GFX_B(ctx)  ((ctx)->depth / 8)		/* Display byte depth */
#define GFX_S(ctx)  ((ctx)->stride)			/* Stride */

#define _RED(color) ((color & 0x00FF0000) / 0x10000)
#define _GRE(color) ((color & 0x0000FF00) / 0x100)
#define _BLU(color) ((color & 0x000000FF) / 0x1)
#define _ALP(color) ((color & 0xFF000000) / 0x1000000)

/*
 * Macros make verything easier.
 */
#define GFX(ctx,x,y) *((uint32_t *)&((ctx)->backbuffer)[(GFX_S(ctx) * (y) + (x) * GFX_B(ctx))])
#define GFXR(ctx,x,y) *((uint32_t *)&((ctx)->buffer)[(GFX_S(ctx) * (y) + (x) * GFX_B(ctx))])
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
	char *   clips;
	int32_t  clips_size;
	uint32_t stride;

	uint32_t _true_stride;
} gfx_context_t;

extern gfx_context_t * init_graphics_fullscreen();
extern gfx_context_t * init_graphics_fullscreen_double_buffer();
extern void reinit_graphics_fullscreen(gfx_context_t * ctx);

#define ALPHA_OPAQUE   0
#define ALPHA_MASK     1
#define ALPHA_EMBEDDED 2
#define ALPHA_INDEXED  3
#define ALPHA_FORCE_SLOW_EMBEDDED 4

extern uint32_t rgb(uint8_t r, uint8_t g, uint8_t b);
extern uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
extern uint32_t alpha_blend(uint32_t bottom, uint32_t top, uint32_t mask);
extern uint32_t alpha_blend_rgba(uint32_t bottom, uint32_t top);
extern uint32_t framebuffer_stride(void);

extern void flip(gfx_context_t * ctx);
void clear_buffer(gfx_context_t * ctx);

extern gfx_context_t * init_graphics_sprite(sprite_t * sprite);
extern sprite_t * create_sprite(size_t width, size_t height, int alpha);

extern void blur_context(gfx_context_t * _dst, gfx_context_t * _src, double amount);
extern void blur_context_no_vignette(gfx_context_t * _dst, gfx_context_t * _src, double amount);
extern void blur_context_box(gfx_context_t * _src, int radius);
extern void sprite_free(sprite_t * sprite);

extern void draw_line(gfx_context_t * ctx, int32_t x0, int32_t x1, int32_t y0, int32_t y1, uint32_t color);
extern void draw_line_thick(gfx_context_t * ctx, int32_t x0, int32_t x1, int32_t y0, int32_t y1, uint32_t color, char thickness);
extern void draw_fill(gfx_context_t * ctx, uint32_t color);

typedef double gfx_matrix_t[2][3];

extern int load_sprite(sprite_t * sprite, const char * filename);
extern int load_sprite_bmp(sprite_t * sprite, const char * filename);
extern void draw_sprite(gfx_context_t * ctx, const sprite_t * sprite, int32_t x, int32_t y);
extern void draw_sprite_scaled(gfx_context_t * ctx, const sprite_t * sprite, int32_t x, int32_t y, uint16_t width, uint16_t height);
extern void draw_sprite_scaled_alpha(gfx_context_t * ctx, const sprite_t * sprite, int32_t x, int32_t y, uint16_t width, uint16_t height, float alpha);
extern void draw_sprite_alpha(gfx_context_t * ctx, const sprite_t * sprite, int32_t x, int32_t y, float alpha);
extern void draw_sprite_alpha_paint(gfx_context_t * ctx, const sprite_t * sprite, int32_t x, int32_t y, float alpha, uint32_t c);
extern void draw_sprite_rotate(gfx_context_t * ctx, const sprite_t * sprite, int32_t x, int32_t y, float rotation, float alpha);
extern void draw_sprite_transform(gfx_context_t * ctx, const sprite_t * sprite, gfx_matrix_t matrix, float alpha);

//extern void context_to_png(FILE * file, gfx_context_t * ctx);

extern uint32_t premultiply(uint32_t color);

extern void gfx_add_clip(gfx_context_t * ctx, int32_t x, int32_t y, int32_t w, int32_t h);
extern void gfx_clear_clip(gfx_context_t * ctx);
extern void gfx_no_clip(gfx_context_t * ctx);

extern uint32_t interp_colors(uint32_t bottom, uint32_t top, uint8_t interp);
extern void draw_rounded_rectangle(gfx_context_t * ctx, int32_t x, int32_t y, uint16_t width, uint16_t height, int radius, uint32_t color);
extern void draw_rectangle(gfx_context_t * ctx, int32_t x, int32_t y, uint16_t width, uint16_t height, uint32_t color);
extern void draw_rectangle_solid(gfx_context_t * ctx, int32_t x, int32_t y, uint16_t width, uint16_t height, uint32_t color);
extern void draw_rounded_rectangle_pattern(gfx_context_t * ctx, int32_t x, int32_t y, uint16_t width, uint16_t height, int radius, uint32_t (*pattern)(int32_t x, int32_t y, double alpha, void * extra), void * extra);

struct gfx_point {
	float x;
	float y;
};

extern float gfx_point_distance(const struct gfx_point * a, const struct gfx_point * b);
extern float gfx_point_distance_squared(const struct gfx_point * a, const struct gfx_point * b);
extern float gfx_point_dot(const struct gfx_point * a, const struct gfx_point * b);
extern struct gfx_point gfx_point_sub(const struct gfx_point * a, const struct gfx_point * b);
extern struct gfx_point gfx_point_add(const struct gfx_point * a, const struct gfx_point * b);
extern float gfx_line_distance(const struct gfx_point * p, const struct gfx_point * v, const struct gfx_point * w);
extern void draw_line_aa_points(gfx_context_t * ctx, struct gfx_point *v, struct gfx_point *w, uint32_t color, float thickness);
extern void draw_line_aa(gfx_context_t * ctx, int x_1, int x_2, int y_1, int y_2, uint32_t color, float thickness);

struct gradient_definition {
	int height;
	int y;
	uint32_t top;
	uint32_t bottom;
};

extern uint32_t gfx_vertical_gradient_pattern(int32_t x, int32_t y, double alpha, void * extra);

extern gfx_context_t * init_graphics_subregion(gfx_context_t * base, int x, int y, int width, int height);

extern void gfx_matrix_identity(gfx_matrix_t);
extern void gfx_matrix_scale(gfx_matrix_t, double x, double y);
extern void gfx_matrix_translate(gfx_matrix_t, double x, double y);
extern void gfx_matrix_rotate(gfx_matrix_t, double rotation);
extern void gfx_matrix_shear(gfx_matrix_t matrix, double x, double y);
extern int gfx_matrix_invert(gfx_matrix_t m, gfx_matrix_t inverse);
extern void gfx_apply_matrix(double x, double y, gfx_matrix_t matrix, double *out_x, double *out_y);


_End_C_Header
