/**
 * @brief xHCI Host Controller Driver
 * @file modules/xhci.c
 * @package x86_64
 *
 * @warning This is a stub driver.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021 K. Lange
 */
#include <kernel/module.h>
#include <kernel/printf.h>
#include <kernel/types.h>
#include <kernel/string.h>
#include <kernel/pci.h>
#include <kernel/mmu.h>
#include <kernel/args.h>
#include <kernel/procfs.h>
#include <kernel/syscall.h>

struct xhci_cap_regs {
	volatile uint32_t cap_caplen_version;
	volatile uint32_t cap_hcsparams1;
	volatile uint32_t cap_hcsparams2;
	volatile uint32_t cap_hcsparams3;
	volatile uint32_t cap_hccparams1;
	volatile uint32_t cap_dboff;
	volatile uint32_t cap_rtsoff;
	volatile uint32_t cap_hccparams2;
} __attribute__((packed));

struct xhci_op_regs {
	volatile uint32_t op_usbcmd;
	volatile uint32_t op_usbsts;
	volatile uint32_t op_pagesize;
	volatile uint32_t op__pad1[2];
	volatile uint32_t op_dnctrl;
	volatile uint32_t op_crcr;
	volatile uint32_t op__pad2[5];
	volatile uint32_t op_dcbaap_lo;
	volatile uint32_t op_dcbaap_hi;
	volatile uint32_t op_config;
} __attribute__((packed));

struct XHCIControllerData {
	uintptr_t mmio;
	uint32_t device;
	struct xhci_cap_regs * cregs;
	struct xhci_op_regs * oregs;
};

static int _counter = 0;

static ssize_t xhci_procfs_callback(fs_node_t * node, off_t offset, size_t size, uint8_t * buffer) {
	struct XHCIControllerData * controller = node->device;
	char buf[2048];

	size_t _bsize = snprintf(buf, 2000,
		"%08x\n"
		"0x%016zx\n"
		"CAPLENGTH  0x%08x\n"
		"HCSPARAMS1 0x%08x\n"
		"HCSPARAMS2 0x%08x\n"
		"HCSPARAMS3 0x%08x\n"
		"HCCPARAMS1 0x%08x\n"
		"DBOFF      0x%08x\n"
		"RTSOFF     0x%08x\n"
		"HCCPARAMS2 0x%08x\n"
		,
		controller->device,
		(uintptr_t)controller->mmio,
		controller->cregs->cap_caplen_version,
		controller->cregs->cap_hcsparams1,
		controller->cregs->cap_hcsparams2,
		controller->cregs->cap_hcsparams3,
		controller->cregs->cap_hccparams1,
		controller->cregs->cap_dboff,
		controller->cregs->cap_rtsoff,
		controller->cregs->cap_hccparams2
	);

	if ((size_t)offset > _bsize) return 0;
	if (size > _bsize - offset) size = _bsize - offset;

	memcpy(buffer, buf + offset, size);
	return size;
}

