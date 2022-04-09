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
#include <errno.h>
#include <kernel/module.h>
#include <kernel/printf.h>
#include <kernel/types.h>
#include <kernel/string.h>
#include <kernel/pci.h>
#include <kernel/mmu.h>
#include <kernel/args.h>
#include <kernel/procfs.h>
#include <kernel/syscall.h>

static void delay_yield(size_t subticks) {
	#ifdef __aarch64__
	asm volatile ("dsb sy\nisb" ::: "memory");
	#endif
	unsigned long s, ss;
	relative_time(0, subticks, &s, &ss);
	sleep_until((process_t *)this_core->current_process, s, ss);
	switch_task(0);
	#ifdef __aarch64__
	asm volatile ("dmb sy\n" ::: "memory");
	#endif
}


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

struct xhci_port_regs {
	volatile uint32_t port_status;
	volatile uint32_t port_pm_status;
	volatile uint32_t port_link_info;
	volatile uint32_t port_lpm_control;
} __attribute__((packed));

struct xhci_op_regs {
	volatile uint32_t op_usbcmd;   /* 0 */
	volatile uint32_t op_usbsts;   // 4
	volatile uint32_t op_pagesize; // 8h
	volatile uint32_t op__pad1[2]; // ch 10h
	volatile uint32_t op_dnctrl;   // 14h
	volatile uint32_t op_crcr[2];     // 18h 1ch
	volatile uint32_t op__pad2[4]; // 20h 24h 28h 2ch
	volatile uint32_t op_dcbaap[2];   // 30h 34h
	volatile uint32_t op_config;   // 38h
	volatile uint8_t  op_more_padding[964]; // 3ch-400h
	struct xhci_port_regs op_portregs[256];
} __attribute__((packed));

struct xhci_trb {
	uint32_t trb_thing_a;
	uint32_t trb_thing_b;
	uint32_t trb_status;
	uint32_t trb_control;
} __attribute__((packed));

struct XHCIControllerData {
	uintptr_t mmio;
	uint32_t device;
	uint64_t pcie_offset;
	struct xhci_cap_regs * cregs;
	struct xhci_op_regs * oregs;
	process_t * thread;
	volatile struct xhci_trb * cr_trbs;
	volatile struct xhci_trb * er_trbs;
	spin_lock_t command_queue;
	uint32_t command_queue_cycle;
	int command_queue_enq;
	volatile uint32_t * doorbells;
};

static uint64_t pci_addr_map(struct XHCIControllerData * controller, uint64_t addr) {
	return addr + controller->pcie_offset;
}

static uintptr_t pci_to_cpu(struct XHCIControllerData * controller, uint64_t addr) {
	return addr - controller->pcie_offset;
}

static uintptr_t allocate_page(uint64_t * phys_out) {
	uint64_t phys = mmu_allocate_a_frame() << 12;
	uintptr_t virt = (uintptr_t)mmu_map_mmio_region(phys, 4096);
	memset((void*)virt,0,4096);
	*phys_out = phys;
	return virt;
}

static int xhci_command(struct XHCIControllerData * controller, uint32_t p1, uint32_t p2, uint32_t status, uint32_t control) {
	spin_lock(controller->command_queue);

	control &= ~1;
	control |= controller->command_queue_cycle;

	controller->cr_trbs[controller->command_queue_enq].trb_thing_a = p1;
	controller->cr_trbs[controller->command_queue_enq].trb_thing_b = p2;
	controller->cr_trbs[controller->command_queue_enq].trb_status  = status;
	controller->cr_trbs[controller->command_queue_enq].trb_control = control;

	controller->command_queue_enq++;
	if (controller->command_queue_enq == 63) {
		controller->cr_trbs[controller->command_queue_enq].trb_control ^= 1;
		if (controller->cr_trbs[controller->command_queue_enq].trb_control & (1 << 1)) {
			controller->command_queue_cycle ^= 1;
		}
		controller->command_queue_enq = 0;
	}

	/* ring doorbell */
	controller->doorbells[0] = 0;

	spin_unlock(controller->command_queue);
	return 0;
}

