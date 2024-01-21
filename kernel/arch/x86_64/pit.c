/**
 * @file kernel/arch/x86_64/pit.c
 * @author K. Lange
 * @brief Legacy x86 Programmable Interrupt Timer
 *
 * The PIT is used as a fallback preempt source if the LAPIC can
 * not be configured. The preempt signal is 100Hz.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021-2024 K. Lange
 */
#include <kernel/printf.h>
#include <kernel/process.h>
#include <kernel/arch/x86_64/ports.h>
#include <kernel/arch/x86_64/irq.h>
#include <kernel/arch/x86_64/regs.h>

/* Programmable interval timer */
#define PIT_A 0x40
#define PIT_B 0x41
#define PIT_C 0x42
#define PIT_CONTROL 0x43

#define PIT_MASK 0xFF
#define PIT_SCALE 1193180
#define PIT_SET 0x34

#define TIMER_IRQ 0

#define RESYNC_TIME 1

/**
 * @brief Set the phase of the PIT in Hz.
 *
 * @param hz Ticks per second.
 */
static void pit_set_timer_phase(long hz) {
	long divisor = PIT_SCALE / hz;
	outportb(PIT_CONTROL, PIT_SET);
	outportb(PIT_A, divisor & PIT_MASK);
	outportb(PIT_A, (divisor >> 8) & PIT_MASK);
}

/**
 * @brief Interrupt handler for the PIT.
 */
int pit_interrupt(struct regs *r) {
	extern void arch_update_clock(void);
	arch_update_clock();

	irq_ack(0);

	if (r->cs == 0x08) return 1;

	switch_task(1);
	return 1;
}

/**
 * @brief Install an interrupt handler for, and turn on, the PIT.
 */
void pit_initialize(void) {
	irq_install_handler(TIMER_IRQ, pit_interrupt, "pit timer");

	/* ELCR? */
	uint8_t val = inportb(0x4D1);
	outportb(0x4D1, val | (1 << (10-8)) | (1 << (11-8)));

	/* Enable PIT */
	pit_set_timer_phase(100);
}
