/**
 * @file  kernel/arch/aarch64/virtio.c
 * @brief Rudimentary, hacky implementations of virtio input devices.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021-2022 K. Lange
 */
#include <stdint.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/pci.h>
#include <kernel/process.h>
#include <kernel/spinlock.h>
#include <kernel/mmu.h>

#include <kernel/arch/aarch64/dtb.h>
#include <kernel/arch/aarch64/gic.h>

struct irq_callback * irq_callbacks[256] = {0};

static spin_lock_t irq_acquire;

volatile uint32_t * gic_regs = NULL;
volatile uint32_t * gicc_regs = NULL;

void gic_map_regs(uintptr_t rpi_tag) {
	if (rpi_tag) {
		gic_regs = (volatile uint32_t*)mmu_map_mmio_region(0xff841000, 0x1000);
		gicc_regs = (volatile uint32_t*)mmu_map_mmio_region(0xff842000, 0x2000);
	} else {
		gic_regs = (volatile uint32_t*)mmu_map_mmio_region(0x08000000, 0x1000);
		gicc_regs = (volatile uint32_t*)mmu_map_mmio_region(0x08010000, 0x2000);
	}
}

void gic_send_sgi(uint8_t intid, int target) {
	uint64_t tlf = ((target == -1) ? 1UL : 0UL) << 24;
	uint64_t sii = intid & 0xF;
	uint64_t ctl = ((target == -1) ? 0UL : (1UL << target)) << 16;

	gic_regs[0x3c0] = (tlf|sii|ctl);
}

void gic_assign_interrupt(int irq, int (*callback)(process_t*,int,void*), void * data) {
	spin_lock(irq_acquire);
	struct irq_callback * cb = calloc(sizeof(struct irq_callback),1);

	cb->callback = callback;
	cb->owner = (process_t*)this_core->current_process;
	cb->data = data;
	cb->next = NULL;

	if (irq_callbacks[irq]) {
		struct irq_callback * parent = irq_callbacks[irq];
		while (parent->next) {
			parent = parent->next;
		}
		parent->next = cb;
	} else {
		irq_callbacks[irq] = cb;
	}

	spin_unlock(irq_acquire);
}

void gic_map_pci_interrupt(const char * name, uint32_t device, int * int_out, int (*callback)(process_t*,int,void*), void * isr_addr) {
	uint32_t phys_hi = (pci_extract_bus(device) << 16) | (pci_extract_slot(device) << 11);
	uint32_t pin = pci_read_field(device, PCI_INTERRUPT_PIN, 1);
	#if 0
	dprintf("%s: device %#x, slot = %d (0x%04x), irq pin = %d\n", name, device, pci_extract_slot(device),
		phys_hi, pin);
	#endif

	uint32_t * pcie_dtb = dtb_find_node_prefix("pcie@");
	if (!pcie_dtb) {
		dprintf("%s: can't find dtb entry\n", name);
		return;
	}

	uint32_t * intMask = dtb_node_find_property(pcie_dtb, "interrupt-map-mask");
	if (!intMask) {
		dprintf("%s: can't find property 'interrupt-map-mask'\n", name);
		return;
	}

	uint32_t * intMap = dtb_node_find_property(pcie_dtb, "interrupt-map");

	if (!intMap) {
		dprintf("%s: can't find property 'interrupt-map'\n", name);
		return;
	}

	for (int i = 0; i < (int)swizzle(intMap[0])/4; i += 10) {
		if (swizzle(intMap[i+2]) == (swizzle(intMask[2]) & phys_hi)) {
			if (swizzle(intMap[i+5]) == (swizzle(intMask[5]) & pin)) {
				#if 0
				dprintf("%s: %#x %#x %#x %#x\n", name,
					swizzle(intMap[i+2]), swizzle(intMap[i+3]), swizzle(intMap[i+4]), swizzle(intMap[i+5]));
				dprintf("%s: Matching device and pin, Interrupt maps to %d\n", name, swizzle(intMap[i+10]));
				#endif
				*int_out = swizzle(intMap[i+10]);
				gic_assign_interrupt(*int_out, callback, isr_addr);
				return;
			}
		}
	}
}

