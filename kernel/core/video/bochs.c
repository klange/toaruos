/*
 * vim:tabstop=4
 * vim:noexpandtab
 *
 * Bochs VBE / QEMU vga=std Graphics Driver
 */

#include <system.h>
#include <fs.h>

uint16_t bochs_resolution_x = 1024;
uint16_t bochs_resolution_y = 768;
uint16_t bochs_resolution_b = 32;
uint16_t bochs_current_bank = 0;

#define BOCHS_BANK_SIZE 16384
#define BOCHS_VID_MEMORY ((uint32_t *)0xA0000)

void
graphics_install_bochs() {
	outports(0x1CE, 0x00);
	outports(0x1CF, 0xB0C4);
	short i = inports(0x1CF);
	kprintf("[bochs] Graphics ID is %x\n", (unsigned int)i);
	kprintf("[bochs] Enabling 1024x768x32 graphics mode!\n");
	/* Disable VBE */
	outports(0x1CE, 0x04);
	outports(0x1CF, 0x00);
	/* Set X resolution to 1024 */
	outports(0x1CE, 0x01);
	outports(0x1CF, 1024);
	/* Set Y resolution to 768 */
	outports(0x1CE, 0x02);
	outports(0x1CF, 768);
	/* Set bpp to 32 */
	outports(0x1CE, 0x03);
	outports(0x1CF, 0x20);
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
bochs_draw_logo() {
	fs_node_t * file = kopen("/bs.bmp",0);
	char *bufferb = malloc(file->length);
	size_t bytes_read = read_fs(file, 0, file->length, (uint8_t *)bufferb);
	size_t i;
	uint16_t x = 0;
	uint16_t y = 0;
	for (i = 54; i < bytes_read; i += 3) {
		uint32_t color =	bufferb[i] +
							bufferb[i+1] * 0x100 +
							bufferb[i+2] * 0x10000;
		bochs_set_coord(406 + x, 350 + (68 - y), color);
		++x;
		if (x == 212) {
			x = 0;
			++y;
		}
	}
	free(bufferb);
}
