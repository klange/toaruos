/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * Mouse driver
 */
#include <system.h>
#include <logging.h>

uint8_t mouse_cycle = 0;
int8_t  mouse_byte[3];
int8_t  mouse_x = 0;
int8_t  mouse_y = 0;

#define MOUSE_SCALE 10;

int32_t actual_x = 5120;
int32_t actual_y = 3835;

extern uint32_t * bochs_vid_memory;

#define GFX_W  1024
#define GFX_H  768
#define GFX_B  4
#define GFX(x,y) bochs_vid_memory[GFX_W * (y + bochs_current_scroll()) + (x)]
#define SPRITE(sprite,x,y) sprite->bitmap[sprite->width * (y) + (x)]
#define SMASKS(sprite,x,y) sprite->masks[sprite->width * (y) + (x)]
#define _RED(color) ((color & 0x00FF0000) / 0x10000)
#define _GRE(color) ((color & 0x0000FF00) / 0x100)
#define _BLU(color) ((color & 0x000000FF) / 0x1)
#define GUARD(x,y) ((x) < 0 || (y) < 0 || (x) >= GFX_W || (y) >= GFX_H)

typedef struct sprite {
	uint16_t width;
	uint16_t height;
	uint32_t * bitmap;
	uint32_t * masks;
	uint32_t blank;
	uint8_t  alpha;
} sprite_t;

sprite_t * cursor;

uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
	return (r * 0x10000) + (g * 0x100) + (b * 0x1);
}

uint32_t alpha_blend(uint32_t bottom, uint32_t top, uint32_t mask) {
	float a = _RED(mask) / 256.0;
	uint8_t red = _RED(bottom) * (1.0 - a) + _RED(top) * a;
	uint8_t gre = _GRE(bottom) * (1.0 - a) + _GRE(top) * a;
	uint8_t blu = _BLU(bottom) * (1.0 - a) + _BLU(top) * a;
	return rgb(red,gre,blu);
}

void draw_sprite(sprite_t * sprite, int16_t x, int16_t y) {
	for (int16_t _y = 0; _y < sprite->height; ++_y) {
		for (int16_t _x = 0; _x < sprite->width; ++_x) {
			if (sprite->alpha) {
				if (SMASKS(sprite,_x,_y) != sprite->blank) {
					if (!GUARD(x + _x, y + _y))
						GFX(x + _x, y + _y) = alpha_blend(GFX(x + _x, y + _y), SPRITE(sprite, _x, _y), SMASKS(sprite, _x, _y));
				}
			} else {
				if (SPRITE(sprite,_x,_y) != sprite->blank) {
					if (!GUARD(x + _x, y + _y))
						GFX(x + _x, y + _y) = SPRITE(sprite, _x, _y);
				}
			}
		}
	}
}

void load_sprite(sprite_t * sprite, char * filename) {
	/* Open the requested binary */


	fs_node_t * image = kopen(filename, 0);
	size_t image_size= 0;

	image_size = image->length;

	/* Alright, we have the length */
	char * bufferb = malloc(image_size);
	read_fs(image, 0, image_size, (uint8_t *)bufferb);
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
			sprite->bitmap[(height - y - 1) * width + x] = color;
		}
		i += row_width;
	}
	free(bufferb);
}

void init_cursor(char * filename, char * alpha) {
	cursor = malloc(sizeof(sprite_t));
	load_sprite(cursor, filename);
	sprite_t alpha_tmp;
	if (alpha) {
		cursor->alpha = 1;
		load_sprite(&alpha_tmp, alpha);
		cursor->masks = alpha_tmp.bitmap;
	} else {
		cursor->alpha = 0;
	}
	cursor->blank = 0x0;
}

void mouse_handler(struct regs *r) {
	IRQ_OFF;
	switch (mouse_cycle) {
		case 0:
			mouse_byte[0] = inportb(0x60);
			++mouse_cycle;
			break;
		case 1:
			mouse_byte[1] = inportb(0x60);
			++mouse_cycle;
			break;
		case 2:
			mouse_byte[2] = inportb(0x60);
			mouse_x = mouse_byte[1];
			mouse_y = mouse_byte[2];
			mouse_cycle = 0;
			uint32_t previous_x = actual_x;
			uint32_t previous_y = actual_y;
			actual_x = actual_x + mouse_x * MOUSE_SCALE;
			actual_y = actual_y + mouse_y * MOUSE_SCALE;
			if (actual_x < 0) actual_x = 0;
			if (actual_x > 10230) actual_x = 10230;
			if (actual_y < 0) actual_y = 0;
			if (actual_y > 7670) actual_y = 7670;
			short c_x = (short)(previous_x / 10 / 8);
			short c_y = (short)((7670 - previous_y) / 10 / 12);
			for (short i = c_x - 2; i < c_x + 3; ++i) {
				for (short j = c_y - 2; j < c_y + 3; ++j) {
					bochs_redraw_cell(i,j);
				}
			}
			draw_sprite(cursor, actual_x / 10 - 24, 767 - actual_y / 10 - 24);
			break;
	}
	IRQ_ON;
}

void mouse_wait(uint8_t a_type) {
	uint32_t timeout = 100000;
	if (!a_type) {
		while (--timeout) {
			if ((inportb(0x64) & 0x01) == 1) {
				return;
			}
		}
		return;
	} else {
		while (--timeout) {
			if (!((inportb(0x64) & 0x02))) {
				return;
			}
		}
		return;
	}
}

void mouse_write(uint8_t write) {
	mouse_wait(1);
	outportb(0x64, 0xD4);
	mouse_wait(1);
	outportb(0x60, write);
}

uint8_t mouse_read() {
	mouse_wait(0);
	char t = inportb(0x60);
	return t;
}

void mouse_install() {
	LOG(INFO, "Initializing mouse cursor driver");
	uint8_t status;
	IRQ_OFF;
	mouse_wait(1);
	outportb(0x64,0xA8);
	mouse_wait(1);
	outportb(0x64,0x20);
	mouse_wait(0);
	status = inportb(0x60) | 2;
	mouse_wait(1);
	outportb(0x64, 0x60);
	mouse_wait(1);
	outportb(0x60, status);
	mouse_write(0xF6);
	mouse_read();
	mouse_write(0xF4);
	mouse_read();
	IRQ_ON;
	init_cursor("/usr/share/arrow.bmp", "/usr/share/arrow_alpha.bmp");
	irq_install_handler(12, mouse_handler);
}
