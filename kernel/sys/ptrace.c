/**
 * @file kernel/sys/ptrace.c
 * @brief Process tracing functions
 *
 * Provides single stepping, cross-process memory inspection,
 * regiser inspection, poking, and syscall trace events.
 *
 * @warning This ptrace implementation is incomplete.
 *
 * We are missing a lot of @c ptrace functionality found in other
 * operating systems, and even some of the functionality we have is
 * only partially implemented or may not work as it should.
 *
 * This implementation was intended primarily to support having a
 * @c strace command in userspace, and also provides some limited
 * support for a debugger.
 *
 * @see apps/dbg.c
 * @see apps/strace.c
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021-2022 K. Lange
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

/**
 * @brief Internally set the tracer of a tracee process.
 *
 * Sets up @p tracer to trace @p tracee and sets @p tracee as
 * tracing the default events (syscalls and signals).
 *
 * A tracer can trace multiple tracees, but a tracee can only be
 * traced by one tracer.
 *
 * @param tracer Process that is the doing the tracing
 * @param tracee Process that is breing traced
 */
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

/**
 * @brief Start tracing a process.
 *
 * @ref PTRACE_ATTACH
 *
 * Sets the current process to be the tracer for the target tracee.
 * Both the tracer and tracee will resume normally, until the next
 * ptrace event stops the tracee.
 *
 * TODO What happens if the process is already being traced?
 *
 * @param pid Tracee ID
 * @returns 0 on success, -ESRCH if the tracee is invalid, -EPERM if the tracee
 *          is not owned by the same user as the tracer and the tracer is not root.
 */
long ptrace_attach(pid_t pid) {
	process_t * tracer = (process_t *)this_core->current_process;
	process_t * tracee = process_from_pid(pid);
	if (!tracee) return -ESRCH;
	if (tracer->user != 0 && tracer->user != tracee->user) return -EPERM;

	_ptrace_trace(tracer, tracee);

	return 0;
}

/**
 * @brief Set the current process to be traced by its parent.
 *
 * @ref PTRACE_TRACEME
 *
 * Generally, this is used through the @c ptrace system call by
 * the debugger or @c strace implementation after forking a child
 * process and before calling @c exec.
 *
 * The calling process will resume immediately.
 *
 * TODO What happens if we are already being traced?
 *
 * @returns 0 on success, -EINVAL if the parent was not found.
 */
long ptrace_self(void) {
	process_t * tracee = (process_t*)this_core->current_process;
	process_t * tracer = process_get_parent(tracee);
	if (!tracer) return -EINVAL;

	_ptrace_trace(tracer, tracee);

	return 0;
}

/**
 * @brief Trigger a ptrace event on the currently executing thread.
 *
 * @ref PTRACE_EVENT_SINGLESTEP
 * @ref PTRACE_EVENT_SYSCALL_ENTER
 * @ref PTRACE_EVENT_SYSCALL_EXIT
 *
 * Called elsewhere in the kernel when a trace event happens that is
 * not currently being ignored, such as upon entry into a syscall handler,
 * or exit from a syscall handler, or before a signal would be delivered.
 *
 * Runs in the kernel context of the tracee, causes the tracee to be suspended
 * and awakens the tracer to return from its @c ptrace call.
 *
 * When the kernel context for this process is resumed, the signal number
 * will be checked from the tracee's status and returned to caller that
 * initiated the ptrace event.
 *
 * When resuming from a signal event, the new signal number will replace the
 * old signal number. In this case, if the new signal number is 0 it will
 * be discarded and the tracee will continue as if it had ignored it.
 *
 * When resuming from other events, signals are generally sent directly
 * and the process will act on the signal when it has an opportunity to
 * return to userspace.
 *
 * @param signal Signal number if @p reason is 0.
 * @param reason PTRACE_EVENT value describing the event; 0 for signal delivery.
 * @returns Signal number from tracee status upon resumption.
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
	this_core->current_process->status = 0;
	return signum;
}

/**
 * @brief Resume a traced process.
 *
 * Unsuspends the traced process, sending an appropriate signal if one
 * was currently pending or if one was sent by the tracer through either
 * of @ref ptrace_continue or @ref ptrace_detach.
 *
 * @param pid Tracee ID
 * @param tracee Tracee process object
 * @param sig Signal number to send, or 0 if none.
 */
static void signal_and_continue(pid_t pid, process_t * tracee, int sig) {
	/* Unsuspend */
	__sync_and_and_fetch(&tracee->flags, ~(PROC_FLAG_SUSPENDED));

	/* Does the process have a pending signal? */
	if ((tracee->status >> 8) & 0xFF && (!(tracee->status >> 16) || ((tracee->status >> 16) == 0xFF))) {
		tracee->status = (sig << 8);
		make_process_ready(tracee);
	} else if (sig) {
		send_signal(pid, sig,1);
	} else {
		make_process_ready(tracee);
	}
}

/**
 * @brief Resume the tracee until the next event.
 *
 * @ref PTRACE_CONT
 *
 * Allows the tracee to resume execution, while optionally sending
 * a signal. This signal may be the one that triggered the ptrace
 * event from which the process is being resumed, or a new signal,
 * or no signal at all.
 *
 * @param pid Tracee ID
 * @param sig Signal to send to tracee on resume, or 0 for none.
 * @returns 0 on success, -ESRCH if tracee is invalid.
 */
