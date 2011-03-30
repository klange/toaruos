/*
 * vim:tabstop=4
 * vim:noexpandtab
 */
#include <system.h>

static int print(char * s) {
	kprintf(s);
	return 2;
}

static void syscall_handler(struct regs * r);
static uintptr_t syscalls[] = {
	/* System Call Table */
	(uintptr_t)&print
};
uint32_t num_syscalls = 1;

void
syscalls_install() {
	isrs_install_handler(0x7F, &syscall_handler);
}

void
syscall_handler(
		struct regs * r
		) {
	kprintf("[syscall] syscall\n");
	if (r->eax >= num_syscalls) {
		return;
	}
	uintptr_t location = syscalls[r->eax];

	uint32_t ret;
	__asm__ __volatile__ (
			"push %1\n"
			"push %2\n"
			"push %3\n"
			"push %4\n"
			"push %5\n"
			"call *%6\n"
			"pop %%ebx\n"
			"pop %%ebx\n"
			"pop %%ebx\n"
			"pop %%ebx\n"
			"pop %%ebx\n"
			: "=a" (ret) : "r" (r->edi), "r" (r->esi), "r" (r->edx), "r" (r->ecx), "r" (r->ebx), "r" (location));
	r->eax = ret;
}

//DEFN_SYSCALL1(print, 0, const char *)
int syscall_print(const char * p1) {
	int a = 0xA5ADFACE;
	__asm__ __volatile__("int $0x7F" : "=a" (a) : "0" (0), "b" ((int)p1));
	return a;
}
