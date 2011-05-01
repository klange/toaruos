/*
 * The ToAru Sample Game
 */

#include <stdio.h>
#include <stdint.h>
#include <syscall.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

DEFN_SYSCALL0(getgraphicsaddress, 11);
DEFN_SYSCALL1(kbd_mode, 12, int);
DEFN_SYSCALL0(kbd_get, 13);
DEFN_SYSCALL1(setgraphicsoffset, 16, int);

typedef struct sprite {
	uint16_t width;
	uint16_t height;
	uint32_t * bitmap;
	uint32_t * masks;
	uint32_t blank;
	uint8_t  alpha;
} sprite_t;

#define GFX_W  1024
#define GFX_H  768
#define GFX_B  4
#define GFX(x,y) frame_mem[GFX_W * (y) + (x)]
#define SPRITE(sprite,x,y) sprite->bitmap[sprite->width * (y) + (x)]
#define SMASKS(sprite,x,y) sprite->masks[sprite->width * (y) + (x)]

uint32_t * gfx_mem;
uint32_t * frame_mem;
uint32_t   gfx_size = GFX_B * GFX_H * GFX_W;
sprite_t * sprites[128];


void * malloc_(size_t size) {
	void * ret = malloc(size);
	if (!ret) {
		printf("[WARNING!] malloc_(%d) returned NULL!\n", size);
		while ((ret = malloc(size)) == NULL) {
			printf(".");
		}
	}
	return ret;
}


uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
	return (r * 0x10000) + (g * 0x100) + (b * 0x1);
}

void flip() {
#if 1
	static int offset = 768;
	void * tmp = frame_mem;
	frame_mem = gfx_mem;
	gfx_mem = tmp;
	syscall_setgraphicsoffset(offset);
	offset = 768 - offset;
#else
	memcpy(gfx_mem, frame_mem, gfx_size);
#endif
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
	char * bufferb = malloc_(image_size);
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
	sprite->bitmap = malloc_(sizeof(uint32_t) * width * height);

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
			sprite->bitmap[(height - y) * width + x] = color;
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

/* woah */
char font_buffer[400000];
sprite_t alpha_tmp;

void init_sprite(int i, char * filename, char * alpha) {
	sprites[i] = malloc_(sizeof(sprite_t));
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

int main(int argc, char ** argv) {

	gfx_mem = (void *)syscall_getgraphicsaddress();
	frame_mem = (void *)((uintptr_t)gfx_mem + sizeof(uint32_t) * 1024 * 768); //malloc_(sizeof(uint32_t) * 1024 * 768);
	printf("Graphics memory is at %p, backbuffer is at %p.\n", gfx_mem, frame_mem);

	printf("Loading sprites...\n");
	init_sprite(0, "/etc/ball.bmp", NULL);
	init_sprite(1, "/etc/toaru_logo.bmp", "/etc/toaru_logo_a.bmp");

	printf("\033[J\n");

	syscall_kbd_mode(1);

	int playing = 1;

	int obj_x = GFX_W / 2;
	int obj_y = GFX_H - 200;

	int obj_h = 1;
	int obj_v = 1;

	uint32_t which = 0;

	while (playing) {

		/* Update the sprite location */
		obj_x += obj_h;
		obj_y += obj_v;
		if (obj_x < 0) {
			obj_x = 0;
			obj_h = -obj_h;
		}
		if (obj_x > GFX_W - sprites[which]->width) {
			obj_x = GFX_W - sprites[which]->width;
			obj_h = -obj_h;
		}
		if (obj_y < 0) {
			obj_y = 0;
			obj_v = -obj_v;
		}
		if (obj_y > GFX_H - sprites[which]->height) {
			obj_y = GFX_H - sprites[which]->height;
			obj_v = -obj_v;
		}

		draw_sprite(sprites[which], obj_x, obj_y);
		flip();

		char ch = 0;
		ch = syscall_kbd_get();
		switch (ch) {
			case 16:
				playing = 0;
				break;
			case 30:
				obj_h = -1;
				/* left */
				break;
			case 32:
				obj_h = 1;
				/* right */
				break;
			case 18:
				obj_x = GFX_W / 2;
				obj_y = GFX_H - 200;
				obj_v = 1;
				obj_h = 1;
				break;
			case 2:
				which = 0;
				break;
			case 3:
				which = 1;
				break;
			default:
				break;
		}
	}

	syscall_kbd_mode(0);

	return 0;
}
