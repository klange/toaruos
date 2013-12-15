/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * ToAruOS PCI Initialization
 */

#include <system.h>
#include <logging.h>
#include <pci.h>
#include <pci_list.h>

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

void pci_write_field(uint32_t device, int field, int size, uint32_t value) {
	outportl(PCI_ADDRESS_PORT, pci_get_addr(device, field));

	if (size == 4) {
		outportl(PCI_VALUE_PORT, value);
	} else if (size == 2) {
		outports(PCI_VALUE_PORT + (field & 2), (uint16_t)value);
	} else if (size == 1) {
		outportb(PCI_VALUE_PORT + (field & 3), (uint8_t)value);
	}
}

static void pci_read_bar(uint32_t device, uint8_t index, uint32_t *addr, uint32_t *mask) {
	uint32_t field = PCI_BAR0 + (index * sizeof(uint32_t));

	*addr = pci_read_field(device, field, 4);

	// Find what the size of the bar is
	pci_write_field(device, field, 4, 0xFFFFFFFF);
	*mask = pci_read_field(device, field, 4);
	
	// restore
	pci_write_field(device, field, 4, *addr);
}

void pci_get_bar(uint32_t device, pci_bar *bar, uint8_t index) {
	uint32_t addr_low, addr_hi;
	uint32_t mask_low, mask_hi;

	pci_read_bar(device, index, &addr_low, &mask_low);

	// 64-bit MMIO
	if (addr_low & PCI_BAR_64) {
		pci_read_bar(device, index+1, &addr_hi, &mask_hi);
		bar->u.address = (((uint64_t)addr_hi) << 32) | (addr_low & ~0xf);
		bar->size = ~(((uint64_t)mask_hi << 32) | (mask_low & ~0xf)) + 1;
		bar->flags = addr_low & 0xf;
	// IO Register
	} else if (addr_low & PCI_BAR_IO) {
		bar->u.port = (uint16_t)(addr_low & ~0x3);
		bar->size = (uint16_t)(~(mask_low & ~0x3) + 1);
		bar->flags = addr_low & 0x3;
	// 32-bit MMIO
	} else {
		bar->u.address = (uint64_t)(addr_low & ~0xf);
		bar->size = ~(mask_low & ~0xf) + 1;
		bar->flags = addr_low & 0xf;
	}
}

uint16_t pci_find_type(uint32_t dev) {
	return (pci_read_field(dev, PCI_CLASS, 1) << 8) | pci_read_field(dev, PCI_SUBCLASS, 1);
}

const char * pci_vendor_lookup(unsigned short vendor_id) {
	for (unsigned int i = 0; i < PCI_VENTABLE_LEN; ++i) {
		if (PciVenTable[i].VenId == vendor_id) {
			return PciVenTable[i].VenFull;
		}
	}
	return "";
}

const char * pci_device_lookup(unsigned short vendor_id, unsigned short device_id) {
	for (unsigned int i = 0; i < PCI_DEVTABLE_LEN; ++i) {
		if (PciDevTable[i].VenId == vendor_id && PciDevTable[i].DevId == device_id) {
			return PciDevTable[i].ChipDesc;
		}
	}
	return "";
}

void pci_scan_hit(pci_func_t f, uint32_t dev) {
	int dev_vend = (int)pci_read_field(dev, PCI_VENDOR_ID, 2);
	int dev_dvid = (int)pci_read_field(dev, PCI_DEVICE_ID, 2);

	f(dev, dev_vend, dev_dvid);
}

void pci_scan_func(pci_func_t f, int type, int bus, int slot, int func) {
	uint8_t secbus;
	uint32_t dev = pci_box_device(bus, slot, func);
	if (type == -1 || type == pci_find_type(dev)) {
		pci_scan_hit(f, dev);
	}
	if (pci_find_type(dev) == PCI_TYPE_BRIDGE) {
		secbus = pci_read_field(dev, PCI_SECONDARY_BUS, 1);
		if (bus != secbus) {
			//debug_print(NOTICE, "Scanning secondary bus %d", secbus);
			pci_scan_bus(f, type, secbus);
		}
	}
}

