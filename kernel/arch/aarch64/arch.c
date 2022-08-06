/**
 * @file  kernel/arch/aarch64/arch.c
 * @brief Global functions with arch-specific implementations.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021-2022 K. Lange
 */
#include <stdint.h>
#include <kernel/process.h>
#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/spinlock.h>
#include <kernel/mmu.h>

#include <kernel/arch/aarch64/regs.h>
#include <kernel/arch/aarch64/gic.h>

/**
 * @brief Enter userspace.
 *
 * Called by process startup.
 * Does not return.
 *
 * @param entrypoint Address to "return" to in userspace.
 * @param argc       Number of arguments to provide to the new process.
 * @param argv       Argument array to pass to the new process; make sure this is user-accessible!
 * @param envp       Environment strings array
 * @param stack      Userspace stack address.
 */
void arch_enter_user(uintptr_t entrypoint, int argc, char * argv[], char * envp[], uintptr_t stack) {
	asm volatile(
		"msr ELR_EL1, %0\n" /* entrypoint */
		"msr SP_EL0, %1\n" /* stack */
		"msr SPSR_EL1, %2\n" /* EL 0 */
		::
		"r"(entrypoint), "r"(stack), "r"(0));

	register uint64_t x0 __asm__("x0") = argc;
	register uint64_t x1 __asm__("x1") = (uintptr_t)argv;
	register uint64_t x2 __asm__("x2") = (uintptr_t)envp;
	register uint64_t x4 __asm__("x4") = (uintptr_t)this_core->sp_el1;

	asm volatile(
		"mov sp, x4\n"
		"eret" :: "r"(x0), "r"(x1), "r"(x2), "r"(x4));

	__builtin_unreachable();
}

static void _kill_it(uintptr_t addr, const char * action, const char * desc, size_t size) {
	dprintf("core %d (pid=%d %s): invalid stack for signal %s (%#zx '%s' %zu)\n",
		this_core->cpu_id, this_core->current_process->id, this_core->current_process->name, action, addr, desc, size);
	task_exit(((128 + SIGSEGV) << 8) | SIGSEGV);
}

#define PUSH(stack, type, item) do { \
	stack -= sizeof(type); \
	if (!mmu_validate_user_pointer((void*)(uintptr_t)stack,sizeof(type),MMU_PTR_WRITE)) \
		_kill_it((uintptr_t)stack,"entry",#item,sizeof(type)); \
	*((volatile type *) stack) = item; \
} while (0)

#define POP(stack, type, item) do { \
	if (!mmu_validate_user_pointer((void*)(uintptr_t)stack,sizeof(type),0)) \
		_kill_it((uintptr_t)stack,"return",#item,sizeof(type)); \
	item = *((volatile type *) stack); \
	stack += sizeof(type); \
} while (0)

int arch_return_from_signal_handler(struct regs *r) {
	uintptr_t spsr;
	uintptr_t sp = r->user_sp;

	/* Restore floating point */
	POP(sp, uintptr_t, this_core->current_process->thread.context.saved[13]);
	POP(sp, uintptr_t, this_core->current_process->thread.context.saved[12]);
	for (int i = 0; i < 64; ++i) {
		POP(sp, uint64_t, this_core->current_process->thread.fp_regs[63-i]);
	}
	arch_restore_floating((process_t*)this_core->current_process);

	POP(sp, sigset_t, this_core->current_process->blocked_signals);
	long originalSignal;
	POP(sp, long, originalSignal);

	/* Interrupt system call status */
	POP(sp, long, this_core->current_process->interrupted_system_call);

	/* Process state */
	POP(sp, uintptr_t, spsr);
	this_core->current_process->thread.context.saved[11] = (spsr & 0xf0000000);
	asm volatile ("msr SPSR_EL1, %0" :: "r"(this_core->current_process->thread.context.saved[11]));
	POP(sp, uintptr_t, this_core->current_process->thread.context.saved[10]);
	asm volatile ("msr ELR_EL1, %0" :: "r"(this_core->current_process->thread.context.saved[10]));

	/* Interrupt context registers */
	POP(sp, struct regs, *r);

	asm volatile ("msr SP_EL0, %0" :: "r"(r->user_sp));
	return originalSignal;
}

/**
 * @brief Enter a userspace signal handler.
 *
 * Similar to @c arch_enter_user but also setups up magic return addresses.
 *
 * Since signal handlers do to take complicated argument arrays, this only
 * supplies a @p signum argument.
 *
 * Does not return.
 *
 * @param entrypoint Userspace address of the signal handler, set by the process.
 * @param signum     Signal number that caused this entry.
 */
