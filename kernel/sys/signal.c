/**
 * @file  kernel/sys/signal.c
 * @brief Signal handling.
 *
 * Provides signal entry and delivery; also handles suspending
 * and resuming jobs (SIGTSTP, SIGCONT).
 *
 * As of Misaka 2.1, signal delivery has been largely rewritten:
 * - Signals can only be delivered a times when we would be
 *   normally returning to userspace. This matches behavior in
 *   a number of other kernels.
 * - Signals should cause kernel sleeps to return with an error
 *   state, ending any blocking system calls and allowing them
 *   to either gracefully return or bubble up -ERESTARTSYS to
 *   be restarted.
 * - Userspace signal handlers now push context on the userspace
 *   stack. This is arch-specific behavior.
 * - Signal handler returns work the same as previously, injecting
 *   a special "magic" return address that should fault.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2012-2022 K. Lange
 */
#include <errno.h>
#include <stdint.h>
#include <sys/signal.h>
#include <sys/signal_defs.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/process.h>
#include <kernel/signal.h>
#include <kernel/spinlock.h>
#include <kernel/ptrace.h>
#include <kernel/syscall.h>

static spin_lock_t sig_lock;

#define SIG_DISP_Ign  0
#define SIG_DISP_Term 1
#define SIG_DISP_Core 2
#define SIG_DISP_Stop 3
#define SIG_DISP_Cont 4

static char sig_defaults[] = {
	0, /* 0? */
	[SIGHUP     ] = SIG_DISP_Term,
	[SIGINT     ] = SIG_DISP_Term,
	[SIGQUIT    ] = SIG_DISP_Core,
	[SIGILL     ] = SIG_DISP_Core,
	[SIGTRAP    ] = SIG_DISP_Core,
	[SIGABRT    ] = SIG_DISP_Core,
	[SIGEMT     ] = SIG_DISP_Core,
	[SIGFPE     ] = SIG_DISP_Core,
	[SIGKILL    ] = SIG_DISP_Term,
	[SIGBUS     ] = SIG_DISP_Core,
	[SIGSEGV    ] = SIG_DISP_Core,
	[SIGSYS     ] = SIG_DISP_Core,
	[SIGPIPE    ] = SIG_DISP_Term,
	[SIGALRM    ] = SIG_DISP_Term,
	[SIGTERM    ] = SIG_DISP_Term,
	[SIGUSR1    ] = SIG_DISP_Term,
	[SIGUSR2    ] = SIG_DISP_Term,
	[SIGCHLD    ] = SIG_DISP_Ign,
	[SIGPWR     ] = SIG_DISP_Ign,
	[SIGWINCH   ] = SIG_DISP_Ign,
	[SIGURG     ] = SIG_DISP_Ign,
	[SIGPOLL    ] = SIG_DISP_Ign,
	[SIGSTOP    ] = SIG_DISP_Stop,
	[SIGTSTP    ] = SIG_DISP_Stop,
	[SIGCONT    ] = SIG_DISP_Cont,
	[SIGTTIN    ] = SIG_DISP_Stop,
	[SIGTTOUT   ] = SIG_DISP_Stop,
	[SIGVTALRM  ] = SIG_DISP_Term,
	[SIGPROF    ] = SIG_DISP_Term,
	[SIGXCPU    ] = SIG_DISP_Core,
	[SIGXFSZ    ] = SIG_DISP_Core,
	[SIGWAITING ] = SIG_DISP_Ign,
	[SIGDIAF    ] = SIG_DISP_Term,
	[SIGHATE    ] = SIG_DISP_Ign,
	[SIGWINEVENT] = SIG_DISP_Ign,
	[SIGCAT     ] = SIG_DISP_Ign,
};

/**
 * @brief If a system call returned -ERESTARTSYS, restart it.
 *
 * Called by both @c handle_signal and @c return_from_signal_handler depending
 * on how the signal was handled.
 *
 * @param r Registers after restoration from signal return.
 */
static void maybe_restart_system_call(struct regs * r) {
	if (this_core->current_process->interrupted_system_call && arch_syscall_number(r) == -ERESTARTSYS) {
		arch_syscall_return(r, this_core->current_process->interrupted_system_call);
		this_core->current_process->interrupted_system_call = 0;
		syscall_handler(r);
	}
}

