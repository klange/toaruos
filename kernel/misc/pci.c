/**
 * @file  kernel/misc/pci.c
 * @brief PCI configuration and scanning.
 *
 * Functions for dealing with PCI devices through configuration mode #1
 * (CPU port I/O methods), including scanning and modifying device
 * configuration bytes.
 *
 * This used to have methods for dealing with ISA bridge IRQ remapping,
 * but it has been removed for the moment.
 *
 * TODO: Implement MSI configuration?
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2011-2021 K. Lange
 */
#include <stdint.h>
#include <kernel/string.h>
#include <kernel/pci.h>
#include <kernel/printf.h>

#include <kernel/mmu.h>

#ifdef __x86_64__
#include <kernel/arch/x86_64/ports.h>
#endif

static uintptr_t pcie_addr(uint32_t device, int field) {
	return (pci_extract_bus(device) << 20) | (pci_extract_slot(device) << 15) | (pci_extract_func(device) << 12) | (field);
}

uintptr_t pcie_ecam_phys = 0x3f000000;

/**
 * @brief Write to a PCI device configuration space field.
 */
void pci_write_field(uint32_t device, int field, int size, uint32_t value) {
#ifdef __x86_64__
	outportl(PCI_ADDRESS_PORT, pci_get_addr(device, field));
	outportl(PCI_VALUE_PORT, value);
#else

	/* ECAM space */
	if (size == 4) {
		*(volatile uint32_t*)mmu_map_from_physical(pcie_ecam_phys + pcie_addr(device,field)) = value;
		return;
	} else if (size == 2) {
		*(volatile uint16_t*)mmu_map_from_physical(pcie_ecam_phys + pcie_addr(device,field)) = value;
		return;
	} else if (size == 1) {
		*(volatile uint8_t*)mmu_map_from_physical(pcie_ecam_phys + pcie_addr(device,field)) = value;
		return;
	}

	dprintf("rejected invalid field write\n");

#endif
}

/**
 * @brief Read from a PCI device configuration space field.
 */
uint32_t pci_read_field(uint32_t device, int field, int size) {
#ifdef __x86_64__
	outportl(PCI_ADDRESS_PORT, pci_get_addr(device, field));

	if (size == 4) {
		uint32_t t = inportl(PCI_VALUE_PORT);
		return t;
	} else if (size == 2) {
		uint16_t t = inports(PCI_VALUE_PORT + (field & 2));
		return t;
	} else if (size == 1) {
		uint8_t t = inportb(PCI_VALUE_PORT + (field & 3));
		return t;
	}
#else
	uintptr_t field_addr = pcie_addr(device,field);
	if (size == 4) {
		return *(volatile uint32_t*)mmu_map_from_physical(pcie_ecam_phys + field_addr);
	} else if (size == 2) {
		return *(volatile uint16_t*)mmu_map_from_physical(pcie_ecam_phys + field_addr);
	} else if (size == 1) {
		return *(volatile uint8_t*)mmu_map_from_physical(pcie_ecam_phys + field_addr);
	}
#endif
	return 0xFFFF;
}

/**
 * @brief Obtain the device type from the class and subclass fields.
 */
uint16_t pci_find_type(uint32_t dev) {
	return (pci_read_field(dev, PCI_CLASS, 1) << 8) | pci_read_field(dev, PCI_SUBCLASS, 1);
}

static void pci_scan_hit(pci_func_t f, uint32_t dev, void * extra) {
	int dev_vend = (int)pci_read_field(dev, PCI_VENDOR_ID, 2);
	int dev_dvid = (int)pci_read_field(dev, PCI_DEVICE_ID, 2);

	f(dev, dev_vend, dev_dvid, extra);
}

void pci_scan_func(pci_func_t f, int type, int bus, int slot, int func, void * extra) {
	uint32_t dev = pci_box_device(bus, slot, func);
	if (type == -1 || type == pci_find_type(dev)) {
		pci_scan_hit(f, dev, extra);
	}
	if (pci_find_type(dev) == PCI_TYPE_BRIDGE) {
		pci_scan_bus(f, type, pci_read_field(dev, PCI_SECONDARY_BUS, 1), extra);
	}
}

void pci_scan_slot(pci_func_t f, int type, int bus, int slot, void * extra) {
	uint32_t dev = pci_box_device(bus, slot, 0);
	if (pci_read_field(dev, PCI_VENDOR_ID, 2) == PCI_NONE) {
		return;
	}
	pci_scan_func(f, type, bus, slot, 0, extra);
	if (!pci_read_field(dev, PCI_HEADER_TYPE, 1)) {
		return;
	}
	for (int func = 1; func < 8; func++) {
		uint32_t dev = pci_box_device(bus, slot, func);
		if (pci_read_field(dev, PCI_VENDOR_ID, 2) != PCI_NONE) {
			pci_scan_func(f, type, bus, slot, func, extra);
		}
	}
}

void pci_scan_bus(pci_func_t f, int type, int bus, void * extra) {
	for (int slot = 0; slot < 32; ++slot) {
		pci_scan_slot(f, type, bus, slot, extra);
	}
}

/**
 * @brief Scan PCI buses for devices, calling the given function for each device.
 *
 * Used by drivers to implement device discovery, runs a callback function for ever
 * device found. A device consists of a bus, slot, and function. Also performs
 * recursive scans of bridges.
 */
void pci_scan(pci_func_t f, int type, void * extra) {
	if ((pci_read_field(0, PCI_HEADER_TYPE, 1) & 0x80) == 0) {
		pci_scan_bus(f,type,0,extra);
		return;
	}

	int hit = 0;
	for (int func = 0; func < 8; ++func) {
		uint32_t dev = pci_box_device(0, 0, func);
		if (pci_read_field(dev, PCI_VENDOR_ID, 2) != PCI_NONE) {
			hit = 1;
			pci_scan_bus(f, type, func, extra);
		} else {
			break;
		}
	}

	if (!hit) {
		for (int bus = 0; bus < 256; ++bus) {
			for (int slot = 0; slot < 32; ++slot) {
				pci_scan_slot(f,type,bus,slot,extra);
			}
		}
	}
}

int pci_get_interrupt(uint32_t device) {
	return pci_read_field(device, PCI_INTERRUPT_LINE, 1);
}
