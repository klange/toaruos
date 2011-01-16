#include <system.h>

/*
 * Global Descriptor Tablet Entry
 */
struct gdt_entry {
	unsigned short limit_low;
	unsigned short base_low;
	unsigned char base_middle;
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

struct gdt_entry	gdt[3];
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
	gp.limit = (sizeof(struct gdt_entry) * 3) - 1;
	gp.base = &gdt;
	/* NULL */
	gdt_set_gate(0, 0, 0, 0, 0);
	/* Code segment */
	gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);
	/* Data segment */
	gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);
	/* Go go go */
	gdt_flush();
}
