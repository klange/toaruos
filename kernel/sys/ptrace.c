/**
 * @file kernel/sys/ptrace.c
 * @brief Process tracing functions
 *
 * Provides single stepping, cross-process memory inspection,
 * regiser inspection, poking, and syscall trace events.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021 K. Lange
 */
#include <stdint.h>
#include <errno.h>
#include <sys/ptrace.h>
#include <kernel/printf.h>
#include <kernel/process.h>
#include <kernel/string.h>
#include <kernel/signal.h>
#include <kernel/syscall.h>
#include <kernel/ptrace.h>
#include <kernel/args.h>
#include <kernel/mmu.h>

#if defined(__x86_64__)
#include <kernel/arch/x86_64/regs.h>
#elif defined(__aarch64__)
#include <kernel/arch/aarch64/regs.h>
#else
#error "no regs"
#endif

static void _ptrace_trace(process_t * tracer, process_t * tracee) {
	spin_lock(tracer->wait_lock);
	__sync_or_and_fetch(&tracee->flags, (PROC_FLAG_TRACE_SYSCALLS | PROC_FLAG_TRACE_SIGNALS));

	if (!tracer->tracees) {
		tracer->tracees = list_create("debug tracees", tracer);
	}

	list_insert(tracer->tracees, tracee);

	tracee->tracer = tracer->id;

	spin_unlock(tracer->wait_lock);
}

long ptrace_attach(pid_t pid) {
	process_t * tracer = (process_t *)this_core->current_process;
	process_t * tracee = process_from_pid(pid);
	if (!tracee) return -ESRCH;
	if (tracer->user != 0 && tracer->user != tracee->user) return -EPERM;

	_ptrace_trace(tracer, tracee);

	return 0;
}

long ptrace_self(void) {
	process_t * tracee = (process_t*)this_core->current_process;
	process_t * tracer = process_get_parent(tracee);
	if (!tracer) return -EINVAL;

	_ptrace_trace(tracer, tracee);

	return 0;
}

/**
 * @brief Trigger a ptrace event on the currently executing thread.
 */
long ptrace_signal(int signal, int reason) {
	this_core->current_process->status = 0x7F | (signal << 8) | (reason << 16);
	__sync_or_and_fetch(&this_core->current_process->flags, PROC_FLAG_SUSPENDED);

	process_t * parent = process_from_pid(this_core->current_process->tracer);
	if (parent && !(parent->flags & PROC_FLAG_FINISHED)) {
		spin_lock(parent->wait_lock);
		wakeup_queue(parent->wait_queue);
		spin_unlock(parent->wait_lock);
	}
	switch_task(0);

	int signum = (this_core->current_process->status >> 8);
	this_core->current_process->status = 0x7F;
	return signum;
}

long ptrace_continue(pid_t pid, int sig) {
	process_t * tracee = process_from_pid(pid);
	if (!tracee || (tracee->tracer != this_core->current_process->id) || !(tracee->flags & PROC_FLAG_SUSPENDED)) return -ESRCH;

	/* Unsuspend */
	__sync_and_and_fetch(&tracee->flags, ~(PROC_FLAG_SUSPENDED));
	tracee->status = (sig << 8);
	make_process_ready(tracee);

	return 0;
}

long ptrace_getregs(pid_t pid, void * data) {
	if (!data || ptr_validate(data, "ptrace")) return -EFAULT;
	process_t * tracee = process_from_pid(pid);
	if (!tracee || (tracee->tracer != this_core->current_process->id) || !(tracee->flags & PROC_FLAG_SUSPENDED)) return -ESRCH;

	/* Copy registers */
	memcpy(data, tracee->interrupt_registers ? tracee->interrupt_registers : tracee->syscall_registers, sizeof(struct regs));
#ifdef __aarch64__
	memcpy((char*)data + sizeof(struct regs), &tracee->thread.context.saved[10], sizeof(uintptr_t));
#endif

	return 0;
}

