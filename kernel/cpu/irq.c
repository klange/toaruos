/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2011-2014 Kevin Lange
 * Copyright (C) 2015 Dale Weiler
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

static void (*irqs[])(void) = {
	_irq0, _irq1, _irq2,  _irq3,  _irq4,  _irq5,  _irq6,  _irq7,
	_irq8, _irq9, _irq10, _irq11, _irq12, _irq13, _irq14, _irq15
};

#define IRQ_CHAIN_SIZE (sizeof(irqs)/sizeof(*irqs))
#define IRQ_CHAIN_DEPTH 4

#define SYNC_CLI() asm volatile("cli")
#define SYNC_STI() asm volatile("sti")

static irq_handler_chain_t irq_routines[IRQ_CHAIN_SIZE * IRQ_CHAIN_DEPTH] = { NULL };

void irq_install_handler(size_t irq, irq_handler_chain_t handler) {
	/* Disable interrupts when changing handlers */
	SYNC_CLI();
	for (size_t i = 0; i < IRQ_CHAIN_DEPTH; i++) {
		if (irq_routines[i * IRQ_CHAIN_SIZE + irq])
			continue;
		irq_routines[i * IRQ_CHAIN_SIZE + irq] = handler;
		break;
	}
	SYNC_STI();
}


void irq_uninstall_handler(size_t irq) {
	/* Disable interrupts when changing handlers */
	SYNC_CLI();
	for (size_t i = 0; i < IRQ_CHAIN_DEPTH; i++)
		irq_routines[i * IRQ_CHAIN_SIZE + irq] = NULL;
	SYNC_STI();
}


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

static void irq_setup_gates(void) {
	for (size_t i = 0; i < IRQ_CHAIN_SIZE; i++)
		idt_set_gate(32 + i, irqs[i], 0x08, 0x8E);
}

void irq_install(void) {
	irq_remap();
	irq_setup_gates();
}

/* TODO: Clean up everything below */
void irq_ack(size_t irq_no) {
	if (irq_no >= 8) {
		outportb(0xA0, 0x20);
	}
	outportb(0x20, 0x20);
}

void irq_handler(struct regs *r) {
	IRQ_OFF;
	if (r->int_no > 47 || r->int_no < 32) {
		IRQ_RES;
		return;
	}
	for (size_t i = 0; i < IRQ_CHAIN_DEPTH; i++) {
		irq_handler_chain_t handler = irq_routines[i * IRQ_CHAIN_SIZE + (r->int_no - 32)];
		if (handler && handler(r)) {
			goto _done;
		}
	}
	irq_ack(r->int_no - 32);
_done:
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
