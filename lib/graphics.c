/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2012-2018 K. Lange
 *
 * Generic Graphics library for ToaruOS
 */

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <fcntl.h>
#include <dlfcn.h>

#include <sys/ioctl.h>

#ifndef NO_SSE
#include <xmmintrin.h>
#include <emmintrin.h>
#endif

#include <kernel/video.h>

#include <toaru/graphics.h>

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


static int _is_in_clip(gfx_context_t * ctx, int32_t y) {
	if (!ctx->clips) return 1;
	if (y < 0 || y >= ctx->clips_size) return 1;
	return ctx->clips[y];
}


void gfx_add_clip(gfx_context_t * ctx, int32_t x, int32_t y, int32_t w, int32_t h) {
	(void)x;
	(void)w; // TODO Horizontal clipping
	if (!ctx->clips) {
		ctx->clips = malloc(ctx->height);
		memset(ctx->clips, 0, ctx->height);
		ctx->clips_size = ctx->height;
	}
	for (int i = max(y,0); i < min(y+h,ctx->clips_size); ++i) {
		ctx->clips[i] = 1;
	}
}

void gfx_clear_clip(gfx_context_t * ctx) {
	if (ctx->clips) {
		memset(ctx->clips, 0, ctx->clips_size);
	}
}

void gfx_no_clip(gfx_context_t * ctx) {
	void * tmp = ctx->clips;
	if (!tmp) return;
	ctx->clips = NULL;
	free(tmp);
}

/* Pointer to graphics memory */
void flip(gfx_context_t * ctx) {
	if (ctx->clips) {
		for (size_t i = 0; i < ctx->height; ++i) {
			if (_is_in_clip(ctx,i)) {
				memcpy(&ctx->buffer[i*GFX_S(ctx)], &ctx->backbuffer[i*GFX_S(ctx)], 4 * ctx->width);
			}
		}
	} else {
		memcpy(ctx->buffer, ctx->backbuffer, ctx->size);
	}
}

void clearbuffer(gfx_context_t * ctx) {
	memset(ctx->backbuffer, 0, ctx->size);
}

/* Deprecated */
static int framebuffer_fd = 0;
gfx_context_t * init_graphics_fullscreen() {
	gfx_context_t * out = malloc(sizeof(gfx_context_t));
	out->clips = NULL;

	if (!framebuffer_fd) {
		framebuffer_fd = open("/dev/fb0", 0, 0);
	}
	if (framebuffer_fd < 0) {
		/* oh shit */
		free(out);
		return NULL;
	}

	ioctl(framebuffer_fd, IO_VID_WIDTH,  &out->width);
	ioctl(framebuffer_fd, IO_VID_HEIGHT, &out->height);
	ioctl(framebuffer_fd, IO_VID_DEPTH,  &out->depth);
	ioctl(framebuffer_fd, IO_VID_STRIDE, &out->stride);
	ioctl(framebuffer_fd, IO_VID_ADDR,   &out->buffer);
	ioctl(framebuffer_fd, IO_VID_SIGNAL, NULL);

	out->size   = GFX_H(out) * GFX_S(out);
	out->backbuffer = out->buffer;
	return out;
}

uint32_t framebuffer_stride(void) {
	uint32_t stride;
	ioctl(framebuffer_fd, IO_VID_STRIDE, &stride);
	return stride;
}

gfx_context_t * init_graphics_fullscreen_double_buffer() {
	gfx_context_t * out = init_graphics_fullscreen();
	if (!out) return NULL;
	out->backbuffer = malloc(GFX_S(out) * GFX_H(out));
	return out;
}

gfx_context_t * init_graphics_subregion(gfx_context_t * base, int x, int y, int width, int height) {
	gfx_context_t * out = malloc(sizeof(gfx_context_t));

	out->clips = NULL;
	out->depth = 32;

	out->width = width;
	out->height = height;
	out->stride = base->stride;
	out->backbuffer = base->buffer + (base->stride * y) + x * 4;
	out->buffer = base->buffer + (base->stride * y) + x * 4;

	if (base->clips) {
		for (int _y = 0; _y < height; ++_y) {
			if (_is_in_clip(base, y + _y)) {
				gfx_add_clip(out,0,_y,width,1);
			}
		}
	}

	out->size = 0; /* don't allow flip or clear operations */
	return out;
}

void reinit_graphics_fullscreen(gfx_context_t * out) {

	ioctl(framebuffer_fd, IO_VID_WIDTH,  &out->width);
	ioctl(framebuffer_fd, IO_VID_HEIGHT, &out->height);
	ioctl(framebuffer_fd, IO_VID_DEPTH,  &out->depth);
	ioctl(framebuffer_fd, IO_VID_STRIDE, &out->stride);

	out->size   = GFX_H(out) * GFX_S(out);

	if (out->clips && out->clips_size != out->height) {
		free(out->clips);
		out->clips = NULL;
		out->clips_size = 0;
	}

	if (out->buffer != out->backbuffer) {
		ioctl(framebuffer_fd, IO_VID_ADDR,   &out->buffer);
		out->backbuffer = realloc(out->backbuffer, GFX_S(out) * GFX_H(out));
	} else {
		ioctl(framebuffer_fd, IO_VID_ADDR,   &out->buffer);
		out->backbuffer = out->buffer;
	}

}

