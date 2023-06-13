/**
 * @brief Generic Graphics library for ToaruOS
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2012-2021 K. Lange
 */
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <fcntl.h>
#include <dlfcn.h>

#include <sys/ioctl.h>

#if !defined(NO_SSE) && defined(__x86_64__)
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

#define fmax(a,b) ((a) > (b) ? (a) : (b))
#define fmin(a,b) ((a) < (b) ? (a) : (b))

static inline int _is_in_clip(gfx_context_t * ctx, int32_t y) {
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

void gfx_flip_24bit(gfx_context_t * ctx) {
	for (size_t y = 0; y < ctx->height; ++y) {
		if (_is_in_clip(ctx,y)) {
			for (size_t x = 0; x < ctx->width; ++x) {
				((uint8_t*)ctx->buffer)[y * ctx->_true_stride + x * 3] = ((uint8_t*)ctx->backbuffer)[y * ctx->stride + x * 4];
				((uint8_t*)ctx->buffer)[y * ctx->_true_stride + x * 3+1] = ((uint8_t*)ctx->backbuffer)[y * ctx->stride + x * 4+1];
				((uint8_t*)ctx->buffer)[y * ctx->_true_stride + x * 3+2] = ((uint8_t*)ctx->backbuffer)[y * ctx->stride + x * 4+2];
			}
		}
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
	out->buffer = NULL;

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

	if (out->depth == 24) {
		out->depth = 32;
		out->_true_stride = out->stride;
		out->stride = 4 * GFX_W(out);
		out->size = 0;
	}

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
	out->backbuffer = base->backbuffer + (base->stride * y) + x * 4;
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

inline uint32_t alpha_blend_rgba(uint32_t bottom, uint32_t top) {
	if (_ALP(bottom) == 0) return top;
	if (_ALP(top) == 255) return top;
	if (_ALP(top) == 0) return bottom;
	uint8_t a = _ALP(top);
	uint16_t t = 0xFF ^ a;
	uint8_t d_r = _RED(top) + (((uint32_t)(_RED(bottom) * t + 0x80) * 0x101) >> 16UL);
	uint8_t d_g = _GRE(top) + (((uint32_t)(_GRE(bottom) * t + 0x80) * 0x101) >> 16UL);
	uint8_t d_b = _BLU(top) + (((uint32_t)(_BLU(bottom) * t + 0x80) * 0x101) >> 16UL);
	uint8_t d_a = _ALP(top) + (((uint32_t)(_ALP(bottom) * t + 0x80) * 0x101) >> 16UL);
	return rgba(d_r, d_g, d_b, d_a);
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

static inline int clamp(int a, int l, int h) {
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

void blur_from_into(gfx_context_t * _src, gfx_context_t * _dest, int radius) {

	draw_fill(_dest, rgb(255,0,0));

}

static int (*load_sprite_jpg)(sprite_t *, const char *) = NULL;
static int (*load_sprite_png)(sprite_t *, const char *) = NULL;

static void _load_format_libraries() {
	void * _lib_jpeg = dlopen("libtoaru_jpeg.so", 0);
	if (_lib_jpeg) load_sprite_jpg = dlsym(_lib_jpeg, "load_sprite_jpg");
	void * _lib_png = dlopen("libtoaru_png.so", 0);
	if (_lib_png) load_sprite_png = dlsym(_lib_png, "load_sprite_png");
}

static const char * extension_from_filename(const char * filename) {
	const char * ext = strrchr(filename, '.');
	if (ext && *ext == '.') return ext + 1;
	return "";
}

int load_sprite(sprite_t * sprite, const char * filename) {
	static int librariesLoaded = 0;
	if (!librariesLoaded) {
		_load_format_libraries();
		librariesLoaded = 1;
	}

	const char * ext = extension_from_filename(filename);

	if (!strcmp(ext,"png") || !strcmp(ext,"sdf")) return load_sprite_png ? load_sprite_png(sprite, filename) : 1;
	if (!strcmp(ext,"jpg") || !strcmp(ext,"jpeg")) return load_sprite_jpg ? load_sprite_jpg(sprite, filename) : 1;

	/* Fall back to bitmap */
	return load_sprite_bmp(sprite, filename);
}

int load_sprite_bmp(sprite_t * sprite, const char * filename) {
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

#if !defined(NO_SSE) && defined(__x86_64__)
static __m128i mask00ff;
static __m128i mask0080;
static __m128i mask0101;

__attribute__((constructor)) static void _masks(void) {
	mask00ff = _mm_set1_epi16(0x00FF);
	mask0080 = _mm_set1_epi16(0x0080);
	mask0101 = _mm_set1_epi16(0x0101);
}

__attribute__((__force_align_arg_pointer__))
#endif
void draw_sprite(gfx_context_t * ctx, const sprite_t * sprite, int32_t x, int32_t y) {

	int32_t _left   = max(x, 0);
	int32_t _top    = max(y, 0);
	int32_t _right  = min(x + sprite->width,  ctx->width - 1);
	int32_t _bottom = min(y + sprite->height, ctx->height - 1);
	if (sprite->alpha == ALPHA_EMBEDDED) {
		/* Alpha embedded is the most important step. */
		for (uint16_t _y = 0; _y < sprite->height; ++_y) {
			if (y + _y < _top) continue;
			if (y + _y > _bottom) break;
			if (!_is_in_clip(ctx, y + _y)) continue;
#if defined(NO_SSE) || !defined(__x86_64__)
			for (uint16_t _x = 0; _x < sprite->width; ++_x) {
				if (x + _x < _left || x + _x > _right || y + _y < _top || y + _y > _bottom)
					continue;
				GFX(ctx, x + _x, y + _y) = alpha_blend_rgba(GFX(ctx, x + _x, y + _y), SPRITE(sprite, _x, _y));
			}
#else
			uint16_t _x = (x < _left) ? _left - x : 0;

			/* Ensure alignment */
			for (; _x < sprite->width && x + _x <= _right; ++_x) {
				if (!((uintptr_t)&GFX(ctx, x + _x, y + _y) & 15)) break;
				GFX(ctx, x + _x, y + _y) = alpha_blend_rgba(GFX(ctx, x + _x, y + _y), SPRITE(sprite, _x, _y));
			}
			for (; _x < sprite->width - 3 && x + _x + 3 <= _right; _x += 4) {
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
			for (; _x < sprite->width && x + _x <= _right; ++_x) {
				GFX(ctx, x + _x, y + _y) = alpha_blend_rgba(GFX(ctx, x + _x, y + _y), SPRITE(sprite, _x, _y));
			}
#endif
		}
	} else if (sprite->alpha == ALPHA_OPAQUE) {
		for (uint16_t _y = 0; _y < sprite->height; ++_y) {
			if (y + _y < _top) continue;
			if (y + _y > _bottom) break;
			if (!_is_in_clip(ctx, y + _y)) continue;
			for (uint16_t _x = (x < _left) ? _left - x : 0; _x < sprite->width && x + _x <= _right; ++_x) {
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

static inline int out_of_bounds(const sprite_t * tex, int x, int y) {
	return x < 0 || y < 0 || x >= tex->width || y >= tex->height;
}

/**
 * @brief Use bilinear interpolation to get a blended color at the point u,v
 */
#if 1
static inline uint32_t linear_interp(uint32_t left, uint32_t right, uint16_t pr) {
	uint16_t pl = 0xFF ^ pr;
	uint8_t d_r = (((uint32_t)(_RED(right) * pr + 0x80) * 0x101) >> 16UL) + (((uint32_t)(_RED(left) * pl + 0x80) * 0x101) >> 16UL);
	uint8_t d_g = (((uint32_t)(_GRE(right) * pr + 0x80) * 0x101) >> 16UL) + (((uint32_t)(_GRE(left) * pl + 0x80) * 0x101) >> 16UL);
	uint8_t d_b = (((uint32_t)(_BLU(right) * pr + 0x80) * 0x101) >> 16UL) + (((uint32_t)(_BLU(left) * pl + 0x80) * 0x101) >> 16UL);
	uint8_t d_a = (((uint32_t)(_ALP(right) * pr + 0x80) * 0x101) >> 16UL) + (((uint32_t)(_ALP(left) * pl + 0x80) * 0x101) >> 16UL);
	return rgba(d_r, d_g, d_b, d_a);
}

__attribute__((hot))
static inline uint32_t gfx_bilinear_interpolation(const sprite_t * tex, double u, double v) {
	int x = (int)(u + 2.0) - 2;
	int y = (int)(v + 2.0) - 2;
	uint32_t ul = out_of_bounds(tex,x,y)     ? 0 : SPRITE(tex,x,y);
	uint32_t ur = out_of_bounds(tex,x+1,y)   ? 0 : SPRITE(tex,x+1,y);
	uint32_t ll = out_of_bounds(tex,x,y+1)   ? 0 : SPRITE(tex,x,y+1);
	uint32_t lr = out_of_bounds(tex,x+1,y+1) ? 0 : SPRITE(tex,x+1,y+1);
	if ((ul | ur | ll | lr) == 0) return 0;
	uint8_t u_ratio = (u - x) * 0xFF;
	uint8_t v_ratio = (v - y) * 0xFF;
	uint32_t top = linear_interp(ul,ur,u_ratio);
	uint32_t bot = linear_interp(ll,lr,u_ratio);
	return linear_interp(top,bot,v_ratio);
}
#else
static uint32_t gfx_bilinear_interpolation(const sprite_t * tex, double u, double v) {
	return out_of_bounds(tex,u,v) ? 0 : SPRITE(tex,(unsigned int)u,(unsigned int)v);
}
#endif

static inline void apply_alpha_vector(uint32_t * pixels, size_t width, uint8_t alpha) {
	size_t i = 0;
#if !defined(NO_SSE) && defined(__x86_64__)
	__m128i alp = _mm_set_epi16(alpha,alpha,alpha,alpha,alpha,alpha,alpha,alpha);
	while (i + 3 < width) {
		__m128i p = _mm_load_si128((void*)&pixels[i]);
		__m128i d_l, d_h;

		d_l = _mm_mulhi_epu16(_mm_adds_epu16(_mm_mullo_epi16(_mm_unpacklo_epi8(p, _mm_setzero_si128()),alp),mask0080),mask0101);
		d_h = _mm_mulhi_epu16(_mm_adds_epu16(_mm_mullo_epi16(_mm_unpackhi_epi8(p, _mm_setzero_si128()),alp),mask0080),mask0101);

		_mm_storeu_si128((void*)&pixels[i], _mm_packus_epi16(d_l,d_h));

		i += 4;
	}
#endif
	while (i < width) {
		uint8_t r = _RED(pixels[i]);
		uint8_t g = _GRE(pixels[i]);
		uint8_t b = _BLU(pixels[i]);
		uint8_t a = _ALP(pixels[i]);

		r = (((uint16_t)r * alpha + 0x80) * 0x101) >> 16UL;
		g = (((uint16_t)g * alpha + 0x80) * 0x101) >> 16UL;
		b = (((uint16_t)b * alpha + 0x80) * 0x101) >> 16UL;
		a = (((uint16_t)a * alpha + 0x80) * 0x101) >> 16UL;

		pixels[i] = rgba(r,g,b,a);
		i++;
	}
}

void draw_sprite_alpha(gfx_context_t * ctx, const sprite_t * sprite, int32_t x, int32_t y, float alpha) {
	int32_t _left   = max(x, 0);
	int32_t _top    = max(y, 0);
	int32_t _right  = min(x + sprite->width,  ctx->width);
	int32_t _bottom = min(y + sprite->height, ctx->height);
	sprite_t * scanline = create_sprite(_right - _left, 1, ALPHA_EMBEDDED);
	uint8_t alp = alpha * 255;

	for (uint16_t _y = 0; _y < sprite->height; ++_y) {
		if (y + _y < _top) continue;
		if (y + _y >= _bottom) break;
		if (!_is_in_clip(ctx, y + _y)) continue;
		for (uint16_t _x = (x < _left) ? _left - x : 0; _x < sprite->width && x + _x < _right; ++_x) {
			SPRITE(scanline,_x + x - _left,0) = SPRITE(sprite, _x, _y);
		}
		apply_alpha_vector(scanline->bitmap, scanline->width, alp);
		draw_sprite(ctx,scanline,_left,y + _y);
	}

	sprite_free(scanline);
}

void draw_sprite_alpha_paint(gfx_context_t * ctx, const sprite_t * sprite, int32_t x, int32_t y, float alpha, uint32_t c) {
	int32_t _left   = max(x, 0);
	int32_t _top    = max(y, 0);
	int32_t _right  = min(x + sprite->width,  ctx->width);
	int32_t _bottom = min(y + sprite->height, ctx->height);
	for (uint16_t _y = 0; _y < sprite->height; ++_y) {
		if (y + _y < _top) continue;
		if (y + _y >= _bottom) break;
		if (!_is_in_clip(ctx, y + _y)) continue;
		for (uint16_t _x = (x < _left) ? _left - x : 0; _x < sprite->width && x + _x < _right; ++_x) {
			/* Get the alpha from the sprite at this pixel */
			float n_alpha = alpha * ((float)_ALP(SPRITE(sprite, _x, _y)) / 255.0);
			uint32_t f_color = premultiply((c & 0xFFFFFF) | ((uint32_t)(255 * n_alpha) << 24));
			f_color = (f_color & 0xFFFFFF) | ((uint32_t)(n_alpha * _ALP(c)) << 24);
			GFX(ctx, x + _x, y + _y) = alpha_blend_rgba(GFX(ctx, x + _x, y + _y), f_color);
		}
	}
}

static void apply_matrix(double x, double y, gfx_matrix_t matrix, double *out_x, double *out_y) {
	*out_x = matrix[0][0] * x + matrix[0][1] * y + matrix[0][2];
	*out_y = matrix[1][0] * x + matrix[1][1] * y + matrix[1][2];
}

void gfx_apply_matrix(double x, double y, gfx_matrix_t matrix, double *out_x, double *out_y) {
	apply_matrix(x,y,matrix,out_x,out_y);
}

static void multiply_matrix(gfx_matrix_t x, gfx_matrix_t y) {
	double a = x[0][0];
	double b = x[0][1];
	double c = x[0][2];
	double d = x[1][0];
	double e = x[1][1];
	double f = x[1][2];

	double g = y[0][0];
	double h = y[0][1];
	double i = y[0][2];
	double j = y[1][0];
	double k = y[1][1];
	double l = y[1][2];

	x[0][0] = a * g + b * j;
	x[0][1] = a * h + b * k;
	x[0][2] = a * i + b * l + c;

	x[1][0] = d * g + e * j;
	x[1][1] = d * h + e * k;
	x[1][2] = d * i + e * l + f;
}

void gfx_matrix_identity(gfx_matrix_t matrix) {
	matrix[0][0] = 1;
	matrix[0][1] = 0;
	matrix[0][2] = 0;
	matrix[1][0] = 0;
	matrix[1][1] = 1;
	matrix[1][2] = 0;
}

void gfx_matrix_scale(gfx_matrix_t matrix, double x, double y) {
	multiply_matrix(matrix, (gfx_matrix_t){
		{x, 0.0, 0.0},
		{0.0, y, 0.0},
	});
}

void gfx_matrix_shear(gfx_matrix_t matrix, double x, double y) {
	multiply_matrix(matrix, (gfx_matrix_t){
		{1.0, x, 0.0},
		{y, 1.0, 0.0},
	});
}

void gfx_matrix_rotate(gfx_matrix_t matrix, double r) {
	multiply_matrix(matrix, (gfx_matrix_t){
		{ cos(r), -sin(r), 0.0},
		{ sin(r),  cos(r), 0.0},
	});
}

void gfx_matrix_translate(gfx_matrix_t matrix, double x, double y) {
	multiply_matrix(matrix, (gfx_matrix_t){
		{ 1.0, 0.0, x },
		{ 0.0, 1.0, y },
	});
}

static double matrix_det(gfx_matrix_t matrix) {
	double a = matrix[0][0];
	double b = matrix[0][1];
	double d = matrix[1][0];
	double e = matrix[1][1];
	return a * e - b * d;
}

int gfx_matrix_invert(gfx_matrix_t m, gfx_matrix_t inverse) {

	double det = matrix_det(m);
	if (det == 0.0) return 1;

	double a = m[0][0];
	double b = m[0][1];
	double c = m[1][0];
	double d = m[1][1];

	double tx = m[0][2];
	double ty = m[1][2];

	inverse[0][0] = d * (1.0 / det);
	inverse[0][1] = -b * (1.0 / det);
	inverse[1][0] = -c * (1.0 / det);
	inverse[1][1] = a * (1.0 / det);

	inverse[0][2] = (b * ty - d * tx) / det;
	inverse[1][2] = (c * tx - a * ty) / det;

	return 0;
}

/**
 * @brief Draw a sprite into a context, applying a transformation matrix.
 *
 * Uses the affine transformaton matrix @p matrix to draw @p sprite into @p ctx.
 */
void draw_sprite_transform(gfx_context_t * ctx, const sprite_t * sprite, gfx_matrix_t matrix, float alpha) {
	double inverse[2][3];

	/* Calculate the inverse matrix for use in calculating sprite
	 * coordinate from screen coordinate. */
	gfx_matrix_invert(matrix, inverse);

	/* Use primary matrix to obtain corners of the transformed
	 * sprite in screen coordinates. */
	double ul_x, ul_y;
	double ll_x, ll_y;
	double ur_x, ur_y;
	double lr_x, lr_y;

	apply_matrix(0, 0, matrix, &ul_x, &ul_y);
	apply_matrix(0, sprite->height,  matrix, &ll_x, &ll_y);
	apply_matrix(sprite->width, 0,  matrix, &ur_x, &ur_y);
	apply_matrix(sprite->width, sprite->height,   matrix, &lr_x, &lr_y);

	/* Use the corners to calculate bounds within the target context. */
	int32_t _left   = clamp(fmin(fmin(ul_x, ll_x), fmin(ur_x, lr_x)), 0, ctx->width);
	int32_t _top    = clamp(fmin(fmin(ul_y, ll_y), fmin(ur_y, lr_y)), 0, ctx->height);
	int32_t _right  = clamp(fmax(fmax(ul_x+2, ll_x+2), fmax(ur_x+2, lr_x+2)), 0, ctx->width);
	int32_t _bottom = clamp(fmax(fmax(ul_y+2, ll_y+2), fmax(ur_y+2, lr_y+2)), 0, ctx->height);

	sprite_t * scanline = create_sprite(_right - _left, 1, ALPHA_EMBEDDED);
	uint8_t alp = alpha * 255;

	double filter_x, filter_y, filter_dxx, filter_dxy, filter_dyx, filter_dyy;
	gfx_apply_matrix(_left, _top, inverse, &filter_x, &filter_y);
	gfx_apply_matrix(_left+1, _top, inverse, &filter_dxx, &filter_dxy);
	filter_dxx -= filter_x;
	filter_dxy -= filter_y;
	gfx_apply_matrix(_left, _top+1, inverse, &filter_dyx, &filter_dyy);
	filter_dyx -= filter_x;
	filter_dyy -= filter_y;

	for (int32_t _y = _top; _y < _bottom; ++_y) {
		float u = filter_x;
		float v = filter_y;
		filter_x += filter_dyx;
		filter_y += filter_dyy;
		if (!_is_in_clip(ctx, _y)) continue;
		for (int32_t _x = _left; _x < _right; ++_x) {
			SPRITE(scanline,_x - _left,0) = gfx_bilinear_interpolation(sprite, u, v);
			u += filter_dxx;
			v += filter_dxy;
		}
		apply_alpha_vector(scanline->bitmap, scanline->width, alp);
		draw_sprite(ctx,scanline,_left,_y);
	}

	sprite_free(scanline);
}

void draw_sprite_transform_blur(gfx_context_t * ctx, gfx_context_t * blur_ctx, const sprite_t * sprite, gfx_matrix_t matrix, float alpha, uint8_t threshold) {
	double inverse[2][3];

	/* Calculate the inverse matrix for use in calculating sprite
	 * coordinate from screen coordinate. */
	gfx_matrix_invert(matrix, inverse);

	/* Use primary matrix to obtain corners of the transformed
	 * sprite in screen coordinates. */
	double ul_x, ul_y;
	double ll_x, ll_y;
	double ur_x, ur_y;
	double lr_x, lr_y;

	apply_matrix(0, 0, matrix, &ul_x, &ul_y);
	apply_matrix(0, sprite->height,  matrix, &ll_x, &ll_y);
	apply_matrix(sprite->width, 0,  matrix, &ur_x, &ur_y);
	apply_matrix(sprite->width, sprite->height,   matrix, &lr_x, &lr_y);

	/* Use the corners to calculate bounds within the target context. */
	int32_t _left   = clamp(fmin(fmin(ul_x, ll_x), fmin(ur_x, lr_x)), 0, ctx->width);
	int32_t _top    = clamp(fmin(fmin(ul_y, ll_y), fmin(ur_y, lr_y)), 0, ctx->height);
	int32_t _right  = clamp(fmax(fmax(ul_x+2, ll_x+2), fmax(ur_x+2, lr_x+2)), 0, ctx->width);
	int32_t _bottom = clamp(fmax(fmax(ul_y+2, ll_y+2), fmax(ur_y+2, lr_y+2)), 0, ctx->height);

	blur_ctx->clips_size = ctx->clips_size;
	blur_ctx->clips = ctx->clips;
	blur_ctx->backbuffer = ctx->backbuffer;
	gfx_context_t * f = init_graphics_subregion(blur_ctx, _left, _top, _right - _left, _bottom - _top);
	flip(f);
	f->backbuffer = f->buffer;
	blur_context_box(f, 10);
	free(f);
	blur_ctx->backbuffer = blur_ctx->buffer;
	blur_ctx->clips_size = 0;
	blur_ctx->clips = NULL;

	sprite_t * scanline = create_sprite(_right - _left, 1, ALPHA_EMBEDDED);
	sprite_t * blurline = create_sprite(_right - _left, 1, ALPHA_EMBEDDED);
	uint8_t alp = alpha * 255;

	double filter_x, filter_y, filter_dxx, filter_dxy, filter_dyx, filter_dyy;
	gfx_apply_matrix(_left, _top, inverse, &filter_x, &filter_y);
	gfx_apply_matrix(_left+1, _top, inverse, &filter_dxx, &filter_dxy);
	filter_dxx -= filter_x;
	filter_dxy -= filter_y;
	gfx_apply_matrix(_left, _top+1, inverse, &filter_dyx, &filter_dyy);
	filter_dyx -= filter_x;
	filter_dyy -= filter_y;

	for (int32_t _y = _top; _y < _bottom; ++_y) {
		float u = filter_x;
		float v = filter_y;
		filter_x += filter_dyx;
		filter_y += filter_dyy;
		if (!_is_in_clip(ctx, _y)) continue;
		for (int32_t _x = _left; _x < _right; ++_x) {
			SPRITE(scanline,_x - _left,0) = gfx_bilinear_interpolation(sprite, u, v);
			SPRITE(blurline,_x - _left,0) = (_ALP(SPRITE(scanline,_x - _left,0)) > threshold) ? GFX(blur_ctx,_x,_y) : 0;
			u += filter_dxx;
			v += filter_dxy;
		}
		apply_alpha_vector(blurline->bitmap, blurline->width, alp);
		apply_alpha_vector(scanline->bitmap, scanline->width, alp);
		draw_sprite(ctx,blurline,_left,_y);
		draw_sprite(ctx,scanline,_left,_y);
	}

	sprite_free(scanline);
	sprite_free(blurline);

}

void draw_sprite_rotate(gfx_context_t * ctx, const sprite_t * sprite, int32_t x, int32_t y, float rotation, float alpha) {
	gfx_matrix_t m;
	gfx_matrix_identity(m);
	gfx_matrix_translate(m, x + sprite->width / 2, y + sprite->height / 2);
	gfx_matrix_rotate(m, rotation);
	gfx_matrix_translate(m, -sprite->width / 2, -sprite->height / 2);
	draw_sprite_transform(ctx,sprite,m,alpha);
}

void draw_sprite_scaled(gfx_context_t * ctx, const sprite_t * sprite, int32_t x, int32_t y, uint16_t width, uint16_t height) {
	gfx_matrix_t m;
	gfx_matrix_identity(m);
	gfx_matrix_translate(m, x, y);
	gfx_matrix_scale(m, (double)width / (double)sprite->width, (double)height / (double)sprite->height);
	draw_sprite_transform(ctx,sprite,m,1.0);
}

void draw_sprite_scaled_alpha(gfx_context_t * ctx, const sprite_t * sprite, int32_t x, int32_t y, uint16_t width, uint16_t height, float alpha) {
	gfx_matrix_t m;
	gfx_matrix_identity(m);
	gfx_matrix_translate(m, x, y);
	gfx_matrix_scale(m, (double)width / (double)sprite->width, (double)height / (double)sprite->height);
	draw_sprite_transform(ctx,sprite,m,alpha);
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

float gfx_point_distance(const struct gfx_point * a, const struct gfx_point * b) {
	return sqrt((a->x - b->x) * (a->x - b->x) + (a->y - b->y) * (a->y - b->y));
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
		if (row < 0) continue;
		if (row >= ctx->height) break;
		for (int col = x; col < x + width; col++) {
			if (col < 0) continue;
			if (col >= ctx->width) break;

			if ((col < x + radius || col > x + width - radius - 1) &&
				(row < y + radius || row > y + height - radius - 1)) {
				continue;
			}
			GFX(ctx, col, row) = alpha_blend_rgba(GFX(ctx, col, row), pattern(col,row,1.0,extra));
		}
	}

	struct gfx_point origin = {0.0,0.0};

	for (int py = 0; py < radius + 1; ++py) {
		for (int px = 0; px < radius + 1; ++px) {
			struct gfx_point this = {px,py};
			float dist = gfx_point_distance(&origin,&this);
			if (dist > (double)radius) continue;
			float alpha = 1.0;
			if (dist > (double)(radius-1)) {
				alpha = 1.0 - (dist - (double)(radius-1));
			}
			int _x = clamp(x + width - radius + px, 0, ctx->width-1);
			int _y = clamp(y + height - radius + py, 0, ctx->height-1);
			int _z = clamp(y + radius - py - 1, 0, ctx->height-1);
			GFX(ctx, _x, _y) = alpha_blend_rgba(GFX(ctx, _x, _y), pattern(_x,_y,alpha,extra));
			GFX(ctx, _x, _z) = alpha_blend_rgba(GFX(ctx, _x, _z), pattern(_x,_z,alpha,extra));
			_x = clamp(x + radius - px - 1, 0, ctx->width-1);
			GFX(ctx, _x, _y) = alpha_blend_rgba(GFX(ctx, _x, _y), pattern(_x,_y,alpha,extra));
			GFX(ctx, _x, _z) = alpha_blend_rgba(GFX(ctx, _x, _z), pattern(_x,_z,alpha,extra));
		}
	}
}

uint32_t gfx_fill_pattern(int32_t x, int32_t y, double alpha, void * extra) {
	if (alpha > 1.0) alpha = 1.0;
	if (alpha < 0.0) alpha = 0.0;
	uint32_t c = *(uint32_t*)extra;
	return premultiply(rgba(_RED(c),_GRE(c),_BLU(c),(int)((double)_ALP(c) * alpha)));
}

void draw_rounded_rectangle(gfx_context_t * ctx, int32_t x, int32_t y, uint16_t width, uint16_t height, int radius, uint32_t color) {
	draw_rounded_rectangle_pattern(ctx,x,y,width,height,radius,gfx_fill_pattern,&color);
}

float gfx_point_distance_squared(const struct gfx_point * a, const struct gfx_point * b) {
	return (a->x - b->x) * (a->x - b->x) + (a->y - b->y) * (a->y - b->y);
}

float gfx_point_dot(const struct gfx_point * a, const struct gfx_point * b) {
	return (a->x * b->x) + (a->y * b->y);
}

struct gfx_point gfx_point_sub(const struct gfx_point * a, const struct gfx_point * b) {
	struct gfx_point p = {a->x - b->x, a->y - b->y};
	return p;
}

struct gfx_point gfx_point_add(const struct gfx_point * a, const struct gfx_point * b) {
	struct gfx_point p = {a->x + b->x, a->y + b->y};
	return p;
}

float gfx_line_distance(const struct gfx_point * p, const struct gfx_point * v, const struct gfx_point * w) {
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
void draw_line_aa_points(gfx_context_t * ctx, struct gfx_point *v, struct gfx_point *w, uint32_t color, float thickness) {

	/* Calculate viable bounds */
	int x_0 = max(min(v->x - thickness - 1, w->x - thickness - 1), 0);
	int x_1 = min(max(v->x + thickness + 1, w->x + thickness + 1), ctx->width);
	int y_0 = max(min(v->y - thickness - 1, w->y - thickness - 1), 0);
	int y_1 = min(max(v->y + thickness + 1, w->y + thickness + 1), ctx->height);

	for (int y = y_0; y < y_1; ++y) {
		for (int x = x_0; x < x_1; ++x) {
			struct gfx_point p = {x,y};
			float d = gfx_line_distance(&p,v,w);
			if (d < thickness + 0.5) {
				if (d < thickness - 0.5) {
					GFX(ctx,x,y) = alpha_blend_rgba(GFX(ctx,x,y), color);
				} else {
					float alpha = 1.0 - (d - thickness + 0.5);
					GFX(ctx,x,y) = alpha_blend_rgba(GFX(ctx,x,y), premultiply(rgba(_RED(color),_GRE(color),_BLU(color),(int)((double)_ALP(color) * alpha))));
				}
			}
		}
	}
}

void draw_line_aa(gfx_context_t * ctx, int x_1, int x_2, int y_1, int y_2, uint32_t color, float thickness) {
	struct gfx_point v = {(float)x_1, (float)y_1};
	struct gfx_point w = {(float)x_2, (float)y_2};
	draw_line_aa_points(ctx,&v,&w,color,thickness);
}

