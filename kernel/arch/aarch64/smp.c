/**
 * @file  kernel/arch/aarch64/smp.c
 * @brief Routines for locating and starting other CPUs.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2022 K. Lange
 */
#include <stdint.h>
#include <kernel/process.h>
#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/mmu.h>

#include <sys/ptrace.h>

#include <kernel/arch/aarch64/regs.h>
#include <kernel/arch/aarch64/dtb.h>

extern process_t * spawn_kidle(int);
extern void timer_start(void);
extern void aarch64_processor_data(void);

static uint32_t cpu_on = 0;
static int method = 0;

static volatile uint32_t _smp_mutex = 0;

volatile uintptr_t * aarch64_jmp_target = 0;
volatile uint64_t  aarch64_sctlr      = 0;
volatile uint64_t  aarch64_tcr        = 0;
volatile uint64_t  aarch64_mair       = 0;
volatile uint64_t  aarch64_vbar       = 0;
volatile uintptr_t aarch64_ttbr0      = 0;
volatile uintptr_t aarch64_ttbr1      = 0;
volatile uintptr_t aarch64_stack      = 0;

void ap_start(uint64_t core_id) {
	dprintf("smp: core %zu is online\n", core_id);

	extern void arch_set_core_base(uintptr_t base);
	arch_set_core_base((uintptr_t)&processor_local_data[core_id]);

	this_core->cpu_id = core_id;

	extern void fpu_enable(void);
	fpu_enable();

	aarch64_processor_data();

	this_core->current_pml = mmu_get_kernel_directory();
	this_core->kernel_idle_task = spawn_kidle(0);
	this_core->current_process = this_core->kernel_idle_task;
	asm volatile ("isb");

	timer_start();

	_smp_mutex = 1;
	asm volatile ("isb" ::: "memory");

	switch_next();
}


void smp_bootstrap(void) {
	asm volatile (
		/* Store x0, which is our core ID */
		"mov x3, x0\n"
		/* Set up TTBR1 with high memory directory */
		"ldr x0, aarch64_ttbr1\n"
		"msr TTBR1_EL1, x0\n"
		/* Load stack pointer */
		"ldr x0, aarch64_stack\n"
		"mov sp, x0\n"
		/* Set up TTBR0 with our temporary directory */
		"ldr x0, aarch64_ttbr0\n"
		"msr TTBR0_EL1, x0\n"
		"dsb ishst\n"
		"tlbi vmalle1is\n"
		"dsb ish\n"
		"isb\n"
		/* Load VBAR from first core */
		"ldr x0, aarch64_vbar\n"
		"msr VBAR_EL1, x0\n"
		/* Load MAIR from first core */
		"ldr x0, aarch64_mair\n"
		"msr MAIR_EL1, x0\n"
		/* Load TCR from first core */
		"ldr x0, aarch64_tcr\n"
		"msr TCR_EL1, x0\n"
		/* Load SCTLR from first core, enable mmu */
		"ldr x0, aarch64_sctlr\n"
		"ldr x1, aarch64_jmp_target\n"
		"msr SCTLR_EL1, x0\n"
		"isb\n"
		/* Restore core ID as argument */
		"mov x0, x3\n"
		/* Jump to C entrypoint */
		"br x1\n");
}

static void start_cpu(uint32_t * node) {
	uint32_t * cpuid = dtb_node_find_property(node, "reg");
	uint32_t num = swizzle(cpuid[2]);
	dprintf("smp: cpu node %d %#zx '%s'\n", num, (uintptr_t)node, (char *)(node));
	if (num == 0) return;

	if (method == 0x637668) {
		_smp_mutex = 0;
		aarch64_stack = (uintptr_t)sbrk(4096);
		uint64_t x0 = cpu_on;
		uint64_t x1 = num;
		uint64_t x2 = mmu_map_to_physical(NULL, (uintptr_t)&smp_bootstrap);
		uint64_t x3 = num;
		asm volatile ("dc civac, %0\ndsb sy" :: "r"(&aarch64_stack) : "memory");
		asm volatile ("isb" ::: "memory");

		asm volatile (
			"mov x0, %0\n"
			"mov x1, %1\n"
			"mov x2, %2\n"
			"mov x3, %3\n"
			"hvc 0" :: "r"(x0), "r"(x1), "r"(x2), "r"(x3) : "x0","x1","x2","x3");
		while (_smp_mutex == 0);

		processor_count = num + 1;
	} else {
		dprintf("smp: Don't know how to turn on with '%#x'\n", method);
		/* smc? */
	}
}

#define _pagemap __attribute__((aligned(4096))) = {0}
static union PML startup_ttbr0[2][512] _pagemap;

void aarch64_smp_start(void) {

	uint32_t * psci = dtb_find_node("psci");

	if (!psci) {
		dprintf("smp: no 'psci' interface node\n");
		return;
	}

	uint32_t * psci_method = dtb_node_find_property(psci, "method");
	uint32_t * psci_cpu_on = dtb_node_find_property(psci, "cpu_on");

	if (!psci_method || !psci_cpu_on) {
		dprintf("smp: don't know how to turn on these cores\n");
		return;
	}

	dprintf("smp: startup method is '0x%x'\n", psci_method[2]);
	method = psci_method[2];
	cpu_on = swizzle(psci_cpu_on[2]);

	uint32_t * cpus = dtb_find_node("cpus");
	if (!cpus) {
		dprintf("smp: no 'cpus' node\n");
		return;
	}

	aarch64_jmp_target = (void*)(uintptr_t)ap_start;
	asm volatile ("mrs %0, MAIR_EL1"  : "=r"(aarch64_mair));
	asm volatile ("mrs %0, TCR_EL1"   : "=r"(aarch64_tcr));
	asm volatile ("mrs %0, SCTLR_EL1" : "=r"(aarch64_sctlr));
	asm volatile ("mrs %0, VBAR_EL1"  : "=r"(aarch64_vbar));

	startup_ttbr0[0][0].raw = mmu_map_to_physical(NULL, (uintptr_t)&startup_ttbr0[1]) | (0x3) | (1 << 10);
	for (long i = 0; i < 512; ++i) {
		startup_ttbr0[1][i].raw = (i << 30) | (1 << 2) | 1 | (1 << 10);
	}

	aarch64_ttbr0 = mmu_map_to_physical(NULL, (uintptr_t)&startup_ttbr0[0]);
	aarch64_ttbr1 = mmu_map_to_physical(NULL, (uintptr_t)mmu_get_kernel_directory());

	asm volatile (
		"dsb ishst\n"
		"tlbi vmalle1is\n"
		"dsb ish\n"
		"isb\n"
	);

	dtb_callback_direct_children(cpus, start_cpu);
}
