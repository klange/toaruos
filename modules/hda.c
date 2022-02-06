/**
 * @file kernel/audio/hda.c
 * @brief Driver for the Intel High Definition Audio.
 * @package x86_64
 *
 * @warning This is a stub driver.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021 K. Lange
 */
#include <errno.h>
#include <kernel/types.h>
#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/pci.h>
#include <kernel/process.h>
#include <kernel/mmu.h>
#include <kernel/list.h>
#include <kernel/module.h>
#include <kernel/mod/snd.h>

#include <kernel/arch/x86_64/ports.h>
#include <kernel/arch/x86_64/regs.h>
#include <kernel/arch/x86_64/irq.h>

#if 0
static snd_knob_t _knobs[] = {
	{
		"Master",
		SND_KNOB_MASTER
	}
};

static int hda_mixer_read(uint32_t knob_id, uint32_t *val);
static int hda_mixer_write(uint32_t knob_id, uint32_t val);

/**
 * TODO: We should generate this dynamically
 *       for each card based on the ports we find.
 */
static snd_device_t _snd = {
	.name            = "Intel HDA",
	.device          = NULL,
	.playback_speed  = 48000,
	.playback_format = SND_FORMAT_L16SLE,

	.knobs     = _knobs,
	.num_knobs = 1,

	.mixer_read  = hda_mixer_read,
	.mixer_write = hda_mixer_write,
};

static int hda_mixer_read(uint32_t knob_id, uint32_t *val) {
	return 0;
}

static int hda_mixer_write(uint32_t knob_id, uint32_t val) {
	return 0;
}
#endif

static void hda_setup(uint32_t device) {
	/* Map MMIO */
	uintptr_t mmio_addr = pci_read_field(device, PCI_BAR0, 4) & 0xFFFFFFFE;
	void* mapped_mmio = mmu_map_mmio_region(mmio_addr, 0x1000 * 8); /* TODO size? */

	/* Enable bus mastering, MMIO */
	pci_write_field(device, PCI_COMMAND, 2, 0x6);

	/* Enable controller */
	((volatile uint32_t*)mapped_mmio)[2] |= 1;
	while (!(((volatile uint32_t*)mapped_mmio)[2]) & 0x1);

	printf("hda: codec bitmap: %04x\n",
		((volatile uint16_t*)mapped_mmio)[7]);

	/* Stop DMA engine */

	/* Configure DMA engine */

	/* Map space for DMA */

	/* Set up ring buffer pointers */

	/* Then do the same for the DMA response ring buffer... ? */
}

static void find_hda(uint32_t device, uint16_t vendorid, uint16_t deviceid, void * extra) {
	if (vendorid == 0x8086 && deviceid == 0x2668) {
		hda_setup(device);
	}
}

static int hda_install(int argc, char * argv[]) {
	pci_scan(&find_hda, -1, NULL);
	return 0;
}

static int fini(void) {
	#if 0
	snd_unregister(&_snd);

	free(_device.bdl);
	for (int i = 0; i < AC97_BDL_LEN; i++) {
		free(_device.bufs[i]);
	}
	#endif
	return 0;
}

struct Module metadata = {
	.name = "hda",
	.init = hda_install,
	.fini = fini,
};

