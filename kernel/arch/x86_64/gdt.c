/**
 * @file kernel/arch/x86_64/gdt.c
 * @brief x86-64 GDT
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021 K. Lange
 */

#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/process.h>

/**
 * @brief 64-bit TSS
 */
typedef struct tss_entry {
	uint32_t reserved_0;
	uint64_t rsp[3];
	uint64_t reserved_1;
	uint64_t ist[7];
	uint64_t reserved_2;
	uint16_t reserved_3;
	uint16_t iomap_base;
} __attribute__ ((packed)) tss_entry_t;

typedef struct {
	uint16_t limit_low;
	uint16_t base_low;
	uint8_t base_middle;
	uint8_t access;
	uint8_t granularity;
	uint8_t base_high;
} __attribute__((packed)) gdt_entry_t;

typedef struct {
	uint32_t base_highest;
	uint32_t reserved0;
} __attribute__((packed)) gdt_entry_high_t;

typedef struct {
	uint16_t limit;
	uintptr_t base;
} __attribute__((packed)) gdt_pointer_t;

typedef struct  {
	gdt_entry_t entries[7];
	gdt_entry_high_t tss_extra;
	gdt_pointer_t pointer;
	tss_entry_t tss;
} __attribute__((packed)) __attribute__((aligned(0x10))) FullGDT;

FullGDT gdt[32] __attribute__((used)) = {{
	{
		{0x0000, 0x0000, 0x00, 0x00, 0x00, 0x00},
		{0xFFFF, 0x0000, 0x00, 0x9A, (1 << 5) | (1 << 7) | 0x0F, 0x00},
		{0xFFFF, 0x0000, 0x00, 0x92, (1 << 5) | (1 << 7) | 0x0F, 0x00},
		{0xFFFF, 0x0000, 0x00, 0xFA, (1 << 5) | (1 << 7) | 0x0F, 0x00},
		{0xFFFF, 0x0000, 0x00, 0xF2, (1 << 5) | (1 << 7) | 0x0F, 0x00},
		{0xFFFF, 0x0000, 0x00, 0xFA, (1 << 5) | (1 << 7) | 0x0F, 0x00},
		{0x0067, 0x0000, 0x00, 0xE9, 0x00, 0x00},
	},
	{0x00000000, 0x00000000},
	{0x0000, 0x0000000000000000},
	{0,{0,0,0},0,{0,0,0,0,0,0,0},0,0,0},
}};

void gdt_install(void) {
	for (int i = 1; i < 32; ++i) {
		memcpy(&gdt[i], &gdt[0], sizeof(*gdt));
	}

	for (int i = 0; i < 32; ++i) {
		gdt[i].pointer.limit = sizeof(gdt[i].entries)+sizeof(gdt[i].tss_extra)-1;
		gdt[i].pointer.base  = (uintptr_t)&gdt[i].entries;

		uintptr_t addr = (uintptr_t)&gdt[i].tss;
		gdt[i].entries[6].limit_low = sizeof(gdt[i].tss);
		gdt[i].entries[6].base_low = (addr & 0xFFFF);
		gdt[i].entries[6].base_middle = (addr >> 16) & 0xFF;
		gdt[i].entries[6].base_high = (addr >> 24) & 0xFF;
		gdt[i].tss_extra.base_highest = (addr >> 32) & 0xFFFFFFFF;
	}

	extern void * stack_top;
	gdt[0].tss.rsp[0] = (uintptr_t)&stack_top;

	asm volatile (
		"lgdt %0\n"
		"mov $0x10, %%ax\n"
		"mov %%ax, %%ds\n"
		"mov %%ax, %%es\n"
		"mov %%ax, %%ss\n"
		"mov $0x33, %%ax\n" /* TSS offset */
		"ltr %%ax\n"
		: : "m"(gdt[0].pointer) : "rax", "memory"
	);
}

void gdt_copy_to_trampoline(int ap, char * trampoline) {
	memcpy(trampoline, &gdt[ap].pointer, sizeof(gdt[ap].pointer));
}

void arch_set_kernel_stack(uintptr_t stack) {
	gdt[this_core->cpu_id].tss.rsp[0] = stack;
	this_core->syscall_stack = stack;
}

void arch_set_tls_base(uintptr_t tlsbase) {
	asm volatile ("wrmsr" : : "c"(0xc0000100), "d"((uint32_t)(tlsbase >> 32)), "a"((uint32_t)(tlsbase & 0xFFFFFFFF)));
}
