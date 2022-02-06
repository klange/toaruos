/**
 * @file modules/piix4.c
 * @brief Intel PIIX4 ISA Bridge Driver
 * @package x86_64
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021 K. Lange
 */
#include <errno.h>
#include <kernel/types.h>
#include <kernel/args.h>
#include <kernel/printf.h>
#include <kernel/pci.h>
#include <kernel/module.h>

#define PIIX4_PCI_PIRQRC  0x60

static void find_isa_bridge(uint32_t device, uint16_t vendorid, uint16_t deviceid, void * extra) {
	if (vendorid == 0x8086 && (deviceid == 0x7000 || deviceid == 0x7110)) {
		*((uint32_t *)extra) = device;
	}
}

static uint32_t pci_isa = 0;
static int pci_isa_base_slot = 0;
static int pci_isa_bus_offset = 0;
static int pci_isa_last_bus = 0;
static uint8_t pci_remaps[4] = {0};

static void piix_remap(uint32_t device, uint16_t vendorid, uint16_t deviceid, void * extra) {
	uint32_t irq_pin = pci_read_field(device, PCI_INTERRUPT_PIN, 1);
	uint32_t irq_line = pci_read_field(device, PCI_INTERRUPT_LINE, 1);
	if (irq_pin == 0) return;
	if (pci_extract_bus(device) != pci_isa_last_bus) {
		pci_isa_bus_offset++;
		pci_isa_last_bus = pci_extract_bus(device);
	}
	/* Calculate PIRQ from pin and device slot. */
	int slot = (pci_extract_slot(device) - pci_isa_base_slot) % 4;
	int bus  = (pci_isa_bus_offset) % 4;
	int pirq = (slot + irq_pin + bus - 1) % 4;
	if (irq_line < 32 && irq_line != pci_remaps[pirq]) {
		pci_write_field(device, PCI_INTERRUPT_LINE, 1, pci_remaps[pirq]);
	}
}

static int init(int argc, char * argv[]) {
	if (args_present("nopciremap")) return -ENODEV;

	pci_scan(&find_isa_bridge, -1, &pci_isa);
	if (!pci_isa) {
		return -ENODEV;
	}

	pci_isa_base_slot = pci_extract_slot(pci_isa);
	pci_isa_last_bus = pci_extract_bus(pci_isa);

	for (int i = 0; i < 4; ++i) {
		pci_remaps[i] = pci_read_field(pci_isa, PIIX4_PCI_PIRQRC + i, 1);
	}

	uint32_t out = 0;
	memcpy(&out, &pci_remaps, 4);
	pci_write_field(pci_isa, PIIX4_PCI_PIRQRC, 4, out);
	pci_scan(piix_remap, -1, NULL);

	return 0;
}

static int fini(void) {
	return 0;
}

struct Module metadata = {
	.name = "piix4",
	.init = init,
	.fini = fini,
};

