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

#include <kernel/arch/x86_64/regs.h>
#include <kernel/arch/x86_64/mmu.h>

long ptrace_attach(pid_t pid) {
	process_t * tracee = process_from_pid(pid);
	if (!tracee) return -ESRCH;
	if (this_core->current_process->user != 0 && this_core->current_process->user != tracee->user) return -EPERM;
	__sync_or_and_fetch(&tracee->flags, PROC_FLAG_TRACED);
	tracee->tracer = this_core->current_process->id;
	send_signal(pid, SIGSTOP, 1);
	return 0;
}

long ptrace_self(void) {
	process_t * parent = process_get_parent((process_t*)this_core->current_process);
	if (!parent) return -EINVAL;

	__sync_or_and_fetch(&this_core->current_process->flags, PROC_FLAG_TRACED);
	this_core->current_process->tracer = parent->id;

	return 0;
}

/**
 * @brief Trigger a ptrace event on the currently executing thread.
 */
long ptrace_signal(int reason) {
	__sync_or_and_fetch(&this_core->current_process->flags, PROC_FLAG_SUSPENDED);
	this_core->current_process->status = 0x7F | (SIGTRAP << 8) | (reason << 16);

	process_t * parent = process_from_pid(this_core->current_process->tracer);
	if (parent && !(parent->flags & PROC_FLAG_FINISHED)) {
		wakeup_queue(parent->wait_queue);
	}
	switch_task(0);

	return 0;
}

long ptrace_continue(pid_t pid) {
	process_t * tracee = process_from_pid(pid);
	if (!tracee || (tracee->tracer != this_core->current_process->id) || !(tracee->flags & PROC_FLAG_SUSPENDED)) return -ESRCH;

	/* Unsuspend */
	__sync_and_and_fetch(&tracee->flags, ~(PROC_FLAG_SUSPENDED));
	tracee->status = 0;
	make_process_ready(tracee);

	return 0;
}

long ptrace_getregs(pid_t pid, void * data) {
	if (!data || ptr_validate(data, "ptrace")) return -EFAULT;
	process_t * tracee = process_from_pid(pid);
	if (!tracee || (tracee->tracer != this_core->current_process->id) || !(tracee->flags & PROC_FLAG_SUSPENDED)) return -ESRCH;

	/* Copy registers */
	memcpy(data, tracee->syscall_registers, sizeof(struct regs));

	return 0;
}

long ptrace_peek(pid_t pid, void * addr, void * data) {
	if (!data || ptr_validate(data, "ptrace")) return -EFAULT;
	process_t * tracee = process_from_pid(pid);
	if (!tracee || (tracee->tracer != this_core->current_process->id) || !(tracee->flags & PROC_FLAG_SUSPENDED)) return -ESRCH;

	/* Figure out where *addr is...
	 *  TODO: We don't ever really page things to disk, and we don't have file
	 *        mappings that may be not-present, so this is fairly straightforward for now... */
	uintptr_t mapped_address = mmu_map_to_physical(tracee->thread.page_directory->directory, (uintptr_t)addr);

	if ((intptr_t)mapped_address < 0 && (intptr_t)mapped_address > -10) return -EFAULT;

	uintptr_t blarg = (uintptr_t)mmu_map_from_physical(mapped_address);

	/* Yeah, uh, one byte. That works. */
	*(char*)data = *(char*)blarg;

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
			return ptrace_continue(pid);
		case PTRACE_PEEKDATA:
			return ptrace_peek(pid,addr,data);
		default:
			return -EINVAL;
	}
}