/**
 * @brief Examine the pending signal and perform an appropriate action.
 *
 * This is called by @c process_check_signals below. It should not be called
 * directly by other parts of the kernel. Previously, it was called through
 * process switching...
 *
 * When a signal handler is called, this does not return. The userspace
 * process is resumed in the signal handler context, and any future calls
 * into the kernel are "from scratch".
 *
 * @param proc should be the current active process, which should generally
 *             always be this_core->current_process.
 * @param sig  is the signal node from the pending queue. Currently, this
 *             just contains the signal number and nothing else. It used to
 *             also contain the handler to call, but that led to TOCTOU bugs.
 * @param r    Userspace registers at time of signal entry. This gets passed
 *             forward to @c arch_enter_signal_handler
 * @returns 0 if another signal needs to be handled, 1 otherwise.
 */
int handle_signal(process_t * proc, signal_t * sig, struct regs *r) {
	uintptr_t signum  = sig->signum;
	free(sig);

	uintptr_t handler = proc->signals[signum];

	/* Are we being traced? */
	if (this_core->current_process->flags & PROC_FLAG_TRACE_SIGNALS) {
		signum = ptrace_signal(signum, 0);
	}

	if (proc->flags & PROC_FLAG_FINISHED) {
		return 1;
	}

	if (signum == 0 || signum >= NUMSIGNALS) {
		goto _ignore_signal;
	}

	if (!handler) {
		char dowhat = sig_defaults[signum];
		if (dowhat == SIG_DISP_Term || dowhat == SIG_DISP_Core) {
			task_exit(((128 + signum) << 8) | signum);
			__builtin_unreachable();
		} else if (dowhat == SIG_DISP_Stop) {
			__sync_or_and_fetch(&this_core->current_process->flags, PROC_FLAG_SUSPENDED);
			this_core->current_process->status = 0x7F;

			process_t * parent = process_get_parent((process_t *)this_core->current_process);

			if (parent && !(parent->flags & PROC_FLAG_FINISHED)) {
				wakeup_queue(parent->wait_queue);
			}

			do {
				switch_task(0);
			} while (!this_core->current_process->signal_queue->length);

			return 0; /* Return and handle another */
		} else if (dowhat == SIG_DISP_Cont) {
			/* Continue doesn't actually do anything different at this stage. */
			goto _ignore_signal;
		}
		goto _ignore_signal;
	}

	/* If the handler value is 1 we treat it as IGN. */
	if (handler == 1) goto _ignore_signal;

	proc->signals[signum] = 0;

	arch_enter_signal_handler(handler, signum, r);
	return 1; /* Should not be reachable */

_ignore_signal:
	/* we still need to check if we need to restart something */

	maybe_restart_system_call(r);

	return !this_core->current_process->signal_queue->length;
}

/**
 * @brief Deliver a signal to another process.
 *
 * Called by both system calls like @c kill as well as by some things
 * that want to trigger SIGSEGV, SIGPIPE, and so on.
 *
 * @param process    PID to deliver to. Must be a single PID, not a group specifier.
 * @param signal     Signal number to deliver.
 * @param force_root If the current process isn't root, it can't send signals to
 *                   processes owned by other users, which means we can't send soft
 *                   signals as part operations like SIGPIPE or SIGCHLD. Kernel callers
 *                   can use this parameter to skip this check.
 * @returns General status, should be suitable for sys_kill return value.
 */
