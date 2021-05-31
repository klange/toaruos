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

static void pit_set_timer_phase(long hz) {
	long divisor = PIT_SCALE / hz;
	outportb(PIT_CONTROL, PIT_SET);
	outportb(PIT_A, divisor & PIT_MASK);
	outportb(PIT_A, (divisor >> 8) & PIT_MASK);
}

extern int cmos_time_stuff(struct regs *r);

void pit_initialize(void) {
	irq_install_handler(TIMER_IRQ, cmos_time_stuff, "pit timer");

	/* ELCR? */
	uint8_t val = inportb(0x4D1);
	outportb(0x4D1, val | (1 << (10-8)) | (1 << (11-8)));

	/* Enable PIT */
	pit_set_timer_phase(100); /* 1000 Hz */
}
