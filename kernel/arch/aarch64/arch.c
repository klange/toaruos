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

#include <kernel/arch/aarch64/regs.h>

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

	asm volatile(
		"eret" :: "r"(x0), "r"(x1), "r"(x2));

	__builtin_unreachable();
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
void arch_enter_signal_handler(uintptr_t entrypoint, int signum) {
	asm volatile(
		"msr ELR_EL1, %0\n" /* entrypoint */
		"msr SP_EL0, %1\n" /* stack */
		"msr SPSR_EL1, %2\n" /* spsr from context */
		::
		"r"(entrypoint),
		"r"((this_core->current_process->syscall_registers->user_sp - 128) & 0xFFFFFFFFFFFFFFF0),
		"r"(this_core->current_process->thread.context.saved[11]));

	register uint64_t x0 __asm__("x0") = signum;
	register uint64_t x30 __asm__("x30") = 0x8DEADBEEF;

	asm volatile(
		"eret" :: "r"(x0), "r"(x30));

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
		::"r"(&proc->thread.fp_regs));
}

/**
 * @brief Restore FPU registers for this thread.
 */
void arch_save_floating(process_t * proc) {
	asm volatile (
		"str q0 , [%0, #(0 * 16)]\n"
		"str q1 , [%0, #(1 * 16)]\n"
		"str q2 , [%0, #(2 * 16)]\n"
		"str q3 , [%0, #(3 * 16)]\n"
		"str q4 , [%0, #(4 * 16)]\n"
		"str q5 , [%0, #(5 * 16)]\n"
		"str q6 , [%0, #(6 * 16)]\n"
		"str q7 , [%0, #(7 * 16)]\n"
		"str q8 , [%0, #(8 * 16)]\n"
		"str q9 , [%0, #(9 * 16)]\n"
		"str q10, [%0, #(10 * 16)]\n"
		"str q11, [%0, #(11 * 16)]\n"
		"str q12, [%0, #(12 * 16)]\n"
		"str q13, [%0, #(13 * 16)]\n"
		"str q14, [%0, #(14 * 16)]\n"
		"str q15, [%0, #(15 * 16)]\n"
		"str q16, [%0, #(16 * 16)]\n"
		"str q17, [%0, #(17 * 16)]\n"
		"str q18, [%0, #(18 * 16)]\n"
		"str q19, [%0, #(19 * 16)]\n"
		"str q20, [%0, #(20 * 16)]\n"
		"str q21, [%0, #(21 * 16)]\n"
		"str q22, [%0, #(22 * 16)]\n"
		"str q23, [%0, #(23 * 16)]\n"
		"str q24, [%0, #(24 * 16)]\n"
		"str q25, [%0, #(25 * 16)]\n"
		"str q26, [%0, #(26 * 16)]\n"
		"str q27, [%0, #(27 * 16)]\n"
		"str q28, [%0, #(28 * 16)]\n"
		"str q29, [%0, #(29 * 16)]\n"
		"str q30, [%0, #(30 * 16)]\n"
		"str q31, [%0, #(31 * 16)]\n"
		::"r"(&proc->thread.fp_regs):"memory");
}

/**
 * @brief Prepare for a fatal event by stopping all other cores.
 */
void arch_fatal_prepare(void) {
	/* TODO Stop other cores */
}

/**
 * @brief Halt all processors, including this one.
 * @see arch_fatal_prepare
 */
void arch_fatal(void) {
	arch_fatal_prepare();
	printf("-- fatal panic\n");
	/* TODO */
	while (1) {}
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

