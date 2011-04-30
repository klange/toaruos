/*
 * The ToAru Sample Game
 */

#include <stdio.h>
#include <stdint.h>
#include <syscall.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#if 1
#include <ft2build.h>
#include FT_FREETYPE_H


FT_Library library;
FT_Face    face;
#endif

DEFN_SYSCALL0(getgraphicsaddress, 11);
DEFN_SYSCALL1(kbd_mode, 12, int);
DEFN_SYSCALL0(kbd_get, 13);

typedef struct sprite {
	uint16_t width;
	uint16_t height;
	uint32_t * bitmap;
	uint32_t blank;
} sprite_t;

#define GFX_W  1024
#define GFX_H  768
#define GFX_B  4
#define GFX(x,y) frame_mem[GFX_W * (y) + (x)]
#define SPRITE(sprite,x,y) sprite->bitmap[sprite->width * (y) + (x)]

uint32_t * gfx_mem;
uint32_t * frame_mem;
uint32_t   gfx_size = GFX_B * GFX_H * GFX_W;
sprite_t * sprites[128];


uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
	return (r * 0x10000) + (g * 0x100) + (b * 0x1);
}

void flip() {
	memcpy(gfx_mem, frame_mem, gfx_size);
	memset(frame_mem, 0, gfx_size);
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
	uint32_t width = bufferi[4];
	uint32_t height = bufferi[5];
	uint32_t row_width = (24 * width + 31) / 32 * 4;
	/* Skip right to the important part */
	size_t i = bufferi[2];

	sprite->width = width;
	sprite->height = height;
	sprite->bitmap = malloc(sizeof(uint32_t) * width * height);
	printf("%d x %d\n", width, height);

	for (y = 0; y < height; ++y) {
		for (x = 0; x < width; ++x) {
			if (i > image_size) return;
			/* Extract the color */
			uint32_t color =	bufferb[i   + 3 * x] +
								bufferb[i+1 + 3 * x] * 0x100 +
								bufferb[i+2 + 3 * x] * 0x10000;
			/* Set our point */
			sprite->bitmap[(height - y) * width + x] = color;
		}
		i += row_width;
	}
	free(bufferb);
}

void draw_sprite(sprite_t * sprite, uint16_t x, uint16_t y) {
	for (uint16_t _y = 0; _y < sprite->height; ++_y) {
		for (uint16_t _x = 0; _x < sprite->width; ++_x) {
			if (SPRITE(sprite,_x,_y) != sprite->blank) {
				GFX(x + _x, y + _y) = SPRITE(sprite, _x, _y);
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


int main(int argc, char ** argv) {
	gfx_mem = (void *)syscall_getgraphicsaddress();
	frame_mem = malloc(sizeof(uint32_t) * 1024 * 768);
	printf("Graphics memory is at %p, backbuffer is at %p.\n", gfx_mem, frame_mem);

	printf("Loading sprites...\n");
	sprites[0] = malloc(sizeof(sprite_t));
	load_sprite(sprites[0], "/bs.bmp");
	sprites[0]->blank = 0x0;
	printf("Sprite is %d by %d\n", sprites[0]->width, sprites[0]->height);
	printf("%x\n", sprites[0]->bitmap);

#if 1
	printf("Initialzing Freetype...\n");
	int error = FT_Init_FreeType(&library);
	if (error) {
		printf("FreeType initialization returned %d.\n");
		return error;
	}
	printf("Loading DejaVu Sans font.\n");
	error = FT_New_Face(library,
			"/etc/DejaVuSansMono.ttf",
			0,
			&face);
	if (error == FT_Err_Unknown_File_Format) {
		printf("FT: Unknown format (%d).\n", FT_Err_Unknown_File_Format);
		return error;
	} else {
		printf("FT: Something else went wrong. (%d)\n", error);
	}
#endif


	printf("\033[J\n");

	syscall_kbd_mode(1);

	int playing = 1;

	while (playing) {
		uint32_t c = 0; //0x72A0CF; /* A nice sky blue */
		for (uint16_t x = 0; x < 1024; ++x) {
			for (uint16_t y = 0; y < 768; ++y) {
				GFX(x,y) = c;
			}
		}
		draw_sprite(sprites[0], rand() % 1000, rand() % 700);
		flip();
		char ch = 0;
		if ((ch = syscall_kbd_get())) {
			switch (ch) {
				case 113:
					playing = 0;
				default:
					break;
			}
		}
	}

	syscall_kbd_mode(0);

	return 0;
}
