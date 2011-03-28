/*
 * vim:tabstop=4
 * vim:noexpandtab
 *
 * Bochs VBE / QEMU vga=std Graphics Driver
 */

#include <system.h>
#include <fs.h>

#define PREFERRED_X 1024
#define PREFERRED_Y 768
#define PREFERRED_B 32

uint16_t bochs_resolution_x = 0;
uint16_t bochs_resolution_y = 0;
uint16_t bochs_resolution_b = 0;
uint16_t bochs_current_bank = 0;

#define BOCHS_BANK_SIZE 16384
#define BOCHS_VID_MEMORY ((uint32_t *)0xA0000)
#define BOCHS_BANKS (bochs_resolution_x * bochs_resolution_y * bochs_resolution_b / (BOCHS_BANK_SIZE * 32))

void
graphics_install_bochs() {
	outports(0x1CE, 0x00);
	uint16_t i = inports(0x1CF);
	if (i < 0xB0C0 || i > 0xB0C6) {
		kprintf("[bochs] You are not a Bochs VBE pseudo-card!\n");
		kprintf("[bochs] 0x%x is totally wrong!\n", (unsigned int)i);
		return;
	}
	kprintf("[bochs] Successfully detected a Bochs VBE setup!\n");
	kprintf("[bochs] You are using QEMU or Bochs and I love you.\n");
	outports(0x1CF, 0xB0C4);
	i = inports(0x1CF);
	kprintf("[bochs] Enabling 1024x768x32 graphics mode!\n");
	/* Disable VBE */
	outports(0x1CE, 0x04);
	outports(0x1CF, 0x00);
	/* Set X resolution to 1024 */
	outports(0x1CE, 0x01);
	outports(0x1CF, PREFERRED_X);
	bochs_resolution_x = PREFERRED_X;
	/* Set Y resolution to 768 */
	outports(0x1CE, 0x02);
	outports(0x1CF, PREFERRED_Y);
	bochs_resolution_y = PREFERRED_Y;
	/* Set bpp to 32 */
	outports(0x1CE, 0x03);
	outports(0x1CF, PREFERRED_B);
	bochs_resolution_b = PREFERRED_B;
	/* Re-enable VBE */
	outports(0x1CE, 0x04);
	outports(0x1CF, 0x01);
}

void
bochs_set_bank(
		uint16_t bank
		) {
	if (bank == bochs_current_bank) {
		/* We are already in this bank, stop wasting cycles */
		return;
	}
	outports(0x1CE, 0x05); /* Bank */
	outports(0x1CF, bank);
	bochs_current_bank = bank;
}

static void
bochs_set_point(
		uint16_t x,
		uint16_t y,
		uint32_t color
		) {
	BOCHS_VID_MEMORY[(y * bochs_resolution_x + x) % BOCHS_BANK_SIZE] = color;
}

void
bochs_set_coord(
		uint16_t x,
		uint16_t y,
		uint32_t color
		) {
	uint32_t location = y * bochs_resolution_x + x;
	bochs_set_bank(location / BOCHS_BANK_SIZE);
	uint32_t offset = location % BOCHS_BANK_SIZE;
	BOCHS_VID_MEMORY[offset] = color;
}

void
bochs_scroll() {
	__asm__ __volatile__ ("cli");
	uint32_t * bank_store = malloc(sizeof(uint32_t) * BOCHS_BANK_SIZE);
	for (int i = 1; i < BOCHS_BANKS; ++i) {
		bochs_set_bank(i);
		memcpy(bank_store, BOCHS_VID_MEMORY, sizeof(uint32_t) * BOCHS_BANK_SIZE);
		bochs_set_bank(i - 1);
		memcpy(BOCHS_VID_MEMORY, bank_store, sizeof(uint32_t) * BOCHS_BANK_SIZE);
	}
	free(bank_store);
	__asm__ __volatile__ ("sti");
}

void
bochs_draw_logo(char * filename) {
	/* This is slow and ineffecient, but it's also dead simple. */
	if (!bochs_resolution_x) { return; }
	fs_node_t * file = kopen(filename,0);
	if (!file) { return; }
	char *bufferb = malloc(file->length);
	/* Read the boot logo */
	size_t bytes_read = read_fs(file, 0, file->length, (uint8_t *)bufferb);
	uint16_t x = 0; /* -> 212 */
	uint16_t y = 0; /* -> 68 */
	/* Get the width / height of the image */
	signed int *bufferi = (signed int *)((uintptr_t)bufferb + 2);
	uint32_t width = bufferi[4];
	uint32_t height = bufferi[5];
	uint32_t row_width = (24 * width + 31) / 32 * 4;
	/* Skip right to the important part */
	size_t i = bufferi[2];
	for (y = 0; y < height; ++y) {
		for (x = 0; x < width; ++x) {
			if (i > bytes_read) return;
			/* Extract the color */
			uint32_t color =	bufferb[i   + 3 * x] +
								bufferb[i+1 + 3 * x] * 0x100 +
								bufferb[i+2 + 3 * x] * 0x10000;
			/* Set our point */
			bochs_set_coord((bochs_resolution_x - width) / 2 + x, (bochs_resolution_y - height) / 2 + (height - y), color);
		}
		i += row_width;
	}
}

void
bochs_fill_rect(
		uint16_t x,
		uint16_t y,
		uint16_t w,
		uint16_t h,
		uint32_t color
		) {
	for (uint16_t i = y; i < h + y; ++i) {
		bochs_set_bank(y * bochs_resolution_x / BOCHS_BANK_SIZE);
		for (uint16_t j = x; j < w + x; ++j) {
			bochs_set_point(j,i,color);
		}
	}
}

void
bochs_write_char(
		uint8_t val,
		uint16_t x,
		uint16_t y,
		uint32_t fg,
		uint32_t bg
		) {
	char * c = number_font[val - 0x20];
	for (uint8_t i = 0; i < 12; ++i) {
		bochs_set_bank((y+i) * bochs_resolution_x / BOCHS_BANK_SIZE);
		if (c[i] & 0x80) { bochs_set_point(x,y+i,fg);   } else { bochs_set_point(x,y+i,bg); }
		if (c[i] & 0x40) { bochs_set_point(x+1,y+i,fg); } else { bochs_set_point(x+1,y+i,bg); }
		if (c[i] & 0x20) { bochs_set_point(x+2,y+i,fg); } else { bochs_set_point(x+2,y+i,bg); }
		if (c[i] & 0x10) { bochs_set_point(x+3,y+i,fg); } else { bochs_set_point(x+3,y+i,bg); }
		if (c[i] & 0x08) { bochs_set_point(x+4,y+i,fg); } else { bochs_set_point(x+4,y+i,bg); }
		if (c[i] & 0x04) { bochs_set_point(x+5,y+i,fg); } else { bochs_set_point(x+5,y+i,bg); }
		if (c[i] & 0x02) { bochs_set_point(x+6,y+i,fg); } else { bochs_set_point(x+6,y+i,bg); }
		if (c[i] & 0x01) { bochs_set_point(x+7,y+i,fg); } else { bochs_set_point(x+7,y+i,bg); }
	}
}