void arch_enter_signal_handler(uintptr_t entrypoint, int signum, struct regs *r) {

	uintptr_t sp = (r->user_sp - 128) & 0xFFFFFFFFFFFFFFF0;

	/* Save essential registers */
	PUSH(sp, struct regs, *r);

	/* Save context that wasn't pushed above... */
	asm volatile ("mrs %0, ELR_EL1" : "=r"(this_core->current_process->thread.context.saved[10]));
	PUSH(sp, uintptr_t, this_core->current_process->thread.context.saved[10]);
	asm volatile ("mrs %0, SPSR_EL1" : "=r"(this_core->current_process->thread.context.saved[11]));
	PUSH(sp, uintptr_t, this_core->current_process->thread.context.saved[11]);

	PUSH(sp, long, this_core->current_process->interrupted_system_call);
	this_core->current_process->interrupted_system_call = 0;

	PUSH(sp, long, signum);
	PUSH(sp, sigset_t, this_core->current_process->blocked_signals);

	struct signal_config * config = (struct signal_config*)&this_core->current_process->signals[signum];
	this_core->current_process->blocked_signals |= config->mask | (config->flags & SA_NODEFER ? 0 : (1UL << signum));

	/* Save floating point */
	arch_save_floating((process_t*)this_core->current_process);
	for (int i = 0; i < 64; ++i) {
		PUSH(sp, uint64_t, this_core->current_process->thread.fp_regs[i]);
	}
	PUSH(sp, uintptr_t, this_core->current_process->thread.context.saved[12]);
	PUSH(sp, uintptr_t, this_core->current_process->thread.context.saved[13]);

	asm volatile(
		"msr ELR_EL1, %0\n" /* entrypoint */
		"msr SP_EL0, %1\n" /* stack */
		"msr SPSR_EL1, %2\n" /* spsr from context */
		::
		"r"(entrypoint),
		"r"(sp),
		"r"(0));

	register uint64_t x0 __asm__("x0") = signum;
	register uint64_t x30 __asm__("x30") = 0x8DEADBEEF;
	register uint64_t x4 __asm__("x4") = (uintptr_t)this_core->sp_el1;

	asm volatile(
		"mov sp, x4\n"
		"eret\nnop\nnop" :: "r"(x0), "r"(x30), "r"(x4));

	__builtin_unreachable();
}

/**
 * @brief Save FPU registers for this thread.
 */
void arch_restore_floating(process_t * proc) {
	asm volatile (
		"ldr q0 , [%0, #(0 * 16)]\n"
		"ldr q1 , [%0, #(1 * 16)]\n"
		"ldr q2 , [%0, #(2 * 16)]\n"
		"ldr q3 , [%0, #(3 * 16)]\n"
		"ldr q4 , [%0, #(4 * 16)]\n"
		"ldr q5 , [%0, #(5 * 16)]\n"
		"ldr q6 , [%0, #(6 * 16)]\n"
		"ldr q7 , [%0, #(7 * 16)]\n"
		"ldr q8 , [%0, #(8 * 16)]\n"
		"ldr q9 , [%0, #(9 * 16)]\n"
		"ldr q10, [%0, #(10 * 16)]\n"
		"ldr q11, [%0, #(11 * 16)]\n"
		"ldr q12, [%0, #(12 * 16)]\n"
		"ldr q13, [%0, #(13 * 16)]\n"
		"ldr q14, [%0, #(14 * 16)]\n"
		"ldr q15, [%0, #(15 * 16)]\n"
		"ldr q16, [%0, #(16 * 16)]\n"
		"ldr q17, [%0, #(17 * 16)]\n"
		"ldr q18, [%0, #(18 * 16)]\n"
		"ldr q19, [%0, #(19 * 16)]\n"
		"ldr q20, [%0, #(20 * 16)]\n"
		"ldr q21, [%0, #(21 * 16)]\n"
		"ldr q22, [%0, #(22 * 16)]\n"
		"ldr q23, [%0, #(23 * 16)]\n"
		"ldr q24, [%0, #(24 * 16)]\n"
		"ldr q25, [%0, #(25 * 16)]\n"
		"ldr q26, [%0, #(26 * 16)]\n"
		"ldr q27, [%0, #(27 * 16)]\n"
		"ldr q28, [%0, #(28 * 16)]\n"
		"ldr q29, [%0, #(29 * 16)]\n"
		"ldr q30, [%0, #(30 * 16)]\n"
		"ldr q31, [%0, #(31 * 16)]\n"
		"msr fpcr, %1\n"
		"msr fpsr, %2\n"
		::"r"(&proc->thread.fp_regs),
		"r"(proc->thread.context.saved[12]),
		"r"(proc->thread.context.saved[13])
		);
}

/**
 * @brief Restore FPU registers for this thread.
 */
