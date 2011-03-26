/*
 * vim:tabstop=4
 * vim:noexpandtab
 *
 * Bochs VBE / QEMU vga=std Graphics Driver
 */

#include <system.h>

void
graphics_install_bochs() {
	outports(0x1CE, 0x00);
	outports(0x1CF, 0xB0C4);
	short i = inports(0x1CF);
	kprintf("Bochs VBE Mode [%x]\n", (unsigned int)i);
	kprintf("Switching to GRAPHICS MODE!\n");
	timer_wait(90);
	/* Disable VBE */
	outports(0x1CE, 0x04);
	outports(0x1CF, 0x00);
	/* Set X resolution to 1024 */
	outports(0x1CE, 0x01);
	outports(0x1CF, 1024);
	/* Set Y resolution to 768 */
	outports(0x1CE, 0x02);
	outports(0x1CF, 768);
	/* Set bpp to 8 */
	outports(0x1CE, 0x03);
	outports(0x1CF, 0x20);
	/* Re-enable VBE */
	outports(0x1CE, 0x04);
	outports(0x1CF, 0x41);
	/* Let's draw some stuff */
	uint32_t * vid_mem = (uint32_t *)0xA0000;
	uint32_t x = 0;
	while (x < 1024 * 768) {
		if ((x / 1024) % 2) {
			*vid_mem = 0x00FF0000;
		} else {
			*vid_mem = krand();
		}
		++vid_mem;
		++x;
	}
}