gfx_context_t * init_graphics_sprite(sprite_t * sprite) {
	gfx_context_t * out = malloc(sizeof(gfx_context_t));
	out->clips = NULL;

	out->width  = sprite->width;
	out->stride = sprite->width * sizeof(uint32_t);
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

inline uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
	return 0xFF000000 | (r << 16) | (g << 8) | (b);
}

inline uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
	return (a << 24U) | (r << 16) | (g << 8) | (b);
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

	r = r * a / 255;
	g = g * a / 255;
	b = b * a / 255;
	return rgba(r,g,b,a);
}

static int clamp(int a, int l, int h) {
	return a < l ? l : (a > h ? h : a);
}

static void _box_blur_horizontal(gfx_context_t * _src, int radius) {
	int w = _src->width;
	int h = _src->height;
	int half_radius = radius / 2;
	uint32_t * out_color = calloc(sizeof(uint32_t), w);

	for (int y = 0; y < h; y++) {
		int hits = 0;
		int r = 0;
		int g = 0;
		int b = 0;
		int a = 0;
		for (int x = -half_radius; x < w; x++) {
			int old_p = x - half_radius - 1;
			if (old_p >= 0)
			{
				uint32_t col = GFX(_src, clamp(old_p,0,w-1), y);
				if (col) {
					r -= _RED(col);
					g -= _GRE(col);
					b -= _BLU(col);
					a -= _ALP(col);
				}
				hits--;
			}

			int newPixel = x + half_radius;
			if (newPixel < w) {
				uint32_t col = GFX(_src, clamp(newPixel,0,w-1), y);
				if (col != 0) {
					r += _RED(col);
					g += _GRE(col);
					b += _BLU(col);
					a += _ALP(col);
				}
				hits++;
			}

			if (x >= 0 && x < w) {
				out_color[x] = rgba(r / hits, g / hits, b / hits, a / hits);
			}
		}

		if (!_is_in_clip(_src, y)) continue;
		for (int x = 0; x < w; x++) {
			GFX(_src,x,y) = out_color[x];
		}
	}

	free(out_color);
}

static void _box_blur_vertical(gfx_context_t * _src, int radius) {
	int w = _src->width;
	int h = _src->height;
	int half_radius = radius / 2;

	uint32_t * out_color = calloc(sizeof(uint32_t), h);

	for (int x = 0; x < w; x++) {
		int hits = 0;
		int r = 0;
		int g = 0;
		int b = 0;
		int a = 0;
		for (int y = -half_radius; y < h; y++) {
			int old_p = y - half_radius - 1;
			if (old_p >= 0) {
				uint32_t col = GFX(_src,x,clamp(old_p,0,h-1));
				if (col != 0) {
					r -= _RED(col);
					g -= _GRE(col);
					b -= _BLU(col);
					a -= _ALP(col);
				}
				hits--;
			}

			int newPixel = y + half_radius;
			if (newPixel < h) {
				uint32_t col = GFX(_src,x,clamp(newPixel,0,h-1));
				if (col != 0)
				{
					r += _RED(col);
					g += _GRE(col);
					b += _BLU(col);
					a += _ALP(col);
				}
				hits++;
			}

			if (y >= 0 && y < h) {
				out_color[y] = rgba(r / hits, g / hits, b / hits, a / hits);
			}
		}

		for (int y = 0; y < h; y++) {
			if (!_is_in_clip(_src, y)) continue;
			GFX(_src,x,y) = out_color[y];
		}
	}

	free(out_color);
}

void blur_context_box(gfx_context_t * _src, int radius) {
	_box_blur_horizontal(_src,radius);
	_box_blur_vertical(_src,radius);
}

static int (*load_sprite_jpg)(sprite_t *, char *) = NULL;
static int (*load_sprite_png)(sprite_t *, char *) = NULL;

/**
 * TODO: This should probably use some config file or plugin path
 *       for better discovery; we could rename these libraries and
 *       not have applications / other libraries depend on them
 *       directly and instead go through libtoaru_graphics.
 */
__attribute__((constructor)) static void _load_format_libraries() {
	void * _lib_jpeg = dlopen("libtoaru_jpeg.so", 0);
	if (_lib_jpeg) load_sprite_jpg = dlsym(_lib_jpeg, "load_sprite_jpg");
	void * _lib_png = dlopen("libtoaru_png.so", 0);
	if (_lib_png) load_sprite_png = dlsym(_lib_png, "load_sprite_png");
}

static char * extension_from_filename(char * filename) {
	char * ext = strrchr(filename, '.');
	if (ext && *ext == '.') return ext + 1;
	return "";
}

int load_sprite(sprite_t * sprite, char * filename) {

	char * ext = extension_from_filename(filename);

	if (!strcmp(ext,"png") || !strcmp(ext,"sdf")) return load_sprite_png(sprite, filename);
	if (!strcmp(ext,"jpg") || !strcmp(ext,"jpeg")) return load_sprite_jpg(sprite, filename);

	/* Fall back to bitmap */
	return load_sprite_bmp(sprite, filename);
}

