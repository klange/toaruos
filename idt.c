#include <system.h>

/*
 * IDT Entry
 */
struct idt_entry {
	unsigned short base_low;
	unsigned short sel;
	unsigned char zero;
	unsigned char flags;
	unsigned short base_high;
} __attribute__((packed));

struct idt_ptr {
	unsigned short limit;
	unsigned int base;
} __attribute__((packed));

struct idt_entry idt[256];
struct idt_ptr idtp;

extern void idt_load();

/*
 * idt_set_gate
 * Set an IDT gate
 */
void
idt_set_gate(
		unsigned char num,
		unsigned long base,
		unsigned short sel,
		unsigned char flags
		) {
	idt[num].base_low =		(base & 0xFFFF);
	idt[num].base_high =	(base >> 16) & 0xFFFF;
	idt[num].sel =			sel;
	idt[num].zero =			0;
	idt[num].flags =		flags;
}

/*
 * idt_install
 * Install the IDTs
 */
void
idt_install() {
	idtp.limit = (sizeof(struct idt_entry) * 256) - 1;
	idtp.base = &idt;
	memset(&idt, 0, sizeof(struct idt_entry) * 256);

	idt_load();
}