static ssize_t xhci_write(fs_node_t * node, off_t offset, size_t size, uint8_t * buffer) {
	struct XHCIControllerData * controller = node->device;

	if (size != sizeof(struct xhci_trb)) return -EINVAL;

	struct xhci_trb * data = (void*)buffer;

	xhci_command(controller, data->trb_thing_a, data->trb_thing_b, data->trb_status, data->trb_control);

	return sizeof(struct xhci_trb);

}


static struct XHCIControllerData * _irq_owner = NULL;
#include <kernel/arch/x86_64/irq.h>
static int irq_handler(struct regs *r) {
	int irq = r->int_no - 32;

	if (_irq_owner) {
		/* Is it ours? */
		uint32_t status = _irq_owner->oregs->op_usbsts;
		if (status & (1 << 3)) {
			_irq_owner->oregs->op_usbsts = (1 << 3);
			dprintf("xhci: irq\n");

			uintptr_t rts = (uintptr_t)_irq_owner->cregs + _irq_owner->cregs->cap_rtsoff;
			volatile uint32_t * irs0_32 = (uint32_t*)(rts + 0x20);
			irs0_32[0] |= 1;

			make_process_ready(_irq_owner->thread);

			irq_ack(irq);
			return 1;
		}
	}

	return 0;
}