int load_sprite_bmp(sprite_t * sprite, char * filename) {
	/* Open the requested binary */
	FILE * image = fopen(filename, "r");

	if (!image) return 1;

	size_t image_size= 0;

	fseek(image, 0, SEEK_END);
	image_size = ftell(image);
	fseek(image, 0, SEEK_SET);

	/* Alright, we have the length */
	char * bufferb = malloc(image_size);
	fread(bufferb, image_size, 1, image);

	if (bufferb[0] == 'B' && bufferb[1] == 'M') {
		/* Bitmaps */
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
		sprite->masks = NULL;

		int alpha_after = ((unsigned char *)&bufferi[13])[2] == 0xFF;

		#define _BMP_A 0x1000000
		#define _BMP_R 0x1
		#define _BMP_G 0x100
		#define _BMP_B 0x10000

		if (bpp == 32) {
			sprite->alpha = ALPHA_EMBEDDED;
		}

		for (y = 0; y < height; ++y) {
			for (x = 0; x < width; ++x) {
				if (i > image_size) goto _cleanup_sprite;
				/* Extract the color */
				uint32_t color;
				if (bpp == 24) {
					color =	(bufferb[i   + 3 * x] & 0xFF) +
							(bufferb[i+1 + 3 * x] & 0xFF) * 0x100 +
							(bufferb[i+2 + 3 * x] & 0xFF) * 0x10000 + 0xFF000000;
				} else if (bpp == 32 && alpha_after == 0) {
					color =	(bufferb[i   + 4 * x] & 0xFF) * _BMP_A +
							(bufferb[i+1 + 4 * x] & 0xFF) * _BMP_R +
							(bufferb[i+2 + 4 * x] & 0xFF) * _BMP_G +
							(bufferb[i+3 + 4 * x] & 0xFF) * _BMP_B;
					color = premultiply(color);
				} else if (bpp == 32 && alpha_after == 1) {
					color =	(bufferb[i   + 4 * x] & 0xFF) * _BMP_R +
							(bufferb[i+1 + 4 * x] & 0xFF) * _BMP_G +
							(bufferb[i+2 + 4 * x] & 0xFF) * _BMP_B +
							(bufferb[i+3 + 4 * x] & 0xFF) * _BMP_A;
					color = premultiply(color);
				} else {
					color = rgb(bufferb[i + x],bufferb[i + x],bufferb[i + x]); /* Unsupported */
				}
				/* Set our point */
				sprite->bitmap[(height - y - 1) * width + x] = color;
			}
			i += row_width;
		}
	} else {
		/* Assume targa; limited support */
		struct Header {
			uint8_t id_length;
			uint8_t color_map_type;
			uint8_t image_type;

			uint16_t color_map_first_entry;
			uint16_t color_map_length;
			uint8_t color_map_entry_size;

			uint16_t x_origin;
			uint16_t y_origin;
			uint16_t width;
			uint16_t height;
			uint8_t  depth;
			uint8_t  descriptor;
		} __attribute__((packed));
		struct Header * header = (struct Header *)bufferb;

		if (header->id_length || header->color_map_type || (header->image_type != 2)) {
			/* Unable to parse */
			goto _cleanup_sprite;
		}

		sprite->width = header->width;
		sprite->height = header->height;
		sprite->bitmap = malloc(sizeof(uint32_t) * sprite->width * sprite->height);
		sprite->masks = NULL;

		uint16_t x = 0;
		uint16_t y = 0;

		int i = sizeof(struct Header);
		if (header->depth == 24) {
			for (y = 0; y < sprite->height; ++y) {
				for (x = 0; x < sprite->width; ++x) {
					uint32_t color = rgb(
								bufferb[i+2 + 3 * x],
								bufferb[i+1 + 3 * x],
								bufferb[i   + 3 * x]);
					sprite->bitmap[(sprite->height - y - 1) * sprite->width + x] = color;
				}
				i += sprite->width * 3;
			}
		} else if (header->depth == 32) {
			for (y = 0; y < sprite->height; ++y) {
				for (x = 0; x < sprite->width; ++x) {
					uint32_t color = rgba(
								bufferb[i+2 + 4 * x],
								bufferb[i+1 + 4 * x],
								bufferb[i   + 4 * x],
								bufferb[i+3 + 4 * x]);
					sprite->bitmap[(sprite->height - y - 1) * sprite->width + x] = color;
				}
				i += sprite->width * 4;
			}
		}

	}

_cleanup_sprite:
	fclose(image);
	free(bufferb);
	return 0;
}

#ifndef NO_SSE
static __m128i mask00ff;
static __m128i mask0080;
static __m128i mask0101;

__attribute__((constructor)) static void _masks(void) {
	mask00ff = _mm_set1_epi16(0x00FF);
	mask0080 = _mm_set1_epi16(0x0080);
	mask0101 = _mm_set1_epi16(0x0101);
}
#endif

