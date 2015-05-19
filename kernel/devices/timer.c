/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2011-2013 Kevin Lange
 *
 * Programmable Interrupt Timer
 */
#include <system.h>
#include <logging.h>
#include <process.h>

#define PIT_A 0x40
#define PIT_B 0x41
#define PIT_C 0x42
#define PIT_CONTROL 0x43

#define PIT_MASK 0xFF
#define PIT_SCALE 1193180
#define PIT_SET 0x36

#define TIMER_IRQ 0

#define SUBTICKS_PER_TICK 1000
#define RESYNC_TIME 1

/*
 * Set the phase (in hertz) for the Programmable
 * Interrupt Timer (PIT).
 */
void
timer_phase(
		int hz
		) {
	int divisor = PIT_SCALE / hz;
	outportb(PIT_CONTROL, PIT_SET);
	outportb(PIT_A, divisor & PIT_MASK);
	outportb(PIT_A, (divisor >> 8) & PIT_MASK);
}

/*
 * Internal timer counters
 */
unsigned long timer_ticks = 0;
unsigned long timer_subticks = 0;
signed long timer_drift = 0;
signed long _timer_drift = 0;

static int behind = 0;

/*
 * IRQ handler for when the timer fires
 */
int timer_handler(struct regs *r) {
	if (++timer_subticks == SUBTICKS_PER_TICK || (behind && ++timer_subticks == SUBTICKS_PER_TICK)) {
		timer_ticks++;
		timer_subticks = 0;
		if (timer_ticks % RESYNC_TIME == 0) {
			uint32_t new_time = read_cmos();
			_timer_drift = new_time - boot_time - timer_ticks;
			if (_timer_drift > 0) behind = 1;
			else behind = 0;
		}
	}
	irq_ack(TIMER_IRQ);

	wakeup_sleepers(timer_ticks, timer_subticks);
	switch_task(1);
	return 1;
}

void relative_time(unsigned long seconds, unsigned long subseconds, unsigned long * out_seconds, unsigned long * out_subseconds) {
	if (subseconds + timer_subticks > SUBTICKS_PER_TICK) {
		*out_seconds    = timer_ticks + seconds + 1;
		*out_subseconds = (subseconds + timer_subticks) - SUBTICKS_PER_TICK;
	} else {
		*out_seconds    = timer_ticks + seconds;
		*out_subseconds = timer_subticks + subseconds;
	}
}

/*
 * Device installer for the PIT
 */
void timer_install(void) {
	debug_print(NOTICE,"Initializing interval timer");
	boot_time = read_cmos();
	irq_install_handler(TIMER_IRQ, timer_handler);
	timer_phase(SUBTICKS_PER_TICK); /* 100Hz */
}

