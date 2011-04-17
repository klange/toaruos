#include <system.h>

void kernel_halt() {
	settextcolor(4,15);
	kprintf("\n \x13 System Halted!\n\n");

	while (1) {
		__asm__ __volatile__ ("cli");
		__asm__ __volatile__ ("hlt");
	}
}

void halt_and_catch_fire(char * error_message, const char * file, int line, struct regs * regs) {
	__asm__ __volatile__("cli");
	settextcolor(0,11);
	kprintf("Process %d did a dumb.\n", getpid());
	kprintf("PANIC! %s\n", error_message);
	kprintf("File: %s\n", file);
	kprintf("Line: %d\n", line);
	if (regs) {
		kprintf("Registers at interrupt:\n");
		kprintf("eax=0x%x ebx=0x%x\n", regs->eax, regs->ebx);
		kprintf("ecx=0x%x edx=0x%x\n", regs->ecx, regs->edx);
		kprintf("esp=0x%x ebp=0x%x\n", regs->esp, regs->ebp);
		kprintf("Error code: 0x%x\n",  regs->err_code);
		kprintf("EFLAGS:     0x%x\n",  regs->eflags);
		kprintf("User ESP:   0x%x\n",  regs->useresp);
		kprintf("eip=0x%x\n",          regs->eip);
	}
	kprintf("Killing process...\n");
	resettextcolor();
	kexit(0);
}

void assert_failed(const char *file, uint32_t line, const char *desc) {
	__asm__ __volatile__("cli");
	settextcolor(14,3);
	kprintf("Kernel Assertion Failed: %s\n", desc);
	kprintf("File: %s\n", file);
	kprintf("Line: %d\n", line);
	kernel_halt();
}
