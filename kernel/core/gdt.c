/*
 * vim:tabstop=4
 * vim:noexpandtab
 */
#include <system.h>

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

/*
 * (ASM) gdt_flush
 * Reloads the segment registers
 */
extern void gdt_flush();

/*
 * gdt_set_gate
 * Set a GDT descriptor
 */
void
gdt_set_gate(
		int num,
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
gdt_install() {
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
	tss_entry.cs     = 0x0b;
	tss_entry.ss     =
		tss_entry.ds =
		tss_entry.es =
		tss_entry.fs =
		tss_entry.gs = 0x13;
}

void
set_kernel_stack(
		uintptr_t stack
		) {
	tss_entry.esp0 = stack;
}