long ptrace_continue(pid_t pid, int sig) {
	process_t * tracee = process_from_pid(pid);
	if (!tracee || (tracee->tracer != this_core->current_process->id) || !(tracee->flags & PROC_FLAG_SUSPENDED)) return -ESRCH;

	signal_and_continue(pid,tracee,sig);

	return 0;
}

/**
 * @brief Stop tracing a tracee.
 *
 * @ref PTRACE_DETACH
 *
 * Marks the tracee as no longer being traced and resumes it.
 *
 * @param pid Tracee ID
 * @param sig Signal to send to tracee on resume, or 0 for none.
 * @returns 0 on success, -ESRCH if tracee is invalid.
 */
long ptrace_detach(pid_t pid, int sig) {
	process_t * tracee = process_from_pid(pid);
	if (!tracee || (tracee->tracer != this_core->current_process->id) || !(tracee->flags & PROC_FLAG_SUSPENDED)) return -ESRCH;

	/* Mark us not the tracer. */
	tracee->tracer = 0;

	signal_and_continue(pid,tracee,sig);

	return 0;
}

/**
 * @brief Obtain the register context of the tracee.
 *
 * @ref PTRACE_GETREGS
 *
 * Copies the interrupt register context of the tracee into a tracer-provided
 * address. The size, meaning, and layout of the data copied is architecture-dependent.
 *
 * Currently this is either @c interrupt_registers or @c syscall_registers, depending
 * on what is available. Since the tracee needs to be suspended this should represent
 * the actual userspace register context when it resumes.
 *
 * On AArch64 we also add ELR, which isn't in the interrupt or syscall register contexts,
 * but pushed somewhere else...
 *
 * TODO We should support reading FPU regs as well.
 * TODO @c PTRACE_SETREGS so we can modify them.
 *
 * @param pid Tracee ID
 * @param data Address in tracer to write data into.
 * @returns 0 on success, -ESRCH if tracee is invalid.
 */
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

/**
 * @brief Read one byte from the tracee's memory.
 *
 * @ref PTRACE_PEEKDATA
 *
 * Reads one byte of data from the tracee process's memory space.
 * Other implementations of @c PTRACE_PEEKDATA may write other sizes of data,
 * but to make this as straightforward as possible, we only support single
 * bytes. Maybe in the future we'll support other sizes...
 *
 * @param pid Tracee ID
 * @param addr Virtual address in the tracee context to write to.
 * @param data Address in the tracer to store the read byte into.
 * @returns 0 on success, -EFAULT if the requested address is not mapped and readable in the tracee, -ESRCH if tracee is invalid.
 */
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

/**
 * @brief Place a byte of data into the tracee's memory.
 *
 * @ref PTRACE_POKEDATA
 *
 * Writes one byte of data into the tracee process's memory space.
 * Other implementations of @c PTRACE_POKEDATA may write other sizes of data,
 * but to make this as straightforward as possible, we only support single
 * bytes. Maybe in the future we'll support other sizes...
 *
 * TODO This uses mmu_map_from_physical and doesn't do any cache maintenance?
 *      It will probably break when, eg., poking instructions on ARM...
 *
 * @param pid Tracee ID
 * @param addr Virtual address in the tracee context to write to.
 * @param data Address in the tracer context to read one byte from.
 * @returns 0 on success, -ESRCH if tracee is invalid, -EFAULT if the tracee address is not mapped or not writable.
 */
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

/**
 * @brief Disable tracing of syscalls in the tracee.
 *
 * @ref PTRACE_SIGNALS_ONLY_PLZ
 *
 * Turns off tracing of syscalls in the tracee. Only signals will be
 * traced. To turn syscall tracing back on, restart tracing by detaching
 * and re-attaching to the tracee.
 *
 * TODO We need a better interface to configure tracing, so we can offer
 *      more complex options than just signals and syscalls...
 *
 * @param pid Tracee ID
 * @returns 0 on success, -ESRCH if the tracee was not found or the current process is not its tracer.
 */
long ptrace_signals_only(pid_t pid) {
	process_t * tracee = process_from_pid(pid);
	if (!tracee || (tracee->tracer != this_core->current_process->id) || !(tracee->flags & PROC_FLAG_SUSPENDED)) return -ESRCH;
	__sync_and_and_fetch(&tracee->flags, ~(PROC_FLAG_TRACE_SYSCALLS));
	return 0;
}

/**
 * @brief Enable single-stepping for a process.
 *
 * @ref PTRACE_SINGLESTEP
 *
 * Enables an architecture-specific mechanism for single step debugging
 * in the requested process. When the process resumes, it will execute
 * one instruction and then fault back to the kernel, and the tracer
 * will be alerted.
 *
 * Single stepping will be disabled again when the process returns from
 * the fault, and must be re-enabled by another call to @c ptrace_singlstep.
 *
 * @param pid ID of the process to enable single-step for
 * @param sig Signal number to hand to the process when it resumes, or 0.
 * @returns 0 on success, -ESRCH if the process could not be found or is not a tracee of the current process.
 */
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

/**
 * @brief Handle ptrace system call requests.
 *
 * Internal interface for dispatching @c ptrace system calls. Maps
 * arguments from the system call to the various ptrace functions.
 *
 * @note This is the direct system call implementation. Data coming
 *       in here is directly from the arguments of the system call.
 *
 * @param request Request type
 * @param pid Tracee ID
 * @param addr Address to peek or poke
 * @param data Place to put or read data, depending on the function
 * @returns Generally, status codes. -EINVAL for an invalid request.
 */
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
		case PTRACE_DETACH:
			return ptrace_detach(pid,(uintptr_t)data);
		default:
			return -EINVAL;
	}
}

