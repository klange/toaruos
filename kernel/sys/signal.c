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
	[SIGTTOU    ] = SIG_DISP_Stop,
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

#define shift_signal(signum) (1ULL << signum)

/**
 * @brief If a system call returned -ERESTARTSYS, restart it.
 *
 * Called by both @c handle_signal and @c return_from_signal_handler depending
 * on how the signal was handled.
 *
 * @param r Registers after restoration from signal return.
 */
static void maybe_restart_system_call(struct regs * r, int signum) {
	if (signum < 0 || signum >= NUMSIGNALS) return;
	if (this_core->current_process->interrupted_system_call && arch_syscall_number(r) == -ERESTARTSYS) {
		if (sig_defaults[signum] == SIG_DISP_Cont || (this_core->current_process->signals[signum].flags & SA_RESTART)) {
			arch_syscall_return(r, this_core->current_process->interrupted_system_call);
			this_core->current_process->interrupted_system_call = 0;
			syscall_handler(r);
		} else {
			this_core->current_process->interrupted_system_call = 0;
			arch_syscall_return(r, -EINTR);
		}
	}
}

#define PENDING (this_core->current_process->pending_signals & ((~this_core->current_process->blocked_signals) | shift_signal(SIGSTOP) | shift_signal(SIGKILL)))

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
int handle_signal(process_t * proc, int signum, struct regs *r) {
	struct signal_config config = proc->signals[signum];

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

	if (!config.handler) {
		char dowhat = sig_defaults[signum];
		if (dowhat == SIG_DISP_Term || dowhat == SIG_DISP_Core) {
			task_exit(((128 + signum) << 8) | signum);
			__builtin_unreachable();
		} else if (dowhat == SIG_DISP_Stop) {
			__sync_or_and_fetch(&this_core->current_process->flags, PROC_FLAG_SUSPENDED);
			this_core->current_process->status = 0x7F | (signum << 8) | 0xFF0000;

			process_t * parent = process_get_parent((process_t *)this_core->current_process);

			if (parent && !(parent->flags & PROC_FLAG_FINISHED)) {
				wakeup_queue(parent->wait_queue);
			}

			do {
				switch_task(0);
			} while (!PENDING);

			return 0; /* Return and handle another */
		} else if (dowhat == SIG_DISP_Cont) {
			/* Continue doesn't actually do anything different at this stage. */
			goto _ignore_signal;
		}
		goto _ignore_signal;
	}

	/* If the handler value is 1 we treat it as IGN. */
	if (config.handler == 1) goto _ignore_signal;

	if (config.flags & SA_RESETHAND) {
		proc->signals[signum].handler = 0;
	}

	arch_enter_signal_handler(config.handler, signum, r);
	return 1; /* Should not be reachable */

_ignore_signal:
	/* we still need to check if we need to restart something */

	maybe_restart_system_call(r, signum);

	return !PENDING;
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

	if (!receiver) return -ESRCH;
	if (!force_root && receiver->user != this_core->current_process->user && this_core->current_process->user != USER_ROOT_UID &&
		!(signal == SIGCONT && receiver->session == this_core->current_process->session)) return -EPERM;
	if (receiver->flags & PROC_FLAG_IS_TASKLET) return -EPERM;
	if (signal >= NUMSIGNALS || signal < 0) return -EINVAL;
	if (receiver->flags & PROC_FLAG_FINISHED) return -ESRCH;
	if (signal == 0) return 0;

	int awaited = receiver->awaited_signals & shift_signal(signal);
	int ignored = !receiver->signals[signal].handler && !sig_defaults[signal];
	int blocked = (receiver->blocked_signals & shift_signal(signal)) && signal != SIGKILL && signal != SIGSTOP;

	/* sigcont always unsuspends */
	if (sig_defaults[signal] == SIG_DISP_Cont && (receiver->flags & PROC_FLAG_SUSPENDED)) {
		__sync_and_and_fetch(&receiver->flags, ~(PROC_FLAG_SUSPENDED));
		receiver->status = 0;
	}

	/* Do nothing if the signal is not being waited for or blocked and the default disposition is to ignore. */
	if (!awaited && !blocked && ignored) return 0;

	/* Mark the signal for delivery. */
	spin_lock(sig_lock);
	receiver->pending_signals |= shift_signal(signal);
	spin_unlock(sig_lock);

	/* If the signal is blocked and not being awaited, end here. */
	if (blocked && !awaited) return 0;

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

	if (signal < 0) return 0;

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
_tryagain:
	spin_lock(sig_lock);
	if (this_core->current_process && !(this_core->current_process->flags & PROC_FLAG_FINISHED)) {
		/* Set an pending signals that were previously blocked */
		sigset_t active_signals  = PENDING;

		int signal = 0;
		while (active_signals && signal < NUMSIGNALS)  {
			if (active_signals & 1) {
				this_core->current_process->pending_signals &= ~shift_signal(signal);
				spin_unlock(sig_lock);
				if (handle_signal((process_t*)this_core->current_process, signal, r)) return;
				goto _tryagain;
			}
			active_signals >>= 1;
			signal++;
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
	int signum = arch_return_from_signal_handler(r);
	if (PENDING) {
		process_check_signals(r);
	}
	maybe_restart_system_call(r,signum);
}

/**
 * @brief Synchronously wait for specified signals to become pending.
 *
 * The signals in @c awaited are set as the current "awaited set". Delivery
 * of these signals will ignore the blocked and ignored states and always
 * result in the process be awoken with the signal marked pending if it is
 * sleeping. When the process awakens from @c switch_task the awaiting set
 * will be cleared.
 *
 * If no unblocked signal is pending and an awaited, blocked signal is pending,
 * its signal number will be placed in @p sig and it will be unmarked as
 * pending, returning 0. If a unblocked signal is received, @c -EINTR is
 * returned, and under normal circumstances the caller should raise this
 * return status up and allow normal signal handling to occur.
 *
 * Otherwise, if the process is reawoken by some other means and no unblocked
 * signals or awaited signals are pending, it will apply the awaited set and
 * sleep again. This will repeat until either of these conditions are met.
 *
 * If a signal specified in @p awaited is not currently blocked, but is pending
 * upon entering signal_await, it will be marked as not pending and the call
 * will return immediately; if an unblocked signal is not pending, it will not
 * be awaited: signal_await will return with -EINTR.
 *
 * @param awaited Signals to wait for, should all be blocked by caller.
 * @param sig     Will be set to the awaited signal, if one arrives.
 * @returns 0 if an awaited signal arrives, -EINTR if another signal arrives.
 */
int signal_await(sigset_t awaited, int * sig) {
	do {
		sigset_t maybe = awaited & this_core->current_process->pending_signals;
		if (maybe) {
			int signal = 0;
			while (maybe && signal < NUMSIGNALS) {
				if (maybe & 1) {
					spin_lock(sig_lock);
					this_core->current_process->pending_signals &= ~shift_signal(signal);
					*sig = signal;
					spin_unlock(sig_lock);
					return 0;
				}
				maybe >>= 1;
				signal++;
			}
		}

		/* Set awaited signals */
		this_core->current_process->awaited_signals = awaited;

		/* Sleep */
		switch_task(0);

		/* Unset awaited signals. */
		this_core->current_process->awaited_signals = 0;
	} while (!PENDING);

	return -EINTR;
}

