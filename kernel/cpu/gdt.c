/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Global Descriptor Tables module
 *
 * Part of the ToAruOS Kernel
 * (C) 2011 Kevin Lange
 */
#include <system.h>
#include <logging.h>

static void write_tss(int32_t, uint16_t, uint32_t);
tss_entry_t tss_entry;

/*
 * Global Descriptor Table Entry
 */
struct gdt_entry {
	/* Limits */
	unsigned short limit_low;
	/* Segment address */
	unsigned short base_low;
	unsigned char base_middle;
	/* Access modes */
	unsigned char access;
	unsigned char granularity;
	unsigned char base_high;
} __attribute__((packed));

/*
 * GDT pointer
 */
struct gdt_ptr {
	unsigned short limit;
	unsigned int base;
} __attribute__((packed));

struct gdt_entry	gdt[6];
struct gdt_ptr		gp;

/**
 * (ASM) gdt_flush
 * Reloads the segment registers
 */
extern void gdt_flush(void);

/**
 * Set a GDT descriptor
 *
 * @param num The number for the descriptor to set.
 * @param base Base address
 * @param limit Limit
 * @param access Access permissions
 * @param gran Granularity
 */
void
gdt_set_gate(
		size_t num,
		unsigned long base,
		unsigned long limit,
		unsigned char access,
		unsigned char gran
		) {
	/* Base Address */
	gdt[num].base_low =		(base & 0xFFFF);
	gdt[num].base_middle =	(base >> 16) & 0xFF;
	gdt[num].base_high =	(base >> 24) & 0xFF;
	/* Limits */
	gdt[num].limit_low =	(limit & 0xFFFF);
	gdt[num].granularity =	(limit >> 16) & 0X0F;
	/* Granularity */
	gdt[num].granularity |= (gran & 0xF0);
	/* Access flags */
	gdt[num].access = access;
}

/*
 * gdt_install
 * Install the kernel's GDTs
 */
void
gdt_install(void) {
	/* GDT pointer and limits */
	gp.limit = (sizeof(struct gdt_entry) * 6) - 1;
	gp.base = (unsigned int)&gdt;
	/* NULL */
	gdt_set_gate(0, 0, 0, 0, 0);
	/* Code segment */
	gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);
	/* Data segment */
	gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);
	/* User code */
	gdt_set_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xCF);
	/* User data */
	gdt_set_gate(4, 0, 0xFFFFFFFF, 0xF2, 0xCF);
	write_tss(5, 0x10, 0x0);
	/* Go go go */
	gdt_flush();
	tss_flush();
}

/**
 * Write a TSS (we only do this once)
 */
static void
write_tss(
		int32_t num,
		uint16_t ss0,
		uint32_t esp0
		) {
	uintptr_t base  = (uintptr_t)&tss_entry;
	uintptr_t limit = base + sizeof(tss_entry);

	/* Add the TSS descriptor to the GDT */
	gdt_set_gate(num, base, limit, 0xE9, 0x00);

	memset(&tss_entry, 0x0, sizeof(tss_entry));

	tss_entry.ss0    = ss0;
	tss_entry.esp0   = esp0;
	/* Zero out the descriptors */
	tss_entry.cs     = 0x0b;
	tss_entry.ss     =
		tss_entry.ds =
		tss_entry.es =
		tss_entry.fs =
		tss_entry.gs = 0x13;
	tss_entry.iomap_base = sizeof(tss_entry);
}

/**
 * Set the kernel stack.
 *
 * @param stack Pointer to a the stack pointer for the kernel.
 */
void
set_kernel_stack(
		uintptr_t stack
		) {
	tss_entry.esp0 = stack;
}

