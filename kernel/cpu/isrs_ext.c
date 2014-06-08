/* vim: tabstop=4 shiftwidth=4 noexpandtab
*/
/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2014 Kevin Lange
 */
#include <system.h>
#include <logging.h>

/*
 * Exception Handlers
 */
extern void _isr0 (void);
extern void _isr1 (void);
extern void _isr2 (void);
extern void _isr3 (void);
extern void _isr4 (void);
extern void _isr5 (void);
extern void _isr6 (void);
extern void _isr7 (void);
extern void _isr8 (void);
extern void _isr9 (void);
extern void _isr10(void);
extern void _isr11(void);
extern void _isr12(void);
extern void _isr13(void);
extern void _isr14(void);
extern void _isr15(void);
extern void _isr16(void);
extern void _isr17(void);
extern void _isr18(void);
extern void _isr19(void);
extern void _isr20(void);
extern void _isr21(void);
extern void _isr22(void);
extern void _isr23(void);
extern void _isr24(void);
extern void _isr25(void);
extern void _isr26(void);
extern void _isr27(void);
extern void _isr28(void);
extern void _isr29(void);
extern void _isr30(void);
extern void _isr31(void);
extern void _isr127(void);

irq_handler_t isrs_routines[256] = { NULL };

void isrs_install_handler(size_t isrs, irq_handler_t handler) {
	isrs_routines[isrs] = handler;
}

void isrs_uninstall_handler(size_t isrs) {
	isrs_routines[isrs] = 0;
}

void isrs_install(void) {
	/* Exception Handlers */
	memset(isrs_routines, 0x00, sizeof(isrs_routines));
	idt_set_gate(0,  _isr0,  0x08, 0x8E);
	idt_set_gate(1,  _isr1,  0x08, 0x8E);
	idt_set_gate(2,  _isr2,  0x08, 0x8E);
	idt_set_gate(3,  _isr3,  0x08, 0x8E);
	idt_set_gate(4,  _isr4,  0x08, 0x8E);
	idt_set_gate(5,  _isr5,  0x08, 0x8E);
	idt_set_gate(6,  _isr6,  0x08, 0x8E);
	idt_set_gate(7,  _isr7,  0x08, 0x8E);
	idt_set_gate(8,  _isr8,  0x08, 0x8E);
	idt_set_gate(9,  _isr9,  0x08, 0x8E);
	idt_set_gate(10, _isr10, 0x08, 0x8E);
	idt_set_gate(11, _isr11, 0x08, 0x8E);
	idt_set_gate(12, _isr12, 0x08, 0x8E);
	idt_set_gate(13, _isr13, 0x08, 0x8E);
	idt_set_gate(14, _isr14, 0x08, 0x8E);
	idt_set_gate(15, _isr15, 0x08, 0x8E);
	idt_set_gate(16, _isr16, 0x08, 0x8E);
	idt_set_gate(17, _isr17, 0x08, 0x8E);
	idt_set_gate(18, _isr18, 0x08, 0x8E);
	idt_set_gate(19, _isr19, 0x08, 0x8E);
	idt_set_gate(20, _isr20, 0x08, 0x8E);
	idt_set_gate(21, _isr21, 0x08, 0x8E);
	idt_set_gate(22, _isr22, 0x08, 0x8E);
	idt_set_gate(23, _isr23, 0x08, 0x8E);
	idt_set_gate(24, _isr24, 0x08, 0x8E);
	idt_set_gate(25, _isr25, 0x08, 0x8E);
	idt_set_gate(26, _isr26, 0x08, 0x8E);
	idt_set_gate(27, _isr27, 0x08, 0x8E);
	idt_set_gate(28, _isr28, 0x08, 0x8E);
	idt_set_gate(29, _isr29, 0x08, 0x8E);
	idt_set_gate(30, _isr30, 0x08, 0x8E);
	idt_set_gate(31, _isr31, 0x08, 0x8E);
	idt_set_gate(SYSCALL_VECTOR, _isr127, 0x08, 0x8E);
}

static const char *exception_messages[32] = {
	"Division by zero",				/* 0 */
	"Debug",
	"Non-maskable interrupt",
	"Breakpoint",
	"Detected overflow",
	"Out-of-bounds",				/* 5 */
	"Invalid opcode",
	"No coprocessor",
	"Double fault",
	"Coprocessor segment overrun",
	"Bad TSS",						/* 10 */
	"Segment not present",
	"Stack fault",
	"General protection fault",
	"Page fault",
	"Unknown interrupt",			/* 15 */
	"Coprocessor fault",
	"Alignment check",
	"Machine check",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved"
};

void fault_error(struct regs *r) {
	debug_print(CRITICAL, "Unhandled exception: [%d] %s", r->int_no, exception_messages[r->int_no]);
	HALT_AND_CATCH_FIRE("Process caused an unhandled exception", r);
	STOP;
}

