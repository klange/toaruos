/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Graphics library
 */

#include <syscall.h>
#include <stdint.h>
#include <math.h>
#include "graphics.h"
#include "window.h"

#define PNG_DEBUG 3
#include <png.h>

static inline int32_t min(int32_t a, int32_t b) {
	return (a < b) ? a : b;
}

static inline int32_t max(int32_t a, int32_t b) {
	return (a > b) ? a : b;
}

static inline uint16_t min16(uint16_t a, uint16_t b) {
	return (a < b) ? a : b;
}

static inline uint16_t max16(uint16_t a, uint16_t b) {
	return (a > b) ? a : b;
}



/* Pointer to graphics memory */
void flip(gfx_context_t * ctx) {
	memcpy(ctx->buffer, ctx->backbuffer, ctx->size);
}

void clearbuffer(gfx_context_t * ctx) {
	memset(ctx->backbuffer, 0, ctx->size);
}

/* Deprecated */
gfx_context_t * init_graphics_fullscreen() {
	gfx_context_t * out = malloc(sizeof(gfx_context_t));
	out->width  = syscall_getgraphicswidth();
	out->height = syscall_getgraphicsheight();
	out->depth  = syscall_getgraphicsdepth();
	out->size   = GFX_H(out) * GFX_W(out) * GFX_B(out);
	out->buffer = (void *)syscall_getgraphicsaddress();
	out->backbuffer = out->buffer;
	return out;
}

gfx_context_t * init_graphics_fullscreen_double_buffer() {
	gfx_context_t * out = init_graphics_fullscreen();
	out->backbuffer = malloc(sizeof(uint32_t) * GFX_W(out) * GFX_H(out));
	return out;
}

gfx_context_t * init_graphics_window(window_t * window) {
	gfx_context_t * out = malloc(sizeof(gfx_context_t));
	out->width  = window->width;
	out->height = window->height;
	out->depth  = 32;
	out->size   = GFX_H(out) * GFX_W(out) * GFX_B(out);
	out->buffer = window->buffer;
	out->backbuffer = out->buffer;
	return out;
}

gfx_context_t *  init_graphics_window_double_buffer(window_t * window) {
	gfx_context_t * out = init_graphics_window(window);
	out->backbuffer = malloc(sizeof(uint32_t) * GFX_W(out) * GFX_H(out));
	return out;
}

void reinit_graphics_window(gfx_context_t * out, window_t * window) {
	out->width  = window->width;
	out->height = window->height;
	out->depth  = 32;
	out->size   = GFX_H(out) * GFX_W(out) * GFX_B(out);
	if (out->buffer == out->backbuffer) {
		out->buffer = window->buffer;
		out->backbuffer = out->buffer;
	} else {
		out->buffer = window->buffer;
		out->backbuffer = realloc(out->backbuffer, sizeof(uint32_t) * GFX_W(out) * GFX_H(out));
	}
}

gfx_context_t * init_graphics_sprite(sprite_t * sprite) {
	gfx_context_t * out = malloc(sizeof(gfx_context_t));

	out->width  = sprite->width;
	out->height = sprite->height;
	out->depth  = 32;
	out->size   = GFX_H(out) * GFX_W(out) * GFX_B(out);
	out->buffer = (char *)sprite->bitmap;
	out->backbuffer = out->buffer;

	return out;
}

sprite_t * create_sprite(size_t width, size_t height, int alpha) {
	sprite_t * out = malloc(sizeof(sprite_t));

	/*
	uint16_t width;
	uint16_t height;
	uint32_t * bitmap;
	uint32_t * masks;
	uint32_t blank;
	uint8_t  alpha;
	*/

	out->width  = width;
	out->height = height;
	out->bitmap = malloc(sizeof(uint32_t) * out->width * out->height);
	out->masks  = NULL;
	out->blank  = 0x00000000;
	out->alpha  = alpha;

	return out;
}

void sprite_free(sprite_t * sprite) {
	if (sprite->masks) {
		free(sprite->masks);
	}
	free(sprite->bitmap);
	free(sprite);
}

uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
	return 0xFF000000 + (r * 0x10000) + (g * 0x100) + (b * 0x1);
}

uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
	return (a * 0x1000000) + (r * 0x10000) + (g * 0x100) + (b * 0x1);
}

uint32_t alpha_blend(uint32_t bottom, uint32_t top, uint32_t mask) {
	uint8_t a = _RED(mask);
	uint8_t red = (_RED(bottom) * (255 - a) + _RED(top) * a) / 255;
	uint8_t gre = (_GRE(bottom) * (255 - a) + _GRE(top) * a) / 255;
	uint8_t blu = (_BLU(bottom) * (255 - a) + _BLU(top) * a) / 255;
	uint8_t alp = (int)a + (int)_ALP(bottom) > 255 ? 255 : a + _ALP(bottom);
	return rgba(red,gre,blu, alp);
}

#define DONT_USE_FLOAT_FOR_ALPHA 1

uint32_t alpha_blend_rgba(uint32_t bottom, uint32_t top) {
	if (_ALP(bottom) == 0) return top;
	if (_ALP(top) == 255) return top;
	if (_ALP(top) == 0) return bottom;
#if DONT_USE_FLOAT_FOR_ALPHA
	uint16_t a = _ALP(top);
	uint16_t c = 255 - a;
	uint16_t b = ((int)_ALP(bottom) * c) / 255;
	uint16_t alp = min16(a + b, 255);
	uint16_t red = min16((uint32_t)(_RED(bottom) * c + _RED(top) * 255) / 255, 255);
	uint16_t gre = min16((uint32_t)(_GRE(bottom) * c + _GRE(top) * 255) / 255, 255);
	uint16_t blu = min16((uint32_t)(_BLU(bottom) * c + _BLU(top) * 255) / 255, 255);
	return rgba(red,gre,blu,alp);
#else
	double a = _ALP(top) / 255.0;
	double c = 1.0 - a;
	double b = (_ALP(bottom) / 255.0) * c;
	double alp = a + b; if (alp > 1.0) alp = 1.0;
	double red = (_RED(bottom) / 255.0) * c + (_RED(top) / 255.0); if (red > 1.0) red = 1.0;
	double gre = (_GRE(bottom) / 255.0) * c + (_GRE(top) / 255.0); if (gre > 1.0) gre = 1.0;
	double blu = (_BLU(bottom) / 255.0) * c + (_BLU(top) / 255.0); if (blu > 1.0) blu = 1.0;
	return rgba(red * 255, gre * 255, blu * 255, alp * 255);
#endif
}


uint32_t premultiply(uint32_t color) {
	uint16_t a = _ALP(color);
	uint16_t r = _RED(color);
	uint16_t g = _GRE(color);
	uint16_t b = _BLU(color);

	r = r * a / 256;
	g = g * a / 256;
	b = b * a / 256;
	return rgba(r,g,b,a);
}

/*
 * based on the blur.c demo for Cairo
 * Copyright © 2008 Kristian Høgsberg
 * Copyright © 2009 Chris Wilson
 */
#define ARRAY_LENGTH(a) (sizeof (a) / sizeof (a)[0])

