/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2011-2014 Kevin Lange
 *
 * Interrupt Requests
 *
 */
#include <system.h>
#include <logging.h>

extern void _irq0(void);
extern void _irq1(void);
extern void _irq2(void);
extern void _irq3(void);
extern void _irq4(void);
extern void _irq5(void);
extern void _irq6(void);
extern void _irq7(void);
extern void _irq8(void);
extern void _irq9(void);
extern void _irq10(void);
extern void _irq11(void);
extern void _irq12(void);
extern void _irq13(void);
extern void _irq14(void);
extern void _irq15(void);

static irq_handler_t irq_routines[16] = { NULL };

/*
 * Install an interupt handler for a hardware device.
 */
void irq_install_handler(size_t irq, irq_handler_t handler) {
	irq_routines[irq] = handler;
}

/*
 * Remove an interrupt handler for a hardware device.
 */
void irq_uninstall_handler(size_t irq) {
	irq_routines[irq] = 0;
}

/*
 * Check to see if an interrupt handler is occupied.
 *
 * The proper solution here would probably be to have shared IRQs.
 */
int irq_is_handler_free(size_t irq) {
	return !irq_routines[irq];
}

/*
 * Remap interrupt handlers
 */
void irq_remap(void) {
	outportb(0x20, 0x11);
	outportb(0xA0, 0x11);
	outportb(0x21, 0x20);
	outportb(0xA1, 0x28);
	outportb(0x21, 0x04);
	outportb(0xA1, 0x02);
	outportb(0x21, 0x01);
	outportb(0xA1, 0x01);
	outportb(0x21, 0x0);
	outportb(0xA1, 0x0);
}

void irq_gates(void) {
	idt_set_gate(32, _irq0, 0x08, 0x8E);
	idt_set_gate(33, _irq1, 0x08, 0x8E);
	idt_set_gate(34, _irq2, 0x08, 0x8E);
	idt_set_gate(35, _irq3, 0x08, 0x8E);
	idt_set_gate(36, _irq4, 0x08, 0x8E);
	idt_set_gate(37, _irq5, 0x08, 0x8E);
	idt_set_gate(38, _irq6, 0x08, 0x8E);
	idt_set_gate(39, _irq7, 0x08, 0x8E);
	idt_set_gate(40, _irq8, 0x08, 0x8E);
	idt_set_gate(41, _irq9, 0x08, 0x8E);
	idt_set_gate(42, _irq10, 0x08, 0x8E);
	idt_set_gate(43, _irq11, 0x08, 0x8E);
	idt_set_gate(44, _irq12, 0x08, 0x8E);
	idt_set_gate(45, _irq13, 0x08, 0x8E);
	idt_set_gate(46, _irq14, 0x08, 0x8E);
	idt_set_gate(47, _irq15, 0x08, 0x8E);
}

/*
 * Set up interrupt handler for hardware devices.
 */
void irq_install(void) {
	irq_remap();
	irq_gates();
	IRQ_RES;
}

void irq_ack(size_t irq_no) {
	if (irq_no >= 8) {
		outportb(0xA0, 0x20);
	}
	outportb(0x20, 0x20);
}

void irq_handler(struct regs *r) {
	IRQ_OFF;
	void (*handler)(struct regs *r);
	if (r->int_no > 47 || r->int_no < 32) {
		handler = NULL;
	} else {
		handler = irq_routines[r->int_no - 32];
	}
	if (handler) {
		handler(r);
	} else {
		irq_ack(r->int_no - 32);
	}
	IRQ_RES;
}

static int _irq_sem = 0;
void irq_off(void) {
	uint32_t eflags;
	asm volatile(
		"pushf\n"
		"pop %0\n"
		"cli"
		: "=r" (eflags));

	if (eflags & (1 << 9)) {
		_irq_sem = 1;
	} else {
		_irq_sem++;
	}
}

void irq_res(void) {
	if (_irq_sem == 0) {
		asm volatile ("sti");
		return;
	}
	_irq_sem--;
	if (_irq_sem == 0) {
		asm volatile ("sti");
	}
}

void irq_on(void) {
	_irq_sem = 0;
	asm volatile ("sti");
}