void xhci_thread(void * arg) {
	struct XHCIControllerData * controller = arg;

	controller->thread = (process_t*)this_core->current_process;
	spin_init(controller->command_queue);

	/* Begin generic XHCi */
	dprintf("xhci: available slots: %d\n", controller->cregs->cap_hcsparams1 & 0xFF);
	dprintf("xhci: available ports: %d\n", controller->cregs->cap_hcsparams1 >> 24);
	dprintf("xhci: resetting controller\n");
	dprintf("xhci: waiting for controller to stop...\n");
	uint32_t cmd = controller->oregs->op_usbcmd;
	cmd &= ~(1);
	controller->oregs->op_usbcmd = cmd;
	while (!(controller->oregs->op_usbsts & (1 << 0)));

	dprintf("xhci: restarting controller...\n");
	cmd = controller->oregs->op_usbcmd;
	cmd |= (1 << 1);
	controller->oregs->op_usbcmd = cmd;
	while ((controller->oregs->op_usbcmd & (1 << 1)));
	while ((controller->oregs->op_usbsts & (1 << 11)));
	dprintf("xhci: controller is ready: %#x\n", controller->oregs->op_usbsts);

	dprintf("xhci: slot config %#x -> %#x\n",
		controller->oregs->op_config, controller->cregs->cap_hcsparams1 & 0xFF);
	controller->oregs->op_config = controller->cregs->cap_hcsparams1 & 0xFF;

	/* TODO We may need to clear interrupts here by writing status back */
	uint32_t sts = controller->oregs->op_usbsts;
	(void)sts;

	dprintf("xhci: context size is %d\n",
		(controller->cregs->cap_hccparams1 & (1 << 2)) ? 64 : 32);

	uintptr_t ext_off = (controller->cregs->cap_hccparams1 >> 16) << 2;

	volatile uint32_t * ext_caps = (void*)((uintptr_t)controller->cregs + ext_off);

	/**
	 * Verify port configurations;
	 * should be port 1 is usb 2.0
	 *           port 2, 3, 4, 5 are 3.0
	 * port 1 has a hub with 4 ports?
	 */
	while (1) {
		uint32_t cap_val = *ext_caps;

		dprintf("xhci: ecap = %#x\n", cap_val);

		/* Bottom byte is type */
		if ((cap_val & 0xFF) == 2) {
			uint8_t rev_minor = ext_caps[0] >> 16;
			uint8_t rev_major = ext_caps[0] >> 24;
			//uint32_t name_str = ext_caps[1];

			uint8_t port_offset = ext_caps[2];
			uint8_t port_count  = ext_caps[2] >> 8;
			uint8_t psic = ext_caps[2] >> 28;

			dprintf("xhci:  protocol %d.%d %d port%s starting from port %d has %d speed%s\n",
				rev_major, rev_minor,
				port_count, &"s"[port_count==1],
				port_offset,
				psic, &"s"[psic==1]);
		}

		if (cap_val == 0xFFFFffff) break;
		if ((cap_val & 0xFF00) == 0) break;
		ext_caps = (void*)((uintptr_t)ext_caps + ((cap_val & 0xFF00) >> 6));
	}

	/* Device Context Base Address Array */
	uint64_t dcbaap;
	uint64_t * baseCtx = (void*)allocate_page(&dcbaap);

	dprintf("xhci: DCBAAP at %#zx (phys=%#zx)\n", (uintptr_t)baseCtx, dcbaap);
	controller->oregs->op_dcbaap[0] = pci_addr_map(controller, dcbaap);
	controller->oregs->op_dcbaap[1] = pci_addr_map(controller, dcbaap) >> 32;

	/* Enable slots */
	uint32_t cfg = controller->oregs->op_config;
	cfg &= ~0xFF;
	cfg |= 32;
	dprintf("xhci: set cfg = %#x\n", cfg);
	controller->oregs->op_config = cfg;

	/* trbs for event ring */
	uint64_t er_trbs_phys;
	void * er_trbs_virt = (void*)allocate_page(&er_trbs_phys);
	dprintf("xhci: er trbs = %#zx (phys=%#zx)\n",
		(uintptr_t)er_trbs_virt, er_trbs_phys);

	/* erst */
	uint64_t er_erst_phys;
	void * er_erst_virt = (void*)allocate_page(&er_erst_phys);
	dprintf("xhci: er erst = %#zx (phys=%#zx)\n",
		(uintptr_t)er_erst_virt, er_erst_phys);

	((volatile uint64_t*)er_erst_virt)[0] = pci_addr_map(controller, er_trbs_phys);
	((volatile uint64_t*)er_erst_virt)[1] = 64;

	dprintf("xhci: rtsoff = %#x\n", controller->cregs->cap_rtsoff);
	uintptr_t rts = (uintptr_t)controller->cregs + controller->cregs->cap_rtsoff;

	/* Interrupter points to event ring */
	volatile uint32_t * irs0_32 = (uint32_t*)(rts + 0x20);
	irs0_32[2] = 1; /* Size = 1 */
	irs0_32[6] = pci_addr_map(controller, er_trbs_phys) | (1 << 3);
	irs0_32[7] = (pci_addr_map(controller, er_trbs_phys) | (1 << 3)) >> 32;
	irs0_32[1] = 500; /* IMOD */
	irs0_32[0] = 2; /* enable interrupts */
	irs0_32[4] = pci_addr_map(controller, er_erst_phys);
	irs0_32[5] = pci_addr_map(controller, er_erst_phys) >> 32;

	/* trbs for control ring */
	uint64_t cr_trbs_phys;
	void * cr_trbs_virt = (void*)allocate_page(&cr_trbs_phys);

	((volatile uint64_t*)cr_trbs_virt)[63*2] = pci_addr_map(controller, cr_trbs_phys);
	((volatile uint64_t*)cr_trbs_virt)[63*2+1] = ((0x2UL | (6UL << 10)) << 32);

	controller->oregs->op_crcr[0] = (pci_addr_map(controller, cr_trbs_phys) | 1);
	controller->oregs->op_crcr[1] = (pci_addr_map(controller, cr_trbs_phys) | 1) >> 32;

	/* Scratchpad buffers, if needed */
	uint32_t hcs2 = controller->cregs->cap_hcsparams2;
	uint32_t sb_hi = (hcs2 >> 21) & 0x1f;
	uint32_t sb_lo = (hcs2 >> 27) & 0x1f;
	uint32_t sb_max = (sb_hi << 5) | sb_lo;

	/* should be 31 */
	if (sb_max) {
		dprintf("num scratchpad buffers = %u\n", sb_max);

		/* Allocate buffer for array */
		uint64_t scratch_phys;
		uint64_t *scratch_virt = (uint64_t*)allocate_page(&scratch_phys);
		dprintf("xhci: scratch at %#zx (phys=%#zx)\n", (uintptr_t)scratch_virt, scratch_phys);
		/* Our DMA mapping should be 1:1, so, uh, yolo */
		for (unsigned int i = 0; i < sb_max; ++i) {
			uint64_t sb_phys;
			allocate_page(&sb_phys);
			scratch_virt[i] = pci_addr_map(controller, sb_phys);
		}
		baseCtx[0] = pci_addr_map(controller, scratch_phys);
		dprintf("xhci: assigned scratchpad buffer array\n");
	}

	/* TODO This irq API sucks */
	int irq_number = pci_get_interrupt(controller->device);
	irq_install_handler(irq_number, irq_handler, "xhci");
	_irq_owner = controller;

	dprintf("xhci: Starting command ring...\n");
	{
		uint32_t cmd = controller->oregs->op_usbcmd;
		dprintf("cmd before = %#x\n", cmd);
		cmd |= (1 << 0) | (1 << 2);
		controller->oregs->op_usbcmd = cmd;
	}

	dprintf("xhci: status = %#x\n", controller->oregs->op_usbsts);

	delay_yield(50000);

	dprintf("xhci: status = %#x\n", controller->oregs->op_usbsts);
	if (controller->oregs->op_usbsts & (1 << 2)) goto _bail;


	dprintf("xhci: doorbells at %#x\n", controller->cregs->cap_dboff);
	controller->doorbells = (void*)((uintptr_t)controller->cregs + controller->cregs->cap_dboff);

	/* Just want to enable the hub for now, see if we can id it */
	controller->cr_trbs = cr_trbs_virt;
	controller->er_trbs = er_trbs_virt;

	controller->command_queue_cycle = 1;
	controller->command_queue_enq = 0;

	dprintf("xhci: status before ring = %#x\n", controller->oregs->op_usbsts);

	xhci_command(controller, 0, 0, 0, (23 << 10));

	char devName[20] = "/dev/xhciN";
	snprintf(devName, 19, "/dev/xhci%d", 0);
	fs_node_t * fnode = calloc(sizeof(fs_node_t), 1);
	snprintf(fnode->name, 100, "xhci%d", 0);
	fnode->flags   = FS_BLOCKDEVICE;
	fnode->mask    = 0660; /* Only accessible to root user/group */
	fnode->read    = NULL;
	fnode->write   = xhci_write;
	fnode->device  = controller;
	vfs_mount(devName, fnode);

	int event_deq = 0;
	uint32_t event_cycle_state = 1;

	while (1) {
		while ((controller->er_trbs[event_deq].trb_control & 1) != event_cycle_state) {
			switch_task(0);
		}

		uint32_t thing_a = controller->er_trbs[event_deq].trb_thing_a;
		uint32_t thing_b = controller->er_trbs[event_deq].trb_thing_a;
		uint32_t status  = controller->er_trbs[event_deq].trb_status;
		uint32_t control = controller->er_trbs[event_deq].trb_control;

		dprintf("xhci: event %d [%#x %#x %#x %#x]\n",
			event_deq, thing_a, thing_b, status, control);

		event_deq++;
		if (event_deq == 64) {
			event_deq = 0;
			event_cycle_state = !event_cycle_state;
		}

		/* Write new event dequeue pointer */
		uint64_t new_deq_phys = pci_addr_map(controller, er_trbs_phys + sizeof(struct xhci_trb) * event_deq) | (1 << 3);
		irs0_32[6] = new_deq_phys;
		irs0_32[7] = new_deq_phys >> 32;
	}

_bail:
	task_exit(1);
	__builtin_unreachable();
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
	controller->pcie_offset = 0;

	spawn_worker_thread(xhci_thread, "[xhci]", controller);
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