void blur_context(gfx_context_t * _dst, gfx_context_t * _src, double amount) {
	int width, height;
	int x, y, z, w, i, j, k;
	uint32_t *s, *d, a, p;
	uint8_t * src, * dst;
	uint8_t kernel[17];
	const int size = ARRAY_LENGTH(kernel);
	const int half = size / 2;

	width  = _src->width;
	height = _src->height;

	src = _src->backbuffer;
	dst = _dst->backbuffer;

	a = 0;
	for (i = 0; i < size; ++i) {
		double f = i - half;
		a += kernel[i] = exp (- f * f / amount) * 80;
	}

	for (i = 0; i < height; ++i) {
		s = (uint32_t *) (src + i * (_src->width * 4));
		d = (uint32_t *) (dst + i * (_dst->width * 4));
		for (j = 0; j < width; ++j) {
			x = y = z = w = 0;
			for (k = 0; k < size; ++k) {
				if (j - half + k < 0 || j - half + k >= width)
					continue;
				p = s[j - half + k];

				x += ((p >> 24) & 0xFF) * kernel[k];
				y += ((p >> 16) & 0xFF) * kernel[k];
				z += ((p >>  8) & 0xFF) * kernel[k];
				w += ((p >>  0) & 0xFF) * kernel[k];
			}

			d[j] = (x / a << 24) | (y / a << 16) | (z / a << 8) | w / a;
		}
	}

	for (i = 0; i < height; ++i) {
		s = (uint32_t *) (src + i * (_src->width * 4));
		d = (uint32_t *) (dst + i * (_dst->width * 4));
		for (j = 0; j < width; ++j) {
			x = y = z = w = 0;
			for (k = 0; k < size; ++k) {
				if (i - half + k < 0 || i - half + k >= height)
					continue;

				s = (uint32_t *) (dst + (i - half + k) * (_dst->width * 4));
				p = s[j];

				x += ((p >> 24) & 0xFF) * kernel[k];
				y += ((p >> 16) & 0xFF) * kernel[k];
				z += ((p >>  8) & 0xFF) * kernel[k];
				w += ((p >>  0) & 0xFF) * kernel[k];
			}

			d[j] = (x / a << 24) | (y / a << 16) | (z / a << 8) | w / a;
		}
	}
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

int load_sprite_png(sprite_t * sprite, char * file) {
	png_structp png_ptr;
	png_infop info_ptr;
	int number_of_passes;
	png_bytep * row_pointers;
	int x, y;
	png_uint_32 width, height;
	int color_type;
	int bit_depth;

	char header[8];

	FILE *fp = fopen(file, "rb");
	if (!fp) {
		printf("Oh dear. Failed to open wallpaper file.\n");
		return 1;
	}
	fread(header, 1, 8, fp);
	if (png_sig_cmp(header, 0, 8)) {
		printf("Oh dear. Bad signature.\n");
		return 1;
	}

	png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!png_ptr) {
		printf("Oh dear. Couldn't make a read struct.\n");
		return 1;
	}
	info_ptr = png_create_info_struct(png_ptr);

	png_init_io(png_ptr, fp);
	png_set_sig_bytes(png_ptr, 8);
	png_read_info(png_ptr, info_ptr);

	png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type, NULL, NULL, NULL);

	row_pointers = (png_bytep*) malloc(sizeof(png_bytep) * height);
	for (y = 0; y < height; ++y) {
		row_pointers[y] = (png_byte *) malloc(png_get_rowbytes(png_ptr, info_ptr));
	}
	png_read_image(png_ptr, row_pointers);
	fclose(fp);

	sprite->width = width;
	sprite->height = height;
	sprite->bitmap = malloc(sizeof(uint32_t) * width * height);
	sprite->alpha = 0;
	sprite->blank = 0;

	if (color_type == 2) {
		sprite->alpha = ALPHA_OPAQUE;
		for (y = 0; y < height; ++y) {
			png_byte* row = row_pointers[y];
			for (x = 0; x < width; ++x) {
				png_byte * ptr = &(row[x*3]);
				sprite->bitmap[(y) * width + x] = rgb(ptr[0], ptr[1], ptr[2]);
			}
		}
	} else if (color_type == 6) {
		sprite->alpha = ALPHA_EMBEDDED;
		for (y = 0; y < height; ++y) {
			png_byte* row = row_pointers[y];
			for (x = 0; x < width; ++x) {
				png_byte * ptr = &(row[x*4]);
				sprite->bitmap[(y) * width + x] = premultiply(rgba(ptr[0], ptr[1], ptr[2], ptr[3]));
			}
		}

	} else {
		printf("XXX: UNKNOWN COLOR TYPE: %d!\n", color_type);
	}

	for (y = 0; y < height; ++y) {
		free(row_pointers[y]);
	}
	free(row_pointers);

	return 0;
}


