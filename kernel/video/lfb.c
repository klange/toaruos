/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Bochs VBE / QEMU vga=std Graphics Driver
 */

#include <system.h>
#include <fs.h>
#include <types.h>
#include <logging.h>

#define PREFERRED_VY 4096
#define PREFERRED_B 32

uint16_t bochs_resolution_x = 0;
uint16_t bochs_resolution_y = 0;
uint16_t bochs_resolution_b = 0;

/*
 * Address of the linear frame buffer.
 * This can move, so it's a pointer instead of
 * #define.
 */
uint8_t * bochs_vid_memory = (uint8_t *)0xE0000000;

uintptr_t current_scroll = 0;

void
bochs_set_y_offset(uint16_t y) {
	outports(0x1CE, 0x9);
	outports(0x1CF, y);
	current_scroll = y;
}

uint16_t
bochs_current_scroll() {
	return current_scroll;
}

uintptr_t
bochs_get_address() {
	return (uintptr_t)bochs_vid_memory;
}

static void finalize_graphics(uint16_t x, uint16_t y, uint16_t b) {
	bochs_resolution_x = x;
	bochs_resolution_y = y;
	bochs_resolution_b = b;
}

void
graphics_install_bochs(uint16_t resolution_x, uint16_t resolution_y) {
	blog("Setting up BOCHS/QEMU graphics controller...");
	outports(0x1CE, 0x00);
	uint16_t i = inports(0x1CF);
	if (i < 0xB0C0 || i > 0xB0C6) {
		return;
	}
	outports(0x1CF, 0xB0C4);
	i = inports(0x1CF);
	/* Disable VBE */
	outports(0x1CE, 0x04);
	outports(0x1CF, 0x00);
	/* Set X resolution to 1024 */
	outports(0x1CE, 0x01);
	outports(0x1CF, resolution_x);
	/* Set Y resolution to 768 */
	outports(0x1CE, 0x02);
	outports(0x1CF, resolution_y);
	/* Set bpp to 32 */
	outports(0x1CE, 0x03);
	outports(0x1CF, PREFERRED_B);
	/* Set Virtual Height to stuff */
	outports(0x1CE, 0x07);
	outports(0x1CF, PREFERRED_VY);
	/* Re-enable VBE */
	outports(0x1CE, 0x04);
	outports(0x1CF, 0x41);

	/* XXX: Massive hack */
	uint32_t * text_vid_mem = (uint32_t *)0xA0000;
	text_vid_mem[0] = 0xA5ADFACE;

	for (uintptr_t fb_offset = 0xE0000000; fb_offset < 0xFF000000; fb_offset += 0x01000000) {
		/* Enable the higher memory */
		for (uintptr_t i = fb_offset; i <= fb_offset + 0xFF0000; i += 0x1000) {
			dma_frame(get_page(i, 1, kernel_directory), 0, 1, i);
		}

		/* Go find it */
		for (uintptr_t x = fb_offset; x < fb_offset + 0xFF0000; x += 0x1000) {
			if (((uintptr_t *)x)[0] == 0xA5ADFACE) {
				bochs_vid_memory = (uint8_t *)x;
				goto mem_found;
			}
		}

	}

mem_found:
	finalize_graphics(resolution_x, resolution_y, PREFERRED_B);
	bfinish(0);
}

void graphics_install_preset(uint16_t w, uint16_t h) {
	blog("Graphics were pre-configured (thanks, bootloader!), locating video memory...");
	uint16_t b = 32; /* If you are 24 bit, go away, we really do not support you. */

	/* XXX: Massive hack */
	uint32_t * herp = (uint32_t *)0xA0000;
	herp[0] = 0xA5ADFACE;

	for (uintptr_t fb_offset = 0xE0000000; fb_offset < 0xFF000000; fb_offset += 0x01000000) {
		/* Enable the higher memory */
		for (uintptr_t i = fb_offset; i <= fb_offset + 0xFF0000; i += 0x1000) {
			dma_frame(get_page(i, 1, kernel_directory), 0, 1, i);
		}

		/* Go find it */
		for (uintptr_t x = fb_offset; x < fb_offset + 0xFF0000; x += 0x1000) {
			if (((uintptr_t *)x)[0] == 0xA5ADFACE) {
				bochs_vid_memory = (uint8_t *)x;
				goto mem_found;
			}
		}
	}

mem_found:
	finalize_graphics(w,h,b);
	bfinish(0);
}


