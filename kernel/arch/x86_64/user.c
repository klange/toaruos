/**
 * @file  kernel/arch/x86_64/user.c
 * @brief Various assembly snippets for jumping to usermode and back.
 */
#include <stdint.h>
#include <kernel/process.h>
#include <kernel/string.h>
#include <kernel/arch/x86_64/regs.h>
#include <kernel/arch/x86_64/mmu.h>
#include <kernel/arch/x86_64/ports.h>

void arch_enter_user(uintptr_t entrypoint, int argc, char * argv[], char * envp[], uintptr_t stack) {
	struct regs ret;
	ret.cs = 0x18 | 0x03;
	ret.ss = 0x20 | 0x03;
	ret.rip = entrypoint;
	ret.rflags = (1 << 21) | (1 << 9);
	ret.rsp = stack;

	asm volatile (
		"pushq %0\n"
		"pushq %1\n"
		"pushq %2\n"
		"pushq %3\n"
		"pushq %4\n"
		"swapgs\n"
		"iretq"
	: : "m"(ret.ss), "m"(ret.rsp), "m"(ret.rflags), "m"(ret.cs), "m"(ret.rip),
	    "D"(argc), "S"(argv), "d"(envp));
}

void arch_enter_signal_handler(uintptr_t entrypoint, int signum) {
	struct regs ret;
	ret.cs = 0x18 | 0x03;
	ret.ss = 0x20 | 0x03;
	ret.rip = entrypoint;
	ret.rflags = (1 << 21) | (1 << 9);
	ret.rsp = (this_core->current_process->syscall_registers->rsp - 128 - 8) & 0xFFFFFFFFFFFFFFF0; /* ensure considerable alignment */
	*(uintptr_t*)ret.rsp = 0x00000008DEADBEEF; /* arbitrarily chosen stack return sentinel IP */

	asm volatile(
		"pushq %0\n"
		"pushq %1\n"
		"pushq %2\n"
		"pushq %3\n"
		"pushq %4\n"
		"swapgs\n"
		"iretq"
	: : "m"(ret.ss), "m"(ret.rsp), "m"(ret.rflags), "m"(ret.cs), "m"(ret.rip),
	    "D"(signum));
	__builtin_unreachable();
}

__attribute__((naked))
void arch_resume_user(void) {
	asm volatile (
		"pop %r15\n"
		"pop %r14\n"
		"pop %r13\n"
		"pop %r12\n"
		"pop %r11\n"
		"pop %r10\n"
		"pop %r9\n"
		"pop %r8\n"
		"pop %rbp\n"
		"pop %rdi\n"
		"pop %rsi\n"
		"pop %rdx\n"
		"pop %rcx\n"
		"pop %rbx\n"
		"pop %rax\n"
		"add $16, %rsp\n"
		"swapgs\n"
		"iretq\n"
	);
	__builtin_unreachable();
}

static uint8_t saves[512] __attribute__((aligned(16)));
void arch_restore_floating(process_t * proc) {
	memcpy(&saves,(uint8_t *)&proc->thread.fp_regs,512);
	asm volatile ("fxrstor (%0)" :: "r"(saves));
}

void arch_save_floating(process_t * proc) {
	asm volatile ("fxsave (%0)" :: "r"(saves));
	memcpy((uint8_t *)&proc->thread.fp_regs,&saves,512);
}

void arch_pause(void) {
	asm volatile (
		"sti\n"
		"hlt\n"
		"cli\n"
	);
}

extern void lapic_send_ipi(int i, uint32_t val);
void arch_fatal(void) {
	for (int i = 0; i < processor_count; ++i) {
		if (i == this_core->cpu_id) continue;
		lapic_send_ipi(processor_local_data[i].lapic_id, 0x447D);
	}
	while (1) {
		asm volatile (
			"cli\n"
			"hlt\n"
		);
	}
}

long arch_reboot(void) {
	/* load a null page as an IDT */
	uintptr_t frame = mmu_allocate_a_frame();
	uintptr_t * idt = mmu_map_from_physical(frame << 12);
	memset(idt, 0, 0x1000);
	asm volatile (
		"lidt (%0)"
		: : "r"(idt)
	);
	uint8_t out = 0x02;
	while ((out & 0x02) != 0) {
		out = inportb(0x64);
	}
	outportb(0x64, 0xFE); /* Reset */
	return 0;
}

void arch_syscall_return(struct regs * r, long retval) { r->rax = retval; }
long arch_syscall_number(struct regs * r) { return (unsigned long)r->rax; }
long arch_syscall_arg0(struct regs * r) { return r->rbx; }
long arch_syscall_arg1(struct regs * r) { return r->rcx; }
long arch_syscall_arg2(struct regs * r) { return r->rdx; }
long arch_syscall_arg3(struct regs * r) { return r->rsi; }
long arch_syscall_arg4(struct regs * r) { return r->rdi; }
long arch_stack_pointer(struct regs * r) { return r->rsp; }
long arch_user_ip(struct regs * r) { return r->rip; }
