/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2011-2013 Kevin Lange
 * Copyright (C) 2015 Dale Weiler
 *
 * Interrupt Descriptor Tables
 *
 */
#include <system.h>
#include <logging.h>

typedef struct {
	uint16_t base_low;
	uint16_t sel;
	uint8_t zero;
	uint8_t flags;
	uint16_t base_high;
} __attribute__((packed)) idt_entry_t;

typedef struct {
	uint16_t limit;
	uintptr_t base;
} __attribute__((packed)) idt_pointer_t;

/* In the future we may need to put a lock on the access of this */
static struct {
	idt_entry_t entries[256];
	idt_pointer_t pointer;
} idt __attribute__((used));

#define ENTRY(X) (idt.entries[(X)])

typedef void (*idt_gate_t)(void);

extern void idt_load(uintptr_t);

void idt_set_gate(uint8_t num, idt_gate_t base, uint16_t sel, uint8_t flags) {
	ENTRY(num).base_low = ((uintptr_t)base & 0xFFFF);
	ENTRY(num).base_high = ((uintptr_t)base >> 16) & 0xFFFF;
	ENTRY(num).sel = sel;
	ENTRY(num).zero = 0;
	ENTRY(num).flags = flags | 0x60;
}

void idt_install(void) {
	idt_pointer_t * idtp = &idt.pointer;
	idtp->limit = sizeof idt.entries - 1;
	idtp->base = (uintptr_t)&ENTRY(0);
	memset(&ENTRY(0), 0, sizeof idt.entries);

	idt_load((uintptr_t)idtp);
}
