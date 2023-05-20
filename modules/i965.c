/**
 * @file  modules/i965.c
 * @brief Bitbanged modeset driver for a ThinkPad T410's Intel graphics.
 * @package x86_64
 *
 * This is NOT a viable driver for Intel graphics devices. It assumes Vesa
 * has already properly set up the display pipeline with the needed timings
 * for the panel on one particular model of Lenovo ThinkPad and then sets
 * a handful of registers to get the framebuffer into the right resolution.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021 K. Lange
 */
#include <errno.h>
#include <kernel/printf.h>
#include <kernel/types.h>
#include <kernel/video.h>
#include <kernel/pci.h>
#include <kernel/mmu.h>
#include <kernel/vfs.h>
#include <kernel/args.h>
#include <kernel/module.h>

#define REG_PIPEASRC      0x6001C
#define REG_PIPEACONF     0x70008
#define  PIPEACONF_ENABLE (1 << 31)
#define  PIPEACONF_STATE  (1 << 30)
#define REG_DSPALINOFF    0x70184
#define REG_DSPASTRIDE    0x70188
#define REG_DSPASURF      0x7019c

extern fs_node_t * lfb_device;
extern int lfb_use_write_combining;
static uintptr_t ctrl_regs = 0;

static uint32_t i965_mmio_read(uint32_t reg) {
	return *(volatile uint32_t*)(ctrl_regs + reg);
}

static void i965_mmio_write(uint32_t reg, uint32_t val) {
	*(volatile uint32_t*)(ctrl_regs + reg) = val;
}

static void split(uint32_t val, uint32_t * a, uint32_t * b) {
	*a = (val & 0xFFFF) + 1;
	*b = (val >> 16) + 1;
}

static void i965_modeset(uint16_t x, uint16_t y) {
	/* Disable pipe A while we update source size */
	uint32_t pipe = i965_mmio_read(REG_PIPEACONF);
	i965_mmio_write(REG_PIPEACONF, pipe & ~PIPEACONF_ENABLE);
	while (i965_mmio_read(REG_PIPEACONF) & PIPEACONF_STATE);

	/* Set source size */
	i965_mmio_write(REG_PIPEASRC, ((x - 1) << 16) | (y - 1));

	/* Re-enable pipe */
	pipe = i965_mmio_read(REG_PIPEACONF);
	i965_mmio_write(REG_PIPEACONF, pipe | PIPEACONF_ENABLE);
	while (!(i965_mmio_read(REG_PIPEACONF) & PIPEACONF_STATE));

	/* Keep the plane enabled while we update stride value */
	i965_mmio_write(REG_DSPALINOFF, 0);        /* offset to default of 0 */
	i965_mmio_write(REG_DSPASTRIDE, x * 4); /* stride to 4 x width */
	i965_mmio_write(REG_DSPASURF, 0);          /* write to surface address triggers change; use default of 0 */

	/* Update the values we expose to userspace. */
	lfb_resolution_x = x;
	lfb_resolution_y = y;
	lfb_resolution_b = 32;
	lfb_resolution_s = i965_mmio_read(REG_DSPASTRIDE);
	lfb_memsize = lfb_resolution_s * lfb_resolution_y;
	lfb_device->length  = lfb_memsize;
}

extern void fbterm_draw_logo(void);
extern void fbterm_reset(void);

static void setup_framebuffer(uint32_t pcidev) {
	/* Map BAR space for the control registers */
	uint32_t ctrl_space = pci_read_field(pcidev, PCI_BAR0, 4);
	pci_write_field(pcidev, PCI_BAR0, 4, 0xFFFFFFFF);
	uint32_t ctrl_size = pci_read_field(pcidev, PCI_BAR0, 4);
	ctrl_size = ~(ctrl_size & -15) + 1;
	pci_write_field(pcidev, PCI_BAR0, 4, ctrl_space);
	ctrl_space &= 0xFFFFFF00;
	ctrl_regs = (uintptr_t)mmu_map_mmio_region(ctrl_space, ctrl_size);

	lfb_resolution_impl = i965_modeset;
	lfb_set_resolution(1440,900);

	lfb_use_write_combining = 1;

	/* Normally we don't clear the screen on mode set, but we should do it here */
	memset(lfb_vid_memory, 0, lfb_memsize);

	/* Redraw the boot logo; if we were loaded by userspace, it'll probably
	 * be overwritten pretty quickly by the compositor? But whatever... */
	fbterm_reset();
	fbterm_draw_logo();

	/* Helpful to know why the console text got cleared */
	dprintf("i965: video configured for %u x %u\n", lfb_resolution_x, lfb_resolution_y);
}

static void find_intel(uint32_t device, uint16_t v, uint16_t d, void * extra) {
	if (v == 0x8086 && d == 0x0046) {
		setup_framebuffer(device);
	}
}

static int i965_install(int argc, char * argv[]) {
	if (args_present("noi965")) return -ENODEV;
	if (!lfb_resolution_x) return -ENODEV;
	pci_scan(find_intel, -1, NULL);
	return 0;
}

static int fini(void) {
	return 0;
}

struct Module metadata = {
	.name = "i965",
	.init = i965_install,
	.fini = fini,
};