static void find_xhci(uint32_t device, uint16_t v, uint16_t d, void * extra) {
	if (pci_find_type(device) != 0x0C03) return;
	if (pci_read_field(device, PCI_PROG_IF, 1) != 0x30) return;
	fs_node_t * stderr = extra;

	uint16_t command_reg = pci_read_field(device, PCI_COMMAND, 2);
	command_reg |= (1 << 2);
	command_reg |= (1 << 1);
	pci_write_field(device, PCI_COMMAND, 2, command_reg);

	/* The mmio address is 64 bits and combines BAR0 and BAR1... */
	uint64_t addr_low  = pci_read_field(device, PCI_BAR0, 4) & 0xFFFFFFF0;
	uint64_t addr_high = pci_read_field(device, PCI_BAR1, 4) & 0xFFFFFFFF; /* I think this is right? */
	uint64_t mmio_addr = (addr_high << 32) | addr_low;

	if (mmio_addr == 0) {
		/* Need to map... */
		fprintf(stderr, "xhci: Device is unmapped. TODO: Check if this is behind a PCI bridge...\n");
		return;
		#if 0
		mmio_addr = mmu_allocate_n_frames(2) << 12;
		pci_write_field(device, PCI_BAR0, 4, (mmio_addr & 0xFFFFFFF0) | (1 << 2));
		pci_write_field(device, PCI_BAR1, 4, (mmio_addr >> 32));
		#endif
	}

	fprintf(stderr, "xhci: controller found\n");

	struct XHCIControllerData * controller = calloc(sizeof(struct XHCIControllerData), 1);
	controller->device = device;

	/* Map mmio space... */
	uintptr_t xhci_regs = (uintptr_t)mmu_map_mmio_region(mmio_addr, 0x1000 * 4); /* I don't know. */
	controller->mmio  = mmio_addr;
	controller->cregs = (struct xhci_cap_regs*)xhci_regs;
	controller->oregs = (struct xhci_op_regs*)(xhci_regs + (controller->cregs->cap_caplen_version & 0xFF));

	fprintf(stderr, "xhci: available slots: %d\n", controller->cregs->cap_hcsparams1 & 0xFF);
	fprintf(stderr, "xhci: available ports: %d\n", controller->cregs->cap_hcsparams1 >> 24);
	fprintf(stderr, "xhci: resetting controller\n");
	fprintf(stderr, "xhci: waiting for controller to stop...\n");
	controller->oregs->op_usbcmd = 0;
	while (!(controller->oregs->op_usbsts & (1 << 0)));

	fprintf(stderr, "xhci: restarting controller...\n");
	controller->oregs->op_usbcmd = (1 << 1);
	while ((controller->oregs->op_usbcmd & (1 << 1)));
	while ((controller->oregs->op_usbsts & (1 << 11)));
	fprintf(stderr, "xhci: controller is ready.\n");

	fprintf(stderr, "xhci: slot config %#x -> %#x\n",
		controller->oregs->op_config, controller->cregs->cap_hcsparams1 & 0xFF);
	controller->oregs->op_config = controller->cregs->cap_hcsparams1 & 0xFF;

	fprintf(stderr, "xhci: clearing interrupts?\n");
	uint32_t sts = controller->oregs->op_usbsts;
	controller->oregs->op_usbsts = sts;
	controller->oregs->op_dnctrl = 0;

	fprintf(stderr, "xhci: context size is %d\n",
		(controller->cregs->cap_hccparams1 & (1 << 1)) ? 64 : 32);

	uintptr_t dcbaap = mmu_allocate_n_frames(1) << 12;

	char * baseCtx = mmu_map_mmio_region(dcbaap, 0x1000);
	memset(baseCtx, 0, 0x1000);

	controller->oregs->op_dcbaap_lo = dcbaap & 0xFFFFFFFF;
	controller->oregs->op_dcbaap_hi = dcbaap >> 32UL;
	controller->oregs->op_dcbaap_lo = dcbaap & 0xFFFFFFFF;
	controller->oregs->op_dcbaap_hi = dcbaap >> 32UL;

	


	char devName[20] = "/dev/xhciN";
	snprintf(devName, 19, "/dev/xhci%d", _counter);
	fs_node_t * fnode = calloc(sizeof(fs_node_t), 1);
	snprintf(fnode->name, 100, "xhci%d", _counter);
	fnode->flags   = FS_BLOCKDEVICE;
	fnode->mask    = 0660; /* Only accessible to root user/group */
	fnode->read    = xhci_procfs_callback;
	fnode->device  = controller;
	vfs_mount(devName, fnode);

	_counter++;
}

static int init(int argc, char * argv[]) {
	fs_node_t * node = FD_ENTRY(1); /* Get the stdout for the process that loaded the module */
	pci_scan(find_xhci, -1, node);
	return 0;
}

static int fini(void) {
	return 0;
}

struct Module metadata = {
	.name = "xhci",
	.init = init,
	.fini = fini,
};

