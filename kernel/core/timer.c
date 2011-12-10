/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Programmable Interrupt Timer
 */
#include <system.h>

#define PIT_A 0x40
#define PIT_B 0x41
#define PIT_C 0x42
#define PIT_CONTROL 0x43

#define PIT_MASK 0xFF
#define PIT_SCALE 1193180
#define PIT_SET 0x36

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
long timer_ticks = 0;
unsigned long ticker = 0;

/*
 * IRQ handler for when the timer fires
 */
void
timer_handler(
		struct regs *r
		) {
	++timer_ticks;
	switch_task();
}

/*
 * Device installer for the PIT
 */
void timer_install() {
	irq_install_handler(0, timer_handler);
	timer_phase(100); /* 100Hz */
}

/*
 * Wait until `ticks` calls to the timer
 * handler have happened, then resume execution.
 */
void
timer_wait(
		int ticks
		) {
	/* end tick count */
	long eticks;
	eticks = (long)timer_ticks + (long)ticks;
	while(timer_ticks < eticks) {
		/* Halt for interrupt */
		PAUSE;
	}
}