void context_to_png(FILE * file, gfx_context_t * ctx) {
	png_structp png_ptr  = NULL;
	png_infop   info_ptr = NULL;
	int32_t x, y;

	png_byte ** row_pointers = NULL;

	int status = -1;
	int depth  = 8;

	png_ptr  = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	info_ptr = png_create_info_struct(png_ptr);

	if (setjmp(png_jmpbuf(png_ptr))) {
		goto png_write_failure;
	}

	png_set_IHDR(png_ptr, info_ptr,
			ctx->width, ctx->height, depth,
			PNG_COLOR_TYPE_RGB_ALPHA, PNG_INTERLACE_NONE,
			PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

	row_pointers = png_malloc(png_ptr, ctx->height * sizeof(png_byte *));
	for (y = 0; y < ctx->height; ++y) {
		png_byte * row = png_malloc(png_ptr, sizeof(uint8_t) * ctx->width * sizeof(uint32_t));
		row_pointers[y] = row;
		for (x = 0; x < ctx->width; ++x) {
			uint32_t pixel = GFX(ctx, x, y);
			*row++ = _RED(pixel);
			*row++ = _GRE(pixel);
			*row++ = _BLU(pixel);
			*row++ = _ALP(pixel);
		}
	}

	png_init_io(png_ptr, file);
	png_set_rows(png_ptr, info_ptr, row_pointers);
	png_write_png(png_ptr, info_ptr, PNG_TRANSFORM_IDENTITY, NULL);

	for (y = 0; y < ctx->height; y++) {
		png_free(png_ptr, row_pointers[y]);
	}
	png_free(png_ptr, row_pointers);

	fprintf(stderr, "Done writing PNG.\n");
	return;

	png_write_failure:
	fprintf(stderr, "There was an exception while trying to write out a PNG file :(\n");
	return;
}


void draw_sprite(gfx_context_t * ctx, sprite_t * sprite, int32_t x, int32_t y) {
	int32_t _left   = max(x, 0);
	int32_t _top    = max(y, 0);
	int32_t _right  = min(x + sprite->width,  ctx->width - 1);
	int32_t _bottom = min(y + sprite->height, ctx->height - 1);
	for (uint16_t _y = 0; _y < sprite->height; ++_y) {
		for (uint16_t _x = 0; _x < sprite->width; ++_x) {
			if (x + _x < _left || x + _x > _right || y + _y < _top || y + _y > _bottom)
				continue;
			if (sprite->alpha == ALPHA_MASK) {
				GFX(ctx, x + _x, y + _y) = alpha_blend(GFX(ctx, x + _x, y + _y), SPRITE(sprite, _x, _y), SMASKS(sprite, _x, _y));
			} else if (sprite->alpha == ALPHA_EMBEDDED) {
				GFX(ctx, x + _x, y + _y) = alpha_blend_rgba(GFX(ctx, x + _x, y + _y), SPRITE(sprite, _x, _y));
			} else if (sprite->alpha == ALPHA_INDEXED) {
				if (SPRITE(sprite, _x, _y) != sprite->blank) {
					GFX(ctx, x + _x, y + _y) = SPRITE(sprite, _x, _y) | 0xFF000000;
				}
			} else {
				GFX(ctx, x + _x, y + _y) = SPRITE(sprite, _x, _y) | 0xFF000000;
			}
		}
	}
}

void draw_line(gfx_context_t * ctx, int32_t x0, int32_t x1, int32_t y0, int32_t y1, uint32_t color) {
	int deltax = abs(x1 - x0);
	int deltay = abs(y1 - y0);
	int sx = (x0 < x1) ? 1 : -1;
	int sy = (y0 < y1) ? 1 : -1;
	int error = deltax - deltay;
	while (1) {
		if (x0 >= 0 && y0 >= 0 && x0 < ctx->width && y0 < ctx->height) {
			GFX(ctx, x0, y0) = color;
		}
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

void draw_line_thick(gfx_context_t * ctx, int32_t x0, int32_t x1, int32_t y0, int32_t y1, uint32_t color, char thickness) {
	int deltax = abs(x1 - x0);
	int deltay = abs(y1 - y0);
	int sx = (x0 < x1) ? 1 : -1;
	int sy = (y0 < y1) ? 1 : -1;
	int error = deltax - deltay;
	while (1) {
		for (char j = -thickness; j <= thickness; ++j) {
			for (char i = -thickness; i <= thickness; ++i) {
				if (x0 + i >= 0 && x0 + i < ctx->width && y0 + j >= 0 && y0 + j < ctx->height) {
					GFX(ctx, x0 + i, y0 + j) = color;
				}
			}
		}
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


void draw_fill(gfx_context_t * ctx, uint32_t color) {
	for (uint16_t y = 0; y < ctx->height; ++y) {
		for (uint16_t x = 0; x < ctx->width; ++x) {
			GFX(ctx, x, y) = color;
		}
	}
}

/* Bilinear filtering from Wikipedia */
uint32_t getBilinearFilteredPixelColor(sprite_t * tex, double u, double v) {
	u *= tex->width;
	v *= tex->height;
	int x = floor(u);
	int y = floor(v);
	if (x >= tex->width)  return 0;
	if (y >= tex->height) return 0;
	double u_ratio = u - x;
	double v_ratio = v - y;
	double u_o = 1 - u_ratio;
	double v_o = 1 - v_ratio;
	double r_ALP = 255;
	if (tex->alpha == ALPHA_MASK) {
		if (x == tex->width - 1 || y == tex->height - 1) return (SPRITE(tex,x,y) | 0xFF000000) & (0xFFFFFF + _RED(SMASKS(tex,x,y)) * 0x1000000);
		r_ALP = (_RED(SMASKS(tex,x,y)) * u_o + _RED(SMASKS(tex,x+1,y)) * u_ratio) * v_o + (_RED(SMASKS(tex,x,y+1)) * u_o  + _RED(SMASKS(tex,x+1,y+1)) * u_ratio) * v_ratio;
	} else if (tex->alpha == ALPHA_EMBEDDED) {
		if (x == tex->width - 1 || y == tex->height - 1) return (SPRITE(tex,x,y) | 0xFF000000) & (0xFFFFFF + _ALP(SPRITE(tex,x,y)) * 0x1000000);
		r_ALP = (_ALP(SPRITE(tex,x,y)) * u_o + _ALP(SPRITE(tex,x+1,y)) * u_ratio) * v_o + (_ALP(SPRITE(tex,x,y+1)) * u_o  + _ALP(SPRITE(tex,x+1,y+1)) * u_ratio) * v_ratio;
	}
	if (x == tex->width - 1 || y == tex->height - 1) return SPRITE(tex,x,y);
	double r_RED = (_RED(SPRITE(tex,x,y)) * u_o + _RED(SPRITE(tex,x+1,y)) * u_ratio) * v_o + (_RED(SPRITE(tex,x,y+1)) * u_o  + _RED(SPRITE(tex,x+1,y+1)) * u_ratio) * v_ratio;
	double r_BLU = (_BLU(SPRITE(tex,x,y)) * u_o + _BLU(SPRITE(tex,x+1,y)) * u_ratio) * v_o + (_BLU(SPRITE(tex,x,y+1)) * u_o  + _BLU(SPRITE(tex,x+1,y+1)) * u_ratio) * v_ratio;
	double r_GRE = (_GRE(SPRITE(tex,x,y)) * u_o + _GRE(SPRITE(tex,x+1,y)) * u_ratio) * v_o + (_GRE(SPRITE(tex,x,y+1)) * u_o  + _GRE(SPRITE(tex,x+1,y+1)) * u_ratio) * v_ratio;

	return rgb(r_RED,r_GRE,r_BLU) & (0xFFFFFF + (int)r_ALP * 0x1000000);
}

void draw_sprite_scaled(gfx_context_t * ctx, sprite_t * sprite, int32_t x, int32_t y, uint16_t width, uint16_t height) {
	int32_t _left   = max(x, 0);
	int32_t _top    = max(y, 0);
	int32_t _right  = min(x + width,  ctx->width - 1);
	int32_t _bottom = min(y + height, ctx->height - 1);
	for (uint16_t _y = 0; _y < height; ++_y) {
		for (uint16_t _x = 0; _x < width; ++_x) {
			if (x + _x < _left || x + _x > _right || y + _y < _top || y + _y > _bottom)
				continue;
			if (sprite->alpha > 0) {
				uint32_t n_color = getBilinearFilteredPixelColor(sprite, (double)_x / (double)width, (double)_y/(double)height);
				uint32_t f_color = rgb(_ALP(n_color), 0, 0);
				GFX(ctx, x + _x, y + _y) = alpha_blend(GFX(ctx, x + _x, y + _y), n_color, f_color);
			} else {
				GFX(ctx, x + _x, y + _y) = getBilinearFilteredPixelColor(sprite, (double)_x / (double)width, (double)_y/(double)height);
			}
		}
	}
}