int send_signal(pid_t process, int signal, int force_root) {
	process_t * receiver = process_from_pid(process);

	if (!receiver) {
		/* Invalid pid */
		return -ESRCH;
	}

	if (!force_root && receiver->user != this_core->current_process->user && this_core->current_process->user != USER_ROOT_UID) {
		if (!(signal == SIGCONT && receiver->session == this_core->current_process->session)) {
			return -EPERM;
		}
	}

	if (receiver->flags & PROC_FLAG_IS_TASKLET) {
		/* Can not send signals to kernel tasklets */
		return -EINVAL;
	}

	if (signal > NUMSIGNALS) {
		/* Invalid signal */
		return -EINVAL;
	}

	if (receiver->flags & PROC_FLAG_FINISHED) {
		/* Can't send signals to finished processes */
		return -EINVAL;
	}

	if (!receiver->signals[signal] && !sig_defaults[signal]) {
		/* If there is no handler for a signal and its default disposition is IGNORE,
		 * we don't even bother sending it, to avoid having to interrupt + restart system calls. */
		return 0;
	}

	if (sig_defaults[signal] == SIG_DISP_Cont) {
		/* XXX: I'm not sure this check is necessary? And the SUSPEND flag flip probably
		 *      should be on the receiving end. */
		if (!(receiver->flags & PROC_FLAG_SUSPENDED)) {
			return -EINVAL;
		} else {
			__sync_and_and_fetch(&receiver->flags, ~(PROC_FLAG_SUSPENDED));
			receiver->status = 0;
		}
	}

	/* Append signal to list */
	signal_t * sig = malloc(sizeof(signal_t));
	sig->signum  = signal;

	spin_lock(sig_lock);
	list_insert(receiver->signal_queue, sig);
	spin_unlock(sig_lock);

	/* Informs any blocking events that the process has been interrupted
	 * by a signal, which should trigger those blocking events to complete
	 * and potentially return -EINTR or -ERESTARTSYS */
	process_awaken_signal(receiver);

	/* Schedule processes awoken by signals to be run. Unless they're us, we'll
	 * jump to the signal handler as part of returning from this call. */
	if (receiver != this_core->current_process && !process_is_ready(receiver)) {
		make_process_ready(receiver);
	}

	return 0;
}

/**
 * @brief Send a signal to multiple processes.
 *
 * Similar to @c send_signal but for when a negative PID needs to be used.
 *
 * @param group The group process ID. Positive PID, not negative.
 * @param signal Signal number to deliver.
 * @param force_root See explanation in @c send_signal
 * @returns 1 if something was signalled, 0 if there were no valid recipients.
 */
int group_send_signal(pid_t group, int signal, int force_root) {

	int kill_self = 0;
	int killed_something = 0;

	foreach(node, process_list) {
		process_t * proc = node->value;
		if (proc->group == proc->id && proc->job == group) {
			/* Only thread group leaders */
			if (proc->group == this_core->current_process->group) {
				kill_self = 1;
			} else {
				if (send_signal(proc->group, signal, force_root) == 0) {
					killed_something = 1;
				}
			}
		}
	}

	if (kill_self) {
		if (send_signal(this_core->current_process->group, signal, force_root) == 0) {
			killed_something = 1;
		}
	}

	return !!killed_something;
}

/**
 * @brief Examine the signal delivery queue of the current process, and handle signals.
 *
 * Should be called before a userspace return would happen. If a signal handler is to be
 * run in userspace, then process_check_signals will not return, similar to exec.
 *
 * @param r Userspace registers before signal entry.
 */
void process_check_signals(struct regs * r) {
	spin_lock(sig_lock);
	if (this_core->current_process &&
		!(this_core->current_process->flags & PROC_FLAG_FINISHED) &&
		this_core->current_process->signal_queue &&
		this_core->current_process->signal_queue->length > 0) {
		while (1) {
			node_t * node = list_dequeue(this_core->current_process->signal_queue);
			spin_unlock(sig_lock);

			signal_t * sig = node->value;
			free(node);

			if (handle_signal((process_t*)this_core->current_process,sig,r)) return;

			spin_lock(sig_lock);
		}
	}
	spin_unlock(sig_lock);
}

/**
 * @brief Restore pre-signal context and possibly restart system calls.
 *
 * To be called by the platform's fault handler when it determines that
 * a signal handler return has been triggered. Calls platform code to restore
 * the previous userspace context (before the signal) from the userspace stack
 * and restarts an interrupted system call if there was one.
 *
 * @param r Registers at fault, passed to platform code for restoration and
 *          then to @c maybe_restart_system_call to handle system call restarts.
 */
void return_from_signal_handler(struct regs *r) {
	arch_return_from_signal_handler(r);
	maybe_restart_system_call(r);
}
