/**
 * @file kernel/arch/x86_64/pit.c
 * @author K. Lange
 * @brief Legacy x86 Programmable Interrupt Timer
 *
 * Trusty old timer chip that still exists, and is still somehow
 * the only reliable to measure subsecond wallclock times.
 *
 * We continue to the use the PIT as the BSP timer interrupt source,
 * and we also use it as part of timer calibration for TSCs, which
 * is then used to calibrate LAPIC timers.
 *
 * Our main tick rate is 100Hz. We use periodic modes, so this
 * doesn't equate to 1/100s worth of CPU time per process before
 * it gets switched out, rather something less usually, but it
 * does mean we don't need to care about resetting timers or
 * even knowing which timer triggered a userspace pre-emption, since
 * APs use their LAPIC timers (which we also try to set to 100Hz).
 *
 * The actual time doesn't matter, as we don't use the PIT as a real
 * timing source after initialization of the TSC. 100Hz just feels nice?
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

	switch_task(1);
	asm volatile (
		".global _ret_from_preempt_source\n"
		"_ret_from_preempt_source:"
	);
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