void arch_save_floating(process_t * proc) {
	asm volatile (
		"str q0 , [%2, #(0 * 16)]\n"
		"str q1 , [%2, #(1 * 16)]\n"
		"str q2 , [%2, #(2 * 16)]\n"
		"str q3 , [%2, #(3 * 16)]\n"
		"str q4 , [%2, #(4 * 16)]\n"
		"str q5 , [%2, #(5 * 16)]\n"
		"str q6 , [%2, #(6 * 16)]\n"
		"str q7 , [%2, #(7 * 16)]\n"
		"str q8 , [%2, #(8 * 16)]\n"
		"str q9 , [%2, #(9 * 16)]\n"
		"str q10, [%2, #(10 * 16)]\n"
		"str q11, [%2, #(11 * 16)]\n"
		"str q12, [%2, #(12 * 16)]\n"
		"str q13, [%2, #(13 * 16)]\n"
		"str q14, [%2, #(14 * 16)]\n"
		"str q15, [%2, #(15 * 16)]\n"
		"str q16, [%2, #(16 * 16)]\n"
		"str q17, [%2, #(17 * 16)]\n"
		"str q18, [%2, #(18 * 16)]\n"
		"str q19, [%2, #(19 * 16)]\n"
		"str q20, [%2, #(20 * 16)]\n"
		"str q21, [%2, #(21 * 16)]\n"
		"str q22, [%2, #(22 * 16)]\n"
		"str q23, [%2, #(23 * 16)]\n"
		"str q24, [%2, #(24 * 16)]\n"
		"str q25, [%2, #(25 * 16)]\n"
		"str q26, [%2, #(26 * 16)]\n"
		"str q27, [%2, #(27 * 16)]\n"
		"str q28, [%2, #(28 * 16)]\n"
		"str q29, [%2, #(29 * 16)]\n"
		"str q30, [%2, #(30 * 16)]\n"
		"str q31, [%2, #(31 * 16)]\n"
		"mrs %0, fpcr\n"
		"mrs %1, fpsr\n"
		:
		"=r"(proc->thread.context.saved[12]),
		"=r"(proc->thread.context.saved[13])
		:"r"(&proc->thread.fp_regs)
		:"memory");
}

/**
 * @brief Prepare for a fatal event by stopping all other cores.
 */
void arch_fatal_prepare(void) {
	if (processor_count > 1) {
		gic_send_sgi(2,-1);
	}
}

/**
 * @brief Halt all processors, including this one.
 * @see arch_fatal_prepare
 */
void arch_fatal(void) {
	arch_fatal_prepare();
	while (1) {
		asm volatile ("wfi");
	}
}

void arch_wakeup_others(void) {
	#if 1
	for (int i = 0; i < processor_count; i++) {
		if (i == this_core->cpu_id) continue;
		if (!processor_local_data[i].current_process || (processor_local_data[i].current_process != processor_local_data[i].kernel_idle_task)) continue;
		gic_send_sgi(1,i);
	}
	#endif
}


/**
 * @brief Reboot the computer.
 *
 * At least on 'virt', there's a system control
 * register we can write to to reboot or at least
 * do a full shutdown.
 */
long arch_reboot(void) {
	return 0;
}

void aarch64_regs(struct regs *r) {
#define reg(a,b) printf(" X%02d=0x%016zx X%02d=0x%016zx\n",a,r->x ## a, b, r->x ## b)
	reg(0,1);
	reg(2,3);
	reg(4,5);
	reg(6,7);
	reg(8,9);
	reg(10,11);
	reg(12,13);
	reg(14,15);
	reg(16,17);
	reg(18,19);
	reg(20,21);
	reg(22,23);
	reg(24,25);
	reg(26,27);
	reg(28,29);
	printf(" X30=0x%016zx  SP=0x%016zx\n", r->x30, r->user_sp);
#undef reg
}

void aarch64_context(process_t * proc) {
	printf("  SP=0x%016zx BP(x29)=0x%016zx\n", proc->thread.context.sp, proc->thread.context.bp);
	printf("  IP=0x%016zx TLSBASE=0x%016zx\n", proc->thread.context.ip, proc->thread.context.tls_base);
	printf(" X19=0x%016zx     X20=%016zx\n", proc->thread.context.saved[0], proc->thread.context.saved[1]);
	printf(" X21=0x%016zx     X22=%016zx\n", proc->thread.context.saved[2], proc->thread.context.saved[3]);
	printf(" X23=0x%016zx     X24=%016zx\n", proc->thread.context.saved[4], proc->thread.context.saved[5]);
	printf(" X25=0x%016zx     X26=%016zx\n", proc->thread.context.saved[6], proc->thread.context.saved[7]);
	printf(" X27=0x%016zx     X28=%016zx\n", proc->thread.context.saved[8], proc->thread.context.saved[9]);
	printf(" ELR=0x%016zx    SPSR=%016zx\n", proc->thread.context.saved[10], proc->thread.context.saved[11]);
	printf("fpcr=0x%016zx    fpsr=%016zx\n", proc->thread.context.saved[12], proc->thread.context.saved[13]);
}

