#include <system.h>

void halt_and_catch_fire(char * error_message, const char * file, int line) {
	__asm__ __volatile__("cli");
	settextcolor(14,4);
	kprintf("PANIC! %s\n", error_message);
	kprintf("File: %s\n", file);
	kprintf("Line: %d\n", line);
	for (;;);
}

void assert_failed(const char *file, uint32_t line, const char *desc) {
	__asm__ __volatile__("cli");
	settextcolor(14,4);
	kprintf("ASSERTION FAILED! %s\n", desc);
	kprintf("File: %s\n", file);
	kprintf("Line: %d\n", line);
	for (;;);
}
