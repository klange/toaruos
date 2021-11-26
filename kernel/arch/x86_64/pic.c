/**
 * @file  kernel/arch/x86_64/pic.c
 * @brief Legacy PIC support.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021 K. Lange
 */
#include <kernel/printf.h>
#include <kernel/process.h>
#include <kernel/arch/x86_64/ports.h>
#include <kernel/arch/x86_64/irq.h>
#include <kernel/arch/x86_64/regs.h>

/* Programmable interrupt controller */
#define PIC1           0x20
#define PIC1_COMMAND   PIC1
#define PIC1_OFFSET    0x20
#define PIC1_DATA      (PIC1+1)

#define PIC2           0xA0
#define PIC2_COMMAND   PIC2
#define PIC2_OFFSET    0x28
#define PIC2_DATA      (PIC2+1)

#define PIC_EOI        0x20

#define ICW1_ICW4      0x01
#define ICW1_INIT      0x10

#define PIC_WAIT() \
	do { \
		/* May be fragile */ \
		asm volatile("jmp 1f\n\t" \
		             "1:\n\t" \
		             "    jmp 2f\n\t" \
		             "2:"); \
	} while (0)

static void irq_remap(void) {
	/* Cascade initialization */
	outportb(PIC1_COMMAND, ICW1_INIT|ICW1_ICW4); PIC_WAIT();
	outportb(PIC2_COMMAND, ICW1_INIT|ICW1_ICW4); PIC_WAIT();

	/* Remap */
	outportb(PIC1_DATA, PIC1_OFFSET); PIC_WAIT();
	outportb(PIC2_DATA, PIC2_OFFSET); PIC_WAIT();

	/* Cascade identity with slave PIC at IRQ2 */
	outportb(PIC1_DATA, 0x04); PIC_WAIT();
	outportb(PIC2_DATA, 0x02); PIC_WAIT();

	/* Request 8086 mode on each PIC */
	outportb(PIC1_DATA, 0x01); PIC_WAIT();
	outportb(PIC2_DATA, 0x01); PIC_WAIT();
}

void irq_ack(size_t irq_no) {
	if (irq_no >= 8) {
		outportb(PIC2_COMMAND, PIC_EOI);
	}
	outportb(PIC1_COMMAND, PIC_EOI);
}

void pic_initialize(void) {
	irq_remap();
}