long ptrace_peek(pid_t pid, void * addr, void * data) {
	if (!data || ptr_validate(data, "ptrace")) return -EFAULT;
	process_t * tracee = process_from_pid(pid);
	if (!tracee || (tracee->tracer != this_core->current_process->id) || !(tracee->flags & PROC_FLAG_SUSPENDED)) return -ESRCH;

	union PML * page_entry = mmu_get_page_other(tracee->thread.page_directory->directory, (uintptr_t)addr);

	if (!page_entry) return -EFAULT;
	if (!mmu_page_is_user_readable(page_entry)) return -EFAULT;

	uintptr_t mapped_address = mmu_map_to_physical(tracee->thread.page_directory->directory, (uintptr_t)addr);

	if ((intptr_t)mapped_address < 0 && (intptr_t)mapped_address > -10) return -EFAULT;

	uintptr_t blarg = (uintptr_t)mmu_map_from_physical(mapped_address);

	/* Yeah, uh, one byte. That works. */
	*(char*)data = *(char*)blarg;

	return 0;
}

long ptrace_poke(pid_t pid, void * addr, void * data) {
	if (!data || ptr_validate(data, "ptrace")) return -EFAULT;
	process_t * tracee = process_from_pid(pid);
	if (!tracee || (tracee->tracer != this_core->current_process->id) || !(tracee->flags & PROC_FLAG_SUSPENDED)) return -ESRCH;

	union PML * page_entry = mmu_get_page_other(tracee->thread.page_directory->directory, (uintptr_t)addr);

	if (!page_entry) return -EFAULT;
	if (!mmu_page_is_user_writable(page_entry)) return -EFAULT;

	uintptr_t mapped_address = mmu_map_to_physical(tracee->thread.page_directory->directory, (uintptr_t)addr);

	if ((intptr_t)mapped_address < 0 && (intptr_t)mapped_address > -10) return -EFAULT;

	uintptr_t blarg = (uintptr_t)mmu_map_from_physical(mapped_address);

	/* Yeah, uh, one byte. That works. */
	*(char*)blarg = *(char*)data;

	return 0;
}

long ptrace_signals_only(pid_t pid) {
	process_t * tracee = process_from_pid(pid);
	if (!tracee || (tracee->tracer != this_core->current_process->id) || !(tracee->flags & PROC_FLAG_SUSPENDED)) return -ESRCH;
	__sync_and_and_fetch(&tracee->flags, ~(PROC_FLAG_TRACE_SYSCALLS));
	return 0;
}

long ptrace_singlestep(pid_t pid, int sig) {
	process_t * tracee = process_from_pid(pid);
	if (!tracee || (tracee->tracer != this_core->current_process->id) || !(tracee->flags & PROC_FLAG_SUSPENDED)) return -ESRCH;

	/* arch_set_singlestep? */
	#if defined(__x86_64__)
	struct regs * target = tracee->interrupt_registers ? tracee->interrupt_registers : tracee->syscall_registers;
	target->rflags |= (1 << 8);
	#elif defined(__aarch64__)
	tracee->thread.context.saved[11] |= (1 << 21);
	#endif

	__sync_and_and_fetch(&tracee->flags, ~(PROC_FLAG_SUSPENDED));
	tracee->status = (sig << 8);
	make_process_ready(tracee);

	return 0;
}

long ptrace_handle(long request, pid_t pid, void * addr, void * data) {
	switch (request) {
		case PTRACE_ATTACH:
			return ptrace_attach(pid);
		case PTRACE_TRACEME:
			return ptrace_self();
		case PTRACE_GETREGS:
			return ptrace_getregs(pid,data);
		case PTRACE_CONT:
			return ptrace_continue(pid,(uintptr_t)data);
		case PTRACE_PEEKDATA:
			return ptrace_peek(pid,addr,data);
		case PTRACE_POKEDATA:
			return ptrace_poke(pid,addr,data);
		case PTRACE_SIGNALS_ONLY_PLZ:
			return ptrace_signals_only(pid);
		case PTRACE_SINGLESTEP:
			return ptrace_singlestep(pid,(uintptr_t)data);
		default:
			return -EINVAL;
	}
}

