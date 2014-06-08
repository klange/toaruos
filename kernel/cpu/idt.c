/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2011-2013 Kevin Lange
 *
 * Interrupt Descriptor Tables
 *
 */
#include <system.h>
#include <logging.h>

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

/*
 * IDT pointer
 */
struct idt_ptr {
	unsigned short limit;
	uintptr_t base;
} __attribute__((packed));

struct idt_entry idt[256];
struct idt_ptr idtp;

extern void idt_load(void);

/*
 * idt_set_gate
 * Set an IDT gate
 */
void idt_set_gate(
		unsigned char num,
		void (*base)(void),
		unsigned short sel,
		unsigned char flags
		) {
	idt[num].base_low =		((uintptr_t)base & 0xFFFF);
	idt[num].base_high =	((uintptr_t)base >> 16) & 0xFFFF;
	idt[num].sel =			sel;
	idt[num].zero =			0;
	idt[num].flags =		flags | 0x60;
}

/*
 * idt_install
 * Install the IDTs
 */
void idt_install(void) {
	idtp.limit = (sizeof(struct idt_entry) * 256) - 1;
	idtp.base = (uintptr_t)&idt;
	memset(&idt, 0, sizeof(struct idt_entry) * 256);

	idt_load();
}
