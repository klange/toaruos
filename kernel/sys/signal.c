/**
 * @file  kernel/sys/signal.c
 * @brief Signal handling.
 *
 * Provides signal entry and delivery; also handles suspending
 * and resuming jobs (SIGTSTP, SIGCONT).
 *
 * Signal delivery in ToaruOS is a bit odd; we save a lot of the
 * kernel context from before the signal, including the entire
 * kernel stack, so that we can resume userspace in a new context;
 * essentially we build a whole new kernel thread for the signal
 * handler to run in, and then restore the original when the signal
 * handler exits. Signal handler exits are generally handled by
 * a page fault at a magic known address.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2012-2021 K. Lange
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

static void maybe_restart_system_call(struct regs * r) {
	if (this_core->current_process->interrupted_system_call && arch_syscall_number(r) == -ERESTARTSYS) {
		arch_syscall_return(r, this_core->current_process->interrupted_system_call);
		this_core->current_process->interrupted_system_call = 0;
		syscall_handler(r);
	}
}

int handle_signal(process_t * proc, signal_t * sig, struct regs *r) {
	uintptr_t handler = sig->handler;
	uintptr_t signum  = sig->signum;
	free(sig);

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

	arch_enter_signal_handler(handler, signum, r);
	return 1; /* Should not be reachable */

_ignore_signal:
	/* we still need to check if we need to restart something */

	maybe_restart_system_call(r);

	return !this_core->current_process->signal_queue->length;
}

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
	sig->handler = (uintptr_t)receiver->signals[signal];
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

void return_from_signal_handler(struct regs *r) {
	arch_return_from_signal_handler(r);
	maybe_restart_system_call(r);
}
