/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2011-2018 K. Lange
 *
 * ToAruOS PCI Initialization
 */

#include <kernel/system.h>
#include <kernel/pci.h>
#include <kernel/logging.h>

void pci_write_field(uint32_t device, int field, int size, uint32_t value) {
	outportl(PCI_ADDRESS_PORT, pci_get_addr(device, field));
	outportl(PCI_VALUE_PORT, value);
}

uint32_t pci_read_field(uint32_t device, int field, int size) {
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
	return 0xFFFF;
}

uint16_t pci_find_type(uint32_t dev) {
	return (pci_read_field(dev, PCI_CLASS, 1) << 8) | pci_read_field(dev, PCI_SUBCLASS, 1);
}


void pci_scan_hit(pci_func_t f, uint32_t dev, void * extra) {
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

void pci_scan(pci_func_t f, int type, void * extra) {

	if ((pci_read_field(0, PCI_HEADER_TYPE, 1) & 0x80) == 0) {
		pci_scan_bus(f,type,0,extra);
		return;
	}

	for (int func = 0; func < 8; ++func) {
		uint32_t dev = pci_box_device(0, 0, func);
		if (pci_read_field(dev, PCI_VENDOR_ID, 2) != PCI_NONE) {
			pci_scan_bus(f, type, func, extra);
		} else {
			break;
		}
	}
}

static void find_isa_bridge(uint32_t device, uint16_t vendorid, uint16_t deviceid, void * extra) {
	if (vendorid == 0x8086 && (deviceid == 0x7000 || deviceid == 0x7110)) {
		*((uint32_t *)extra) = device;
	}
}
static uint32_t pci_isa = 0;
static uint8_t pci_remaps[4] = {0};
void pci_remap(void) {
	pci_scan(&find_isa_bridge, -1, &pci_isa);
	if (pci_isa) {
		for (int i = 0; i < 4; ++i) {
			pci_remaps[i] = pci_read_field(pci_isa, 0x60+i, 1);
			if (pci_remaps[i] == 0x80) {
				pci_remaps[i] = 10 + (i%1);
			}
		}
		uint32_t out = 0;
		memcpy(&out, &pci_remaps, 4);
		pci_write_field(pci_isa, 0x60, 4, out);
	}
}

int pci_get_interrupt(uint32_t device) {

	if (pci_isa) {
		uint32_t irq_pin = pci_read_field(device, 0x3D, 1);
		if (irq_pin == 0) {
			/* ??? */
			debug_print(ERROR, "PCI device does not specific interrupt line");
			return pci_read_field(device, PCI_INTERRUPT_LINE, 1);
		}
		int pirq = (irq_pin + pci_extract_slot(device) - 2) % 4;
		int int_line = pci_read_field(device, PCI_INTERRUPT_LINE, 1);
		debug_print(ERROR, "slot is %d, irq pin is %d, so pirq is %d and that maps to %d? int_line=%d", pci_extract_slot(device), irq_pin, pirq, pci_remaps[pirq], int_line);
		for (int i = 0; i < 4; ++i) {
			debug_print(ERROR, "  irq[%d] = %d", i, pci_remaps[i]);
		}
		if (pci_remaps[pirq] >= 0x80) {
			debug_print(ERROR, "not mapped, remapping?");
			if (int_line == 0xFF) {
				int_line = 10;
				pci_write_field(device, PCI_INTERRUPT_LINE, 1, int_line);
				debug_print(ERROR, "? Just going in blind here.\n");
			}
			pci_remaps[pirq] = int_line;
			uint32_t out = 0;
			memcpy(&out, &pci_remaps, 4);
			pci_write_field(pci_isa, 0x60, 4, out);
			return int_line;
		}
		pci_write_field(device, PCI_INTERRUPT_LINE, 1, pci_remaps[pirq]);
		return pci_remaps[pirq];
	} else {
		return pci_read_field(device, PCI_INTERRUPT_LINE, 1);
	}
}
