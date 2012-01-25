/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Bochs VBE / QEMU vga=std Graphics Driver
 */

#include <system.h>
#include <fs.h>
#include <types.h>
#include <vesa.h>
#include <logging.h>

#define PROMPT_FOR_MODE 0

/* Friggin' frick, this should be a config option
 * because it's 4096 on some instances of Qemu,
 * ie the one on my laptop, but it's 2048 on
 * the EWS machines. */
#define BOCHS_BUFFER_SIZE 2048
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

#include "../v8086/rme.h"

void
graphics_install_vesa(uint16_t x, uint16_t y) {
	blog("Setting up VESA video controller...");

	/* VESA Structs */
	struct VesaControllerInfo *info = (void*)0x10000;
	struct VesaModeInfo *modeinfo = (void*)0x9000;

	/* 8086 Emulator Status */
	tRME_State *emu;
	void * lowCache;
	lowCache = malloc(RME_BLOCK_SIZE);
	memcpy(lowCache, NULL, RME_BLOCK_SIZE);
	emu = RME_CreateState();
	emu->Memory[0] = lowCache;
	for (int i = RME_BLOCK_SIZE; i < 0x100000; i += RME_BLOCK_SIZE) {
		emu->Memory[i/RME_BLOCK_SIZE] = (void*)i;
	}
	int ret, mode;

	/* Find modes */
	uint16_t * modes;
	memcpy(info->Signature, "VBE2", 4);
	emu->AX.W = 0x4F00;
	emu->ES   = 0x1000;
	emu->DI.W = 0;
	ret = RME_CallInt(emu, 0x10);
	if (info->Version < 0x200 || info->Version > 0x300) {
		bfinish(2);
		kprintf("\033[JYou have attempted to use the VESA/VBE2 driver\nwith a card that does not support VBE2.\n");
		kprintf("\nSystem responded to VBE request with version: 0x%x\n", info->Version);

		STOP;
	}
	modes = (void*)FP_TO_LINEAR(info->Videomodes.Segment,info->Videomodes.Offset);

	uint16_t best_x    = 0;
	uint16_t best_y    = 0;
	uint16_t best_b    = 0;
	uint16_t best_mode = 0;

	for (int i = 1; modes[i] != 0xFFFF; ++i) {
		emu->AX.W = 0x4F01;
		emu->CX.W = modes[i];
		emu->ES   = 0x0900;
		emu->DI.W = 0x0000;
		RME_CallInt(emu, 0x10);
#if PROMPT_FOR_MODE
		kprintf("%d = %dx%d:%d\n", i, modeinfo->Xres, modeinfo->Yres, modeinfo->bpp);
	}

	kprintf("Please select a mode: ");
	char buf[10];
	kgets(buf, 10);
	mode = atoi(buf);
#else
		if ((abs(modeinfo->Xres - x) < abs(best_x - x)) && (abs(modeinfo->Yres - y) < abs(best_y - y))) {
				best_mode = i;
				best_x = modeinfo->Xres;
				best_y = modeinfo->Yres;
				best_b = modeinfo->bpp;
		}
	}
	for (int i = 1; modes[i] != 0xFFFF; ++i) {
		emu->AX.W = 0x4F01;
		emu->CX.W = modes[i];
		emu->ES   = 0x0900;
		emu->DI.W = 0x0000;
		RME_CallInt(emu, 0x10);
		if (modeinfo->Xres == best_x && modeinfo->Yres == best_y) {
			if (modeinfo->bpp > best_b) {
				best_mode = i;
				best_b = modeinfo->bpp;
			}
		}
	}

	if (best_b < 24) {
		kprintf("!!! Rendering at this bit depth (%d) is not currently supported.\n", best_b);
		STOP;
	}


	mode = best_mode;

#endif

	emu->AX.W = 0x4F01;
	if (mode < 100) {
		emu->CX.W = modes[mode];
	} else {
		emu->CX.W = mode;
	}
	emu->ES   = 0x0900;
	emu->DI.W = 0x0000;
	RME_CallInt(emu, 0x10);

	emu->AX.W = 0x4F02;
	emu->BX.W = modes[mode];
	RME_CallInt(emu, 0x10);

	uint16_t actual_x = modeinfo->Xres;
	uint16_t actual_y = modeinfo->Yres;
	uint16_t actual_b = modeinfo->bpp;

	bochs_vid_memory = (uint8_t *)modeinfo->physbase;

	if (!bochs_vid_memory) {
		uint32_t * herp = (uint32_t *)0xA0000;
		herp[0] = 0xA5ADFACE;

		/* Enable the higher memory */
		for (uintptr_t i = 0xE0000000; i <= 0xE0FF0000; i += 0x1000) {
			dma_frame(get_page(i, 1, kernel_directory), 0, 1, i);
		}
		for (uintptr_t i = 0xF0000000; i <= 0xF0FF0000; i += 0x1000) {
			dma_frame(get_page(i, 1, kernel_directory), 0, 1, i);
		}


		/* Go find it */
		for (uintptr_t x = 0xE0000000; x < 0xE0FF0000; x += 0x1000) {
			if (((uintptr_t *)x)[0] == 0xA5ADFACE) {
				bochs_vid_memory = (uint8_t *)x;
				goto mem_found;
			}
		}
		for (uintptr_t x = 0xF0000000; x < 0xF0FF0000; x += 0x1000) {
			if (((uintptr_t *)x)[0] == 0xA5ADFACE) {
				bochs_vid_memory = (uint8_t *)x;
				goto mem_found;
			}
		}
	}
mem_found:

	/*
	 * Finalize the graphics setup with the actual selected resolution.
	 */
	finalize_graphics(actual_x, actual_y, actual_b);
	bfinish(0);
}

