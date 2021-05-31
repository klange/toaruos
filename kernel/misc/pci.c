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
 */
#include <stdint.h>
#include <kernel/string.h>
#include <kernel/pci.h>

/* TODO: PCI is sufficiently generic this shouldn't depend
 *       directly on x86-64 hardware... */
#include <kernel/arch/x86_64/ports.h>

/**
 * @brief Write to a PCI device configuration space field.
 */
void pci_write_field(uint32_t device, int field, int size, uint32_t value) {
	outportl(PCI_ADDRESS_PORT, pci_get_addr(device, field));
	outportl(PCI_VALUE_PORT, value);
}

/**
 * @brief Read from a PCI device configuration space field.
 */
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

	for (int func = 0; func < 8; ++func) {
		uint32_t dev = pci_box_device(0, 0, func);
		if (pci_read_field(dev, PCI_VENDOR_ID, 2) != PCI_NONE) {
			pci_scan_bus(f, type, func, extra);
		} else {
			break;
		}
	}
}

/**
 * @brief Extract the interrupt line from a device.
 *
 * Previously also did IRQ pin decoding, but we don't do that anymore.
 */
int pci_get_interrupt(uint32_t device) {
	return pci_read_field(device, PCI_INTERRUPT_LINE, 1);
}
