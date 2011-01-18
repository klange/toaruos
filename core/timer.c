#include <system.h>

void
timer_phase(
		int hz
		) {
	int divisor = 1193180 / hz;
	outportb(0x43, 0x36);
	outportb(0x40, divisor & 0xFF);
	outportb(0x40, divisor >> 8);
}

long timer_ticks = 0;
unsigned long ticker = 0;

void
timer_handler(
		struct regs *r
		) {
	++timer_ticks;
	if (timer_ticks % 18 == 0) {
		ticker++;
		puts ("Tick. ");
		if     (ticker % 4 == 0) { putch('|'); }
		else if(ticker % 4 == 1) { putch('/'); }
		else if(ticker % 4 == 2) { putch('-'); }
		else if(ticker % 4 == 3) { putch('\\'); }
		putch('\n');
	}
}

void timer_install() {
	irq_install_handler(0, timer_handler);
}

void
timer_wait(
		int ticks
		) {
	long eticks;
	eticks = (long)timer_ticks + (long)ticks;
	while(timer_ticks < eticks);
}
