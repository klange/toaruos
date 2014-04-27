#include <module.h>
#include <printf.h>
#include <mod/shell.h>

static void note(int length, int freq) {

	uint32_t div = 11931800 / freq;
	uint8_t  t;

	outportb(0x43, 0xb6);
	outportb(0x42, (uint8_t)(div));
	outportb(0x42, (uint8_t)(div >> 8));

	t = inportb(0x61);
	outportb(0x61, t | 0x3);

	unsigned long s, ss;
	relative_time(0, length, &s, &ss);
	sleep_until((process_t *)current_process, s, ss);
	switch_task(0);

	t = inportb(0x61) & 0xFC;
	outportb(0x61, t);

}

DEFINE_SHELL_FUNCTION(beep, "Beep.") {
	fprintf(tty, "beep\n");

	note(20, 15680);
	note(10, 11747);
	note(10, 12445);
	note(20, 13969);
	note(10, 12445);
	note(10, 11747);
	note(20, 10465);
	note(10, 10465);
	note(10, 12445);
	note(20, 15680);
	note(10, 13969);
	note(10, 12445);
	note(30, 11747);
	note(10, 12445);
	note(20, 13969);
	note(20, 15680);
	note(20, 12445);
	note(20, 10465);
	note(20, 10465);

	return 0;
}

static int init(void) {
	BIND_SHELL_FUNCTION(beep);
	return 0;
}

static int fini(void) {
	return 0;
}

MODULE_DEF(pcspkr, init, fini);
MODULE_DEPENDS(debugshell);