/* Syscall parameter accessors */
void arch_syscall_return(struct regs * r, long retval) { r->x0 = retval; }
long arch_syscall_number(struct regs * r) { return r->x0; }
long arch_syscall_arg0(struct regs * r)   { return r->x1; }
long arch_syscall_arg1(struct regs * r)   { return r->x2; }
long arch_syscall_arg2(struct regs * r)   { return r->x3; }
long arch_syscall_arg3(struct regs * r)   { return r->x4; }
long arch_syscall_arg4(struct regs * r)   { return r->x5; }
long arch_stack_pointer(struct regs * r)  { return r->user_sp; }
long arch_user_ip(struct regs * r)        { return r->x30; /* TODO this is wrong, this needs to come from ELR but we don't have that */ }

/* No port i/o on arm, but these are still littered around some
 * drivers we need to remove... */
unsigned short inports(unsigned short _port) { return 0; }
unsigned int inportl(unsigned short _port)   { return 0; }
unsigned char inportb(unsigned short _port)  { return 0; }
void inportsm(unsigned short port, unsigned char * data, unsigned long size) {
}

void outports(unsigned short _port, unsigned short _data) {
}

void outportl(unsigned short _port, unsigned int _data) {
}

void outportb(unsigned short _port, unsigned char _data) {
}

void outportsm(unsigned short port, unsigned char * data, unsigned long size) {
}

void arch_framebuffer_initialize(void) {
	/* I'm not sure we have any options here...
	 * lfbvideo calls this expecting it to fill in information
	 * on a preferred video mode; maybe dtb has that? */
}

char * _arch_args = NULL;
const char * arch_get_cmdline(void) {
	/* this should be available from dtb directly as a string */
	extern char * _arch_args;
	return _arch_args ? _arch_args : "";
}

const char * arch_get_loader(void) {
	return "";
}

/* These should probably assembly. */
void arch_enter_tasklet(void) {
	asm volatile (
		"ldp x0, x1, [sp], #16\n"
		"br x1\n" ::: "memory");
	__builtin_unreachable();
}

static spin_lock_t deadlock_lock = { 0 };
void _spin_panic(const char * lock_name, spin_lock_t * target) {
	arch_fatal_prepare();
	while (__sync_lock_test_and_set(deadlock_lock.latch, 0x01));
	dprintf("core %d took over five seconds waiting to acquire %s (owner=%d in %s)\n",
		this_core->cpu_id, lock_name, target->owner - 1, target->func);
	//arch_dump_traceback();
	__sync_lock_release(deadlock_lock.latch);
	arch_fatal();
}

void arch_spin_lock_acquire(const char * name, spin_lock_t * target, const char * func) {
	#if 0
	uint64_t expire = arch_perf_timer() + 5000000UL * arch_cpu_mhz();
	#endif

	/* "loss of an exclusive monitor" is one of the things that causes an "event",
	 * so we spin on wfe to try to load-acquire the latch */
	asm volatile (
		"sevl\n" /* And to avoid multiple jumps, we put the wfe first, so sevl will slide past the first one */
		"1:\n"
		"    wfe\n"
#if 0
	);

	/* Yes, we can splice these assembly snippets with the clock check, this works fine.
	 * If we've been trying to load the latch for five seconds, panic. */
	if (arch_perf_timer() > expire) {
		_spin_panic(name, target);
	}

	asm volatile (
#endif
		"2:\n"
		"   ldaxr w2, [ %1 ]\n"     /* Acquire exclusive monitor and load latch value */
		"   cbnz  w2, 1b\n"         /* If the latch value isn't 0, someone else owns the lock, go back to wfe and wait for them to release it */
		"   stxr  w2, %0, [ %1 ]\n" /* Store our core number as the latch value. */
		"   cbnz  w2, 2b\n"         /* If we failed to exclusively store, try to load again */
		::"r"(this_core->cpu_id+1),"r"(target->latch) : "x2","cc","memory");

	/* Set these for compatibility because there's at least one place we check them;
	 * TODO: just use the latch value since we're setting it to the same thing anyway? */
	target->owner = this_core->cpu_id + 1;

	/* This is purely for debugging */
	target->func  = func;
}

void arch_spin_lock_release(spin_lock_t * target) {
	/* Clear owner debug data */
	target->owner = 0;
	target->func  = NULL;

	/* Release the exclusive monitor and clear the latch value */
	__atomic_store_n(target->latch, 0, __ATOMIC_RELEASE);
}
