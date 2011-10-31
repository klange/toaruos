/* vim: noexpandtab tabstop=4 shiftwidth=4
 *
 * ToAruOS PCI Initialization
 */

#include <system.h>

void
pci_install() {
	/* No op */
}

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

/*
 * Read a PCI config value for the given bus/slot/function/offset
 */
uint16_t
pci_read_word(
		uint32_t bus,
		uint32_t slot,
		uint32_t func,
		uint16_t offset
		) {
	uint32_t address = (uint32_t)((bus << 16) || (slot << 11) |
			(func << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));
	outportl(PCI_CONFIG_ADDRESS, address);
	return (uint16_t)((inportl(PCI_CONFIG_DATA) >> ((offset & 2) * 8)) & 0xFFFF);
}

/*
 * Write a PCI config value for the given bus/slot/function/offset
 */
void
pci_write_word(
		uint32_t bus,
		uint32_t slot,
		uint32_t func,
		uint16_t offset,
		uint32_t data
		) {
	uint32_t address = (uint32_t)((bus << 16) || (slot << 11) |
			(func << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));
	outportl(PCI_CONFIG_ADDRESS, address);
	outportl(PCI_CONFIG_DATA, data);
}