void pci_scan_slot(pci_func_t f, int type, int bus, int slot) {
	uint32_t dev;
	uint8_t count;

	dev = pci_box_device(bus, slot, 0);
	if (pci_read_field(dev, PCI_VENDOR_ID, 2) == PCI_NONE) {
		return;
	}

	pci_scan_func(f, type, bus, slot, 0);
	if (!pci_read_field(dev, PCI_HEADER_TYPE, 1)) {
		return;
	}

	count = pci_read_field(dev, PCI_HEADER_TYPE, 1);
	count = count & PCI_HEADER_TYPE_MULTIFUNC ? 8 : 1;

	for (int func = 1; func < count; func++) {
		uint32_t dev = pci_box_device(bus, slot, func);
		if (pci_read_field(dev, PCI_VENDOR_ID, 2) != PCI_NONE) {
			pci_scan_func(f, type, bus, slot, func);
		}
	}
}

void pci_scan_bus(pci_func_t f, int type, int bus) {
	for (int slot = 0; slot < 32; ++slot) {
		pci_scan_slot(f, type, bus, slot);
	}
}

void pci_scan(pci_func_t f, int type) {
	pci_scan_bus(f, type, 0);

	if (!pci_read_field(0, PCI_HEADER_TYPE, 1)) {
		return;
	}
/*
	for (int func = 1; func < 8; ++func) {
		uint32_t dev = pci_box_device(0, 0, func);
		if (pci_read_field(dev, PCI_VENDOR_ID, 2) != PCI_NONE) {
			pci_scan_bus(f, type, func);
		} else {
			break;
		}
	}
*/
}

list_t *pcidev_list;

void pci_probe(uint32_t dev, uint16_t vendorid, uint16_t deviceid) {
	pci_dev *pci;
	int i;
   
	pci = (pci_dev *)malloc(sizeof(pci_dev));

	pci->dev = dev;
	pci->vendorid = vendorid;
	pci->deviceid = deviceid;

	pci->type = PCI_HDR_TYPE(pci_read_field(dev, PCI_HEADER_TYPE, 1));
	pci->status = pci_read_field(dev, PCI_STATUS, 2);
	pci->cmd = pci_read_field(dev, PCI_COMMAND, 2);

	pci->irq = pci_read_field(dev, PCI_INTERRUPT_LINE, 1);
	pci->pin = pci_read_field(dev, PCI_INTERRUPT_PIN, 1);

	if (pci->type == PCI_HEADER_TYPE_DEVICE) {
		for (i = 0; i < 5; i++) {
			pci_get_bar(dev, &pci->bar[i], i);
		}
	}

	pci->dev_node.value = pci;
	list_append(pcidev_list, &pci->dev_node); 
}

void pci_install(void) {
	int i;
	pcidev_list = list_create();

	pci_scan_bus(pci_probe, -1, 0);

	foreach(node, pcidev_list) {
		pci_dev *pci = ((pci_dev *)node->value);
		debug_print(NOTICE, "PCI %02d:%02d:%02d %04x:%04x %s, %s",
			pci_extract_bus(pci->dev),
			pci_extract_slot(pci->dev),
			pci_extract_func(pci->dev), 
			pci->vendorid,
			pci->deviceid,
			pci_vendor_lookup(pci->vendorid),
			pci_device_lookup(pci->vendorid, pci->deviceid));

		if (pci->irq != 0) {
			debug_print(NOTICE, "\tPCI Interrupt IRQ %d pin %c", 
				pci->irq,
				'A' + (pci->pin - 1));
		}

		if (pci->type == PCI_HEADER_TYPE_DEVICE) {
			for (i = 0; i < 5; i++) {
				if (pci->bar[i].u.address) {
					if (pci->bar[i].flags & PCI_BAR_IO) {
						debug_print(NOTICE, "\tPCI BAR%d port %x size %x", 
							i, 
							pci->bar[i].u.port,
							pci->bar[i].size);
					} else {
						debug_print(NOTICE, "\tPCI BAR%d mmio %x size %x", 
							i,
							pci->bar[i].u.address,
							pci->bar[i].size);
						debug_print(NOTICE, "\tPCI bar size %x", pci->bar[i].size);
					}
				}
			}
		}

		
	}
}
