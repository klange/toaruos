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

struct {
	uint16_t id;
	const char * name;
} _pci_vendors[] = {
	{0x1022, "AMD"},
	{0x106b, "Apple, Inc."},
	{0x1234, "Bochs/QEMU"},
	{0x1274, "Ensoniq"},
	{0x15ad, "VMWare"},
	{0x8086, "Intel Corporation"},
	{0x80EE, "VirtualBox"},
};

struct {
	uint16_t ven_id;
	uint16_t dev_id;
	const char * name;
} _pci_devices[] = {
	{0x1022, 0x2000, "PCNet Ethernet Controller (pcnet)"},
	{0x106b, 0x003f, "OHCI Controller"},
	{0x1234, 0x1111, "VGA BIOS Graphics Extensions"},
	{0x1274, 0x1371, "Creative Labs CT2518 (ensoniq audio)"},
	{0x15ad, 0x0740, "VM Communication Interface"},
	{0x15ad, 0x0405, "SVGA II Adapter"},
	{0x15ad, 0x0790, "PCI bridge"},
	{0x15ad, 0x07a0, "PCI Express Root Port"},
	{0x8086, 0x100e, "Gigabit Ethernet Controller (e1000)"},
	{0x8086, 0x100f, "Gigabit Ethernet Controller (e1000)"},
	{0x8086, 0x1237, "PCI & Memory"},
	{0x8086, 0x2415, "AC'97 Audio Chipset"},
	{0x8086, 0x7000, "PCI-to-ISA Bridge"},
	{0x8086, 0x7010, "IDE Interface"},
	{0x8086, 0x7110, "PIIX4 ISA"},
	{0x8086, 0x7111, "PIIX4 IDE"},
	{0x8086, 0x7113, "Power Management Controller"},
	{0x8086, 0x7190, "Host Bridge"},
	{0x8086, 0x7191, "AGP Bridge"},
	{0x80EE, 0xBEEF, "Bochs/QEMU-compatible Graphics Adapter"},
	{0x80EE, 0xCAFE, "Guest Additions Device"},
};


const char * pci_vendor_lookup(unsigned short vendor_id) {
	for (unsigned int i = 0; i < sizeof(_pci_vendors)/sizeof(_pci_vendors[0]); ++i) {
		if (_pci_vendors[i].id == vendor_id) {
			return _pci_vendors[i].name;
		}
	}
	return "";
}

const char * pci_device_lookup(unsigned short vendor_id, unsigned short device_id) {
	for (unsigned int i = 0; i < sizeof(_pci_devices)/sizeof(_pci_devices[0]); ++i) {
		if (_pci_devices[i].ven_id == vendor_id && _pci_devices[i].dev_id == device_id) {
			return _pci_devices[i].name;
		}
	}
	return "";
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
				pci_remaps[i] = 0x0a; /* fallback */
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
		debug_print(ERROR, "slot is %d, irq pin is %d, so pirq is %d and that maps to %d?", pci_extract_slot(device), irq_pin, pirq, pci_remaps[pirq]);
		if (pci_remaps[pirq] == 0x80) {
			debug_print(ERROR, "not mapped, falling back?\n");
			return pci_read_field(device, PCI_INTERRUPT_LINE, 1);
		}
		return pci_remaps[pirq];
	} else {
		return pci_read_field(device, PCI_INTERRUPT_LINE, 1);
	}
}
