/**
 * @brief xHCI Host Controller Driver
 */

#include <kernel/printf.h>
#include <kernel/types.h>
#include <kernel/string.h>
#include <kernel/pci.h>
#include <kernel/mmu.h>
#include <kernel/args.h>
#include <kernel/procfs.h>

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

struct XHCIControllerData {
	uint32_t device;
	volatile struct xhci_cap_regs * cregs;
	volatile struct xhci_op_regs * oregs;
};

static int _counter = 0;

static ssize_t xhci_procfs_callback(fs_node_t * node, off_t offset, size_t size, uint8_t * buffer) {
	struct XHCIControllerData * controller = node->device;
	char buf[2048];

	size_t _bsize = snprintf(buf, 2000,
		"Device status: %#x\n"
		"64-bit capable? %s\n"
		"context size bit? %d\n"
		"has %d ports, %d slots\n"
		,
		controller->oregs->op_usbsts,
		(controller->cregs->cap_hccparams1 & 1) ? "yes" : "no",
		(controller->cregs->cap_hccparams1 >> 2) & 1,
		(controller->cregs->cap_hcsparams1 >> 24) & 0xFF,
		(controller->cregs->cap_hcsparams1) & 0xFF
	);

	if ((size_t)offset > _bsize) return 0;
	if (size > _bsize - offset) size = _bsize - offset;

	memcpy(buffer, buf + offset, size);
	return size;
}

static void find_xhci(uint32_t device, uint16_t v, uint16_t d, void * extra) {
	uint16_t device_type = pci_find_type(device);
	if (device_type == 0x0C03) {
		printf("xhci: found a host controller at %02x:%02x.%d\n",
			(int)pci_extract_bus(device),
			(int)pci_extract_slot(device),
			(int)pci_extract_func(device));

		struct XHCIControllerData * controller = calloc(sizeof(struct XHCIControllerData), 1);
		controller->device = device;

		/* The mmio address is 64 bits and combines BAR0 and BAR1... */
		uint64_t addr_low  = pci_read_field(device, PCI_BAR0, 4) & 0xFFFFFFF0;
		uint64_t addr_high = pci_read_field(device, PCI_BAR1, 4) & 0xFFFFFFFF; /* I think this is right? */
		uint64_t mmio_addr = (addr_high << 32) | addr_low;

		printf("xhci: mmio space is at %#lx\n", mmio_addr);

		/* Map mmio space... */
		uintptr_t xhci_regs = (uintptr_t)mmu_map_mmio_region(mmio_addr, 0x1000 * 4); /* I don't know. */

		controller->cregs = (volatile struct xhci_cap_regs*)xhci_regs;

		/* Read some registers */
		uint32_t caplength = controller->cregs->cap_caplen_version & 0xFF;
		uint32_t hciversion = (controller->cregs->cap_caplen_version >> 16) & 0xFFFF;
		printf("xhci: CAPLENGTH  = %d\n", caplength);
		printf("xhci: HCIVERSION = %d\n", hciversion);

		controller->oregs = (volatile struct xhci_op_regs*)(xhci_regs + caplength);

		printf("xhci: USBSTS = %#x\n", controller->oregs->op_usbsts);
		if (controller->oregs->op_usbsts & (1 << 0)) printf("xhci:   host controller halt\n");
		if (controller->oregs->op_usbsts & (1 << 2)) printf("xhci:   host system error\n");
		if (controller->oregs->op_usbsts & (1 << 3)) printf("xhci:   event interrupt\n");
		if (controller->oregs->op_usbsts & (1 << 4)) printf("xhci:   port change detect\n");
		if (controller->oregs->op_usbsts & (1 << 8)) printf("xhci:   save state status\n");
		if (controller->oregs->op_usbsts & (1 << 9)) printf("xhci:   restore state status\n");
		if (controller->oregs->op_usbsts & (1 << 10)) printf("xhci:   save restore error\n");
		if (controller->oregs->op_usbsts & (1 << 11)) printf("xhci:   controller not ready\n");
		if (controller->oregs->op_usbsts & (1 << 12)) printf("xhci:   host controlelr error\n");

		char devName[20] = "/dev/xhciN";
		snprintf(devName, 19, "/dev/xhci%d", _counter);
		fs_node_t * fnode = calloc(sizeof(fs_node_t), 1);
		snprintf(fnode->name, 100, "xhci%d", _counter);
		fnode->flags   = FS_BLOCKDEVICE;
		fnode->mask    = 0660; /* Only accessible to root user/group */
		fnode->read    = xhci_procfs_callback;
		fnode->device  = controller;
		vfs_mount(devName, fnode);
	}
}

void xhci_initialize(void) {
	pci_scan(find_xhci, -1, NULL);
}