__attribute__((__force_align_arg_pointer__))
void draw_sprite(gfx_context_t * ctx, sprite_t * sprite, int32_t x, int32_t y) {

	int32_t _left   = max(x, 0);
	int32_t _top    = max(y, 0);
	int32_t _right  = min(x + sprite->width,  ctx->width - 1);
	int32_t _bottom = min(y + sprite->height, ctx->height - 1);
	if (sprite->alpha == ALPHA_MASK) {
		for (uint16_t _y = 0; _y < sprite->height; ++_y) {
			if (!_is_in_clip(ctx, y + _y)) continue;
			for (uint16_t _x = 0; _x < sprite->width; ++_x) {
				if (x + _x < _left || x + _x > _right || y + _y < _top || y + _y > _bottom)
					continue;
				GFX(ctx, x + _x, y + _y) = alpha_blend(GFX(ctx, x + _x, y + _y), SPRITE(sprite, _x, _y), SMASKS(sprite, _x, _y));
			}
		}
	} else if (sprite->alpha == ALPHA_EMBEDDED) {
		/* Alpha embedded is the most important step. */
		for (uint16_t _y = 0; _y < sprite->height; ++_y) {
			if (!_is_in_clip(ctx, y + _y)) continue;
#ifdef NO_SSE
			for (uint16_t _x = 0; _x < sprite->width; ++_x) {
				if (x + _x < _left || x + _x > _right || y + _y < _top || y + _y > _bottom)
					continue;
				GFX(ctx, x + _x, y + _y) = alpha_blend_rgba(GFX(ctx, x + _x, y + _y), SPRITE(sprite, _x, _y));
			}
#else
			uint16_t _x = 0;

			/* Ensure alignment */
			for (; _x < sprite->width; ++_x) {
				if (x + _x < _left || x + _x > _right || y + _y < _top || y + _y > _bottom)
					continue;
				if (!((uintptr_t)&GFX(ctx, x + _x, y + _y) & 15))
					break;
				GFX(ctx, x + _x, y + _y) = alpha_blend_rgba(GFX(ctx, x + _x, y + _y), SPRITE(sprite, _x, _y));
			}
			for (; _x < sprite->width - 3; _x += 4) {
				if (x + _x < _left || y + _y < _top || y + _y > _bottom) {
					continue;
				}
				if (x + _x + 3 > _right)
					break;

				__m128i d = _mm_load_si128((void *)&GFX(ctx, x + _x, y + _y));
				__m128i s = _mm_loadu_si128((void *)&SPRITE(sprite, _x, _y));

				__m128i d_l, d_h;
				__m128i s_l, s_h;

				// unpack destination
				d_l = _mm_unpacklo_epi8(d, _mm_setzero_si128());
				d_h = _mm_unpackhi_epi8(d, _mm_setzero_si128());

				// unpack source
				s_l = _mm_unpacklo_epi8(s, _mm_setzero_si128());
				s_h = _mm_unpackhi_epi8(s, _mm_setzero_si128());

				__m128i a_l, a_h;
				__m128i t_l, t_h;

				// extract source alpha RGBA → AAAA
				a_l = _mm_shufflehi_epi16(_mm_shufflelo_epi16(s_l, _MM_SHUFFLE(3,3,3,3)), _MM_SHUFFLE(3,3,3,3));
				a_h = _mm_shufflehi_epi16(_mm_shufflelo_epi16(s_h, _MM_SHUFFLE(3,3,3,3)), _MM_SHUFFLE(3,3,3,3));

				// negate source alpha
				t_l = _mm_xor_si128(a_l, mask00ff);
				t_h = _mm_xor_si128(a_h, mask00ff);

				// apply source alpha to destination
				d_l = _mm_mulhi_epu16(_mm_adds_epu16(_mm_mullo_epi16(d_l,t_l),mask0080),mask0101);
				d_h = _mm_mulhi_epu16(_mm_adds_epu16(_mm_mullo_epi16(d_h,t_h),mask0080),mask0101);

				// combine source and destination
				d_l = _mm_adds_epu8(s_l,d_l);
				d_h = _mm_adds_epu8(s_h,d_h);

				// pack low + high and write back to memory
				_mm_storeu_si128((void*)&GFX(ctx, x + _x, y + _y), _mm_packus_epi16(d_l,d_h));
			}
			for (; _x < sprite->width; ++_x) {
				if (x + _x < _left || x + _x > _right || y + _y < _top || y + _y > _bottom)
					continue;
				GFX(ctx, x + _x, y + _y) = alpha_blend_rgba(GFX(ctx, x + _x, y + _y), SPRITE(sprite, _x, _y));
			}
#endif
		}
	} else if (sprite->alpha == ALPHA_INDEXED) {
		for (uint16_t _y = 0; _y < sprite->height; ++_y) {
			if (!_is_in_clip(ctx, y + _y)) continue;
			for (uint16_t _x = 0; _x < sprite->width; ++_x) {
				if (x + _x < _left || x + _x > _right || y + _y < _top || y + _y > _bottom)
					continue;
				if (SPRITE(sprite, _x, _y) != sprite->blank) {
					GFX(ctx, x + _x, y + _y) = SPRITE(sprite, _x, _y) | 0xFF000000;
				}
			}
		}
	} else if (sprite->alpha == ALPHA_FORCE_SLOW_EMBEDDED) {
		for (uint16_t _y = 0; _y < sprite->height; ++_y) {
			if (!_is_in_clip(ctx, y + _y)) continue;
			for (uint16_t _x = 0; _x < sprite->width; ++_x) {
				if (x + _x < _left || x + _x > _right || y + _y < _top || y + _y > _bottom)
					continue;
#if 1
				GFX(ctx, x + _x, y + _y) = alpha_blend_rgba(GFX(ctx, x + _x, y + _y), SPRITE(sprite, _x, _y));
#else
				GFX(ctx, x + _x, y + _y) = alpha_blend_rgba(rgba(255,255,0,255), SPRITE(sprite, _x, _y));
#endif
			}
		}
	} else {
		for (uint16_t _y = 0; _y < sprite->height; ++_y) {
			if (!_is_in_clip(ctx, y + _y)) continue;
			for (uint16_t _x = 0; _x < sprite->width; ++_x) {
				if (x + _x < _left || x + _x > _right || y + _y < _top || y + _y > _bottom)
					continue;
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
	if (x <= 0) return 0;
	if (y <= 0) return 0;
	double u_ratio = u - x;
	double v_ratio = v - y;
	double u_o = 1 - u_ratio;
	double v_o = 1 - v_ratio;
	int r_ALP = 255;
	if (tex->alpha == ALPHA_MASK) {
		if (x == tex->width - 1 || y == tex->height - 1) return (SPRITE(tex,x,y) | 0xFF000000) & (0xFFFFFF + ((uint32_t)_RED(SMASKS(tex,x,y)) << 24));
		r_ALP = (_RED(SMASKS(tex,x,y)) * u_o + _RED(SMASKS(tex,x+1,y)) * u_ratio) * v_o + (_RED(SMASKS(tex,x,y+1)) * u_o  + _RED(SMASKS(tex,x+1,y+1)) * u_ratio) * v_ratio;
	} else if (tex->alpha == ALPHA_EMBEDDED) {
		if (x == tex->width - 1 || y == tex->height - 1) return (SPRITE(tex,x,y) | 0xFF000000) & (0xFFFFFF + ((uint32_t)_ALP(SPRITE(tex,x,y)) << 24));
		r_ALP = (_ALP(SPRITE(tex,x,y)) * u_o + _ALP(SPRITE(tex,x+1,y)) * u_ratio) * v_o + (_ALP(SPRITE(tex,x,y+1)) * u_o  + _ALP(SPRITE(tex,x+1,y+1)) * u_ratio) * v_ratio;
	}
	if (x == tex->width - 1 || y == tex->height - 1) return SPRITE(tex,x,y);
	int r_RED = (_RED(SPRITE(tex,x,y)) * u_o + _RED(SPRITE(tex,x+1,y)) * u_ratio) * v_o + (_RED(SPRITE(tex,x,y+1)) * u_o  + _RED(SPRITE(tex,x+1,y+1)) * u_ratio) * v_ratio;
	int r_BLU = (_BLU(SPRITE(tex,x,y)) * u_o + _BLU(SPRITE(tex,x+1,y)) * u_ratio) * v_o + (_BLU(SPRITE(tex,x,y+1)) * u_o  + _BLU(SPRITE(tex,x+1,y+1)) * u_ratio) * v_ratio;
	int r_GRE = (_GRE(SPRITE(tex,x,y)) * u_o + _GRE(SPRITE(tex,x+1,y)) * u_ratio) * v_o + (_GRE(SPRITE(tex,x,y+1)) * u_o  + _GRE(SPRITE(tex,x+1,y+1)) * u_ratio) * v_ratio;

	return rgb(r_RED,r_GRE,r_BLU) & (0xFFFFFF + ((uint32_t)r_ALP << 24));
}

void draw_sprite_scaled(gfx_context_t * ctx, sprite_t * sprite, int32_t x, int32_t y, uint16_t width, uint16_t height) {
	int32_t _left   = max(x, 0);
	int32_t _top    = max(y, 0);
	int32_t _right  = min(x + width,  ctx->width - 1);
	int32_t _bottom = min(y + height, ctx->height - 1);
	for (uint16_t _y = 0; _y < height; ++_y) {
		if (!_is_in_clip(ctx, y + _y)) continue;
		for (uint16_t _x = 0; _x < width; ++_x) {
			if (x + _x < _left || x + _x > _right || y + _y < _top || y + _y > _bottom)
				continue;
			if (sprite->alpha > 0) {
				uint32_t n_color = getBilinearFilteredPixelColor(sprite, (double)_x / (double)width, (double)_y/(double)height);
				GFX(ctx, x + _x, y + _y) = alpha_blend_rgba(GFX(ctx, x + _x, y + _y), n_color);
			} else {
				GFX(ctx, x + _x, y + _y) = getBilinearFilteredPixelColor(sprite, (double)_x / (double)width, (double)_y/(double)height);
			}
		}
	}
}

void draw_sprite_alpha(gfx_context_t * ctx, sprite_t * sprite, int32_t x, int32_t y, float alpha) {
	int32_t _left   = max(x, 0);
	int32_t _top    = max(y, 0);
	int32_t _right  = min(x + sprite->width,  ctx->width - 1);
	int32_t _bottom = min(y + sprite->height, ctx->height - 1);
	for (uint16_t _y = 0; _y < sprite->height; ++_y) {
		if (!_is_in_clip(ctx, y + _y)) continue;
		for (uint16_t _x = 0; _x < sprite->width; ++_x) {
			if (x + _x < _left || x + _x > _right || y + _y < _top || y + _y > _bottom)
				continue;
			uint32_t n_color = SPRITE(sprite, _x, _y);
			uint32_t f_color = premultiply((n_color & 0xFFFFFF) | ((uint32_t)(255 * alpha) << 24));
			f_color = (f_color & 0xFFFFFF) | ((uint32_t)(alpha * _ALP(n_color)) << 24);
			GFX(ctx, x + _x, y + _y) = alpha_blend_rgba(GFX(ctx, x + _x, y + _y), f_color);
		}
	}
}

void draw_sprite_alpha_paint(gfx_context_t * ctx, sprite_t * sprite, int32_t x, int32_t y, float alpha, uint32_t c) {
	int32_t _left   = max(x, 0);
	int32_t _top    = max(y, 0);
	int32_t _right  = min(x + sprite->width,  ctx->width - 1);
	int32_t _bottom = min(y + sprite->height, ctx->height - 1);
	for (uint16_t _y = 0; _y < sprite->height; ++_y) {
		if (!_is_in_clip(ctx, y + _y)) continue;
		for (uint16_t _x = 0; _x < sprite->width; ++_x) {
			if (x + _x < _left || x + _x > _right || y + _y < _top || y + _y > _bottom)
				continue;
			uint32_t n_color = SPRITE(sprite, _x, _y);
			uint32_t f_color = rgb(_ALP(n_color) * alpha, 0, 0);
			GFX(ctx, x + _x, y + _y) = alpha_blend(GFX(ctx, x + _x, y + _y), c, f_color);
		}
	}
}

void draw_sprite_scaled_alpha(gfx_context_t * ctx, sprite_t * sprite, int32_t x, int32_t y, uint16_t width, uint16_t height, float alpha) {
	int32_t _left   = max(x, 0);
	int32_t _top    = max(y, 0);
	int32_t _right  = min(x + width,  ctx->width - 1);
	int32_t _bottom = min(y + height, ctx->height - 1);
	for (uint16_t _y = 0; _y < height; ++_y) {
		if (!_is_in_clip(ctx, y + _y)) continue;
		for (uint16_t _x = 0; _x < width; ++_x) {
			if (x + _x < _left || x + _x > _right || y + _y < _top || y + _y > _bottom)
				continue;
			uint32_t n_color = getBilinearFilteredPixelColor(sprite, (double)_x / (double)width, (double)_y/(double)height);
			uint32_t f_color = premultiply((n_color & 0xFFFFFF) | ((uint32_t)(255 * alpha) << 24));
			f_color = (f_color & 0xFFFFFF) | ((uint32_t)(alpha * _ALP(n_color)) << 24);
			GFX(ctx, x + _x, y + _y) = alpha_blend_rgba(GFX(ctx, x + _x, y + _y), f_color);
		}
	}
}


uint32_t interp_colors(uint32_t bottom, uint32_t top, uint8_t interp) {
	uint8_t red = (_RED(bottom) * (255 - interp) + _RED(top) * interp) / 255;
	uint8_t gre = (_GRE(bottom) * (255 - interp) + _GRE(top) * interp) / 255;
	uint8_t blu = (_BLU(bottom) * (255 - interp) + _BLU(top) * interp) / 255;
	uint8_t alp = (_ALP(bottom) * (255 - interp) + _ALP(top) * interp) / 255;
	return rgba(red,gre,blu, alp);
}

void draw_rectangle(gfx_context_t * ctx, int32_t x, int32_t y, uint16_t width, uint16_t height, uint32_t color) {
	int32_t _left   = max(x, 0);
	int32_t _top    = max(y, 0);
	int32_t _right  = min(x + width,  ctx->width - 1);
	int32_t _bottom = min(y + height, ctx->height - 1);
	for (uint16_t _y = 0; _y < height; ++_y) {
		if (!_is_in_clip(ctx, y + _y)) continue;
		for (uint16_t _x = 0; _x < width; ++_x) {
			if (x + _x < _left || x + _x > _right || y + _y < _top || y + _y > _bottom)
				continue;
			GFX(ctx, x + _x, y + _y) = alpha_blend_rgba(GFX(ctx, x + _x, y + _y), color);
		}
	}
}

void draw_rectangle_solid(gfx_context_t * ctx, int32_t x, int32_t y, uint16_t width, uint16_t height, uint32_t color) {
	int32_t _left   = max(x, 0);
	int32_t _top    = max(y, 0);
	int32_t _right  = min(x + width,  ctx->width - 1);
	int32_t _bottom = min(y + height, ctx->height - 1);
	for (uint16_t _y = 0; _y < height; ++_y) {
		if (!_is_in_clip(ctx, y + _y)) continue;
		for (uint16_t _x = 0; _x < width; ++_x) {
			if (x + _x < _left || x + _x > _right || y + _y < _top || y + _y > _bottom)
				continue;
			GFX(ctx, x + _x, y + _y) = color;
		}
	}
}

void draw_rounded_rectangle(gfx_context_t * ctx, int32_t x, int32_t y, uint16_t width, uint16_t height, int radius, uint32_t color) {
	/* Draw a rounded rectangle */

	if (radius > width / 2) {
		radius = width / 2;
	}

	if (radius > height / 2) {
		radius = height / 2;
	}

	uint32_t c = premultiply(color);
	for (int row = y; row < y + height; row++){
		for (int col = x; col < x + width; col++) {
			if ((col < x + radius || col > x + width - radius - 1) &&
				(row < y + radius || row > y + height - radius - 1)) {
				continue;
			}
			GFX(ctx, col, row) = alpha_blend_rgba(GFX(ctx, col, row), c);
		}
	}

	/* draw the actual rounding */
	for (int i = 0; i < radius; ++i) {
		long r2 = radius * radius;
		long i2 = i * i;
		long j2 = r2 - i2;
		double j_max = sqrt((double)j2);

		for (int j = 0; j <= (int)j_max; ++j) {
			int _x = x + width - radius + i;
			int _y = y + height - radius + j;
			int _z = y + radius - j - 1;

			uint32_t c = color;
			if (j == (int)j_max) {
				c = premultiply(rgba(_RED(c),_GRE(c),_BLU(c),(int)((double)_ALP(c) * (j_max - (double)j))));
			} else {
				c = premultiply(c);
			}

			GFX(ctx, _x, _y) = alpha_blend_rgba(GFX(ctx, _x, _y), c);
			GFX(ctx, _x, _z) = alpha_blend_rgba(GFX(ctx, _x, _z), c);
			_x = x + radius - i - 1;
			GFX(ctx, _x, _y) = alpha_blend_rgba(GFX(ctx, _x, _y), c);
			GFX(ctx, _x, _z) = alpha_blend_rgba(GFX(ctx, _x, _z), c);
		}
	}
}

void draw_rounded_rectangle_pattern(gfx_context_t * ctx, int32_t x, int32_t y, uint16_t width, uint16_t height, int radius, uint32_t (*pattern)(int32_t x, int32_t y, double alpha, void * extra), void * extra) {
	/* Draw a rounded rectangle */

	if (radius > width / 2) {
		radius = width / 2;
	}

	if (radius > height / 2) {
		radius = height / 2;
	}

	for (int row = y; row < y + height; row++){
		for (int col = x; col < x + width; col++) {
			if ((col < x + radius || col > x + width - radius - 1) &&
				(row < y + radius || row > y + height - radius - 1)) {
				continue;
			}
			GFX(ctx, col, row) = alpha_blend_rgba(GFX(ctx, col, row), pattern(col,row,1.0,extra));
		}
	}

	/* draw the actual rounding */
	for (int i = 0; i < radius; ++i) {
		long r2 = radius * radius;
		long i2 = i * i;
		long j2 = r2 - i2;
		double j_max = sqrt((double)j2);

		for (int j = 0; j <= (int)j_max; ++j) {
			int _x = x + width - radius + i;
			int _y = y + height - radius + j;
			int _z = y + radius - j - 1;

			double alpha = (j_max - (double)j);
			GFX(ctx, _x, _y) = alpha_blend_rgba(GFX(ctx, _x, _y), pattern(_x,_y,alpha,extra));
			GFX(ctx, _x, _z) = alpha_blend_rgba(GFX(ctx, _x, _z), pattern(_x,_z,alpha,extra));
			_x = x + radius - i - 1;
			GFX(ctx, _x, _y) = alpha_blend_rgba(GFX(ctx, _x, _y), pattern(_x,_y,alpha,extra));
			GFX(ctx, _x, _z) = alpha_blend_rgba(GFX(ctx, _x, _z), pattern(_x,_z,alpha,extra));
		}
	}
}

float gfx_point_distance(struct gfx_point * a, struct gfx_point * b) {
	return sqrt((a->x - b->x) * (a->x - b->x) + (a->y - b->y) * (a->y - b->y));
}

float gfx_point_distance_squared(struct gfx_point * a, struct gfx_point * b) {
	return (a->x - b->x) * (a->x - b->x) + (a->y - b->y) * (a->y - b->y);
}

float gfx_point_dot(struct gfx_point * a, struct gfx_point * b) {
	return (a->x * b->x) + (a->y * b->y);
}

struct gfx_point gfx_point_sub(struct gfx_point * a, struct gfx_point * b) {
	struct gfx_point p = {a->x - b->x, a->y - b->y};
	return p;
}

struct gfx_point gfx_point_add(struct gfx_point * a, struct gfx_point * b) {
	struct gfx_point p = {a->x + b->x, a->y + b->y};
	return p;
}

#define fmax(a,b) ((a) > (b) ? (a) : (b))
#define fmin(a,b) ((a) < (b) ? (a) : (b))

float gfx_line_distance(struct gfx_point * p, struct gfx_point * v, struct gfx_point * w) {
	float lengthlength = gfx_point_distance_squared(v,w);

	if (lengthlength == 0.0) return gfx_point_distance(p, v); /* point */

	struct gfx_point p_v = gfx_point_sub(p,v);
	struct gfx_point w_v = gfx_point_sub(w,v);
	float tmp = gfx_point_dot(&p_v,&w_v) / lengthlength;
	tmp = fmin(1.0,tmp);
	float t = fmax(0.0, tmp);

	w_v.x *= t;
	w_v.y *= t;

	struct gfx_point v_t = gfx_point_add(v, &w_v);
	return gfx_point_distance(p, &v_t);
}

/**
 * This is slow, but it works...
 *
 * Maybe acceptable for baked UI elements?
 */
void draw_line_aa(gfx_context_t * ctx, int x_1, int x_2, int y_1, int y_2, uint32_t color, float thickness) {
	struct gfx_point v = {(float)x_1, (float)y_1};
	struct gfx_point w = {(float)x_2, (float)y_2};

	for (int y = 0; y < ctx->height; ++y) {
		for (int x = 0; x < ctx->width; ++x) {
			struct gfx_point p = {x,y};
			float d = gfx_line_distance(&p,&v,&w);
			if (d < thickness + 0.5) {
				if (d < thickness - 0.5) {
					GFX(ctx,x,y) = color;
				} else {
					uint32_t f_color = rgb(255 * (1.0 - (d - thickness + 0.5)), 0, 0);
					GFX(ctx,x,y) = alpha_blend(GFX(ctx,x,y), color, f_color);
				}
			}
		}
	}
}

static void calc_rotation(double x, double y, double px, double py, double s, double c, double * u, double * v) {
	/* Translate to pivot */
	x -= px;
	y -= py;

	*u = (x * c - y * s) + px;
	*v = (x * s + y * c) + py;
}

void draw_sprite_rotate(gfx_context_t * ctx, sprite_t * sprite, int32_t x, int32_t y, float rotation, float alpha) {

	double originx = (double)sprite->width / 2.0;
	double originy = (double)sprite->height / 2.0;

	/* Calculate corners */
	double ul_x, ul_y;
	double ll_x, ll_y;
	double ur_x, ur_y;
	double lr_x, lr_y;

	double _s = sin(rotation);
	double _c = cos(rotation);
	calc_rotation(-sprite->width/2, -sprite->height/2, 0, 0, _s, _c, &ul_x, &ul_y);
	calc_rotation(-sprite->width/2, sprite->height/2,  0, 0, _s, _c, &ll_x, &ll_y);
	calc_rotation(sprite->width/2, -sprite->height/2,  0, 0, _s, _c, &ur_x, &ur_y);
	calc_rotation(sprite->width/2, sprite->height/2,   0, 0, _s, _c, &lr_x, &lr_y);

	_s = sin(-rotation);
	_c = cos(-rotation);

	/* Calculate bounds */
	int32_t _left   = min(min(ul_x, ll_x), min(ur_x, lr_x));
	int32_t _top    = min(min(ul_y, ll_y), min(ur_y, lr_y));
	int32_t _right  = max(max(ul_x, ll_x), max(ur_x, lr_x));
	int32_t _bottom = max(max(ul_y, ll_y), max(ur_y, lr_y));

	for (int32_t _y = _top; _y < _bottom; ++_y) {
		if (_y + y < 0) continue;
		if (_y + y  >= ctx->height) break;
		if (!_is_in_clip(ctx, y + _y)) continue;
		for (int32_t _x = _left; _x < _right; ++_x) {
			if (_x + x < 0) continue;
			if (_x + x >= ctx->width) break;
			double u, v;
			calc_rotation(_x + originx, _y + originy, originx, originy, _s, _c, &u, &v);
			uint32_t n_color = getBilinearFilteredPixelColor(sprite, u / (double)sprite->width, v/(double)sprite->height);
			uint32_t f_color = premultiply((n_color & 0xFFFFFF) | ((uint32_t)(255 * alpha) << 24));
			f_color = (f_color & 0xFFFFFF) | ((uint32_t)(alpha * _ALP(n_color)) << 24);
			GFX(ctx, x + _x, y + _y) = alpha_blend_rgba(GFX(ctx, x + _x, y + _y), f_color);
		}
	}
}

uint32_t gfx_vertical_gradient_pattern(int32_t x, int32_t y, double alpha, void * extra) {
	struct gradient_definition * gradient = extra;
	int base_r = _RED(gradient->top), base_g = _GRE(gradient->top), base_b = _BLU(gradient->top);
	int last_r = _RED(gradient->bottom), last_g = _GRE(gradient->bottom), last_b = _BLU(gradient->bottom);
	double gradpoint = (double)(y - (gradient->y)) / (double)gradient->height;

	if (alpha > 1.0) alpha = 1.0;
	if (alpha < 0.0) alpha = 0.0;

	return premultiply(rgba(
		base_r * (1.0 - gradpoint) + last_r * (gradpoint),
		base_g * (1.0 - gradpoint) + last_g * (gradpoint),
		base_b * (1.0 - gradpoint) + last_b * (gradpoint),
		alpha * 255));
}

