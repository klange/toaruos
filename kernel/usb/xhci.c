/**
 * @brief xHCI Host Controller Driver
 */

#include <kernel/printf.h>
#include <kernel/types.h>
#include <kernel/pci.h>
#include <kernel/mmu.h>
#include <kernel/args.h>

struct xhci_cap_regs {
	uint32_t cap_caplen_version;
	uint32_t cap_hcsparams1;
	uint32_t cap_hcsparams2;
	uint32_t cap_hcsparams3;
	uint32_t cap_hccparams1;
	uint32_t cap_dboff;
	uint32_t cap_rtsoff;
	uint32_t cap_hccparams2;
};

struct xhci_op_regs {
	uint32_t op_usbcmd;
	uint32_t op_usbsts;
	uint32_t op_pagesize;
	uint32_t op__pad1[2];
	uint32_t op_dnctrl;
	uint32_t op_crcr;
	uint32_t op__pad2[5];
	uint32_t op_dcbaap;
	uint32_t op__pad3[1];
	uint32_t op_config;
};

static void find_xhci(uint32_t device, uint16_t v, uint16_t d, void * extra) {
	uint16_t device_type = pci_find_type(device);
	if (device_type == 0x0C03) {
		printf("xhci: found a host controller at %02x:%02x.%d\n",
			(int)pci_extract_bus(device),
			(int)pci_extract_slot(device),
			(int)pci_extract_func(device));

		/* The mmio address is 64 bits and combines BAR0 and BAR1... */
		uint64_t addr_low  = pci_read_field(device, PCI_BAR0, 4) & 0xFFFFFFF0;
		uint64_t addr_high = pci_read_field(device, PCI_BAR1, 4) & 0xFFFFFFFF; /* I think this is right? */
		uint64_t mmio_addr = (addr_high << 32) | addr_low;

		printf("xhci: mmio space is at %#lx\n", mmio_addr);

		/* Map mmio space... */
		uintptr_t xhci_regs = (uintptr_t)mmu_map_mmio_region(mmio_addr, 0x1000 * 4); /* I don't know. */


		volatile struct xhci_cap_regs * cregs = (volatile struct xhci_cap_regs*)xhci_regs;

		/* Read some registers */
		uint32_t caplength = cregs->cap_caplen_version & 0xFF;
		uint32_t hciversion = (cregs->cap_caplen_version >> 16) & 0xFFFF;
		printf("xhci: CAPLENGTH  = %d\n", caplength);
		printf("xhci: HCIVERSION = %d\n", hciversion);

		volatile struct xhci_op_regs * oregs = (volatile struct xhci_op_regs*)(xhci_regs + caplength);

		printf("xhci: USBSTS = %#x\n", oregs->op_usbsts);
		if (oregs->op_usbsts & (1 << 0)) printf("xhci:   host controller halt\n");
		if (oregs->op_usbsts & (1 << 2)) printf("xhci:   host system error\n");
		if (oregs->op_usbsts & (1 << 3)) printf("xhci:   event interrupt\n");
		if (oregs->op_usbsts & (1 << 4)) printf("xhci:   port change detect\n");
		if (oregs->op_usbsts & (1 << 8)) printf("xhci:   save state status\n");
		if (oregs->op_usbsts & (1 << 9)) printf("xhci:   restore state status\n");
		if (oregs->op_usbsts & (1 << 10)) printf("xhci:   save restore error\n");
		if (oregs->op_usbsts & (1 << 11)) printf("xhci:   controller not ready\n");
		if (oregs->op_usbsts & (1 << 12)) printf("xhci:   host controlelr error\n");

	}
}

void xhci_initialize(void) {
	pci_scan(find_xhci, -1, NULL);
}
