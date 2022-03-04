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

static spin_lock_t sig_lock;
static spin_lock_t sig_lock_b;

char isdeadly[] = {
	0, /* 0? */
	[SIGHUP     ] = 1,
	[SIGINT     ] = 1,
	[SIGQUIT    ] = 2,
	[SIGILL     ] = 2,
	[SIGTRAP    ] = 2,
	[SIGABRT    ] = 2,
	[SIGEMT     ] = 2,
	[SIGFPE     ] = 2,
	[SIGKILL    ] = 1,
	[SIGBUS     ] = 2,
	[SIGSEGV    ] = 2,
	[SIGSYS     ] = 2,
	[SIGPIPE    ] = 1,
	[SIGALRM    ] = 1,
	[SIGTERM    ] = 1,
	[SIGUSR1    ] = 1,
	[SIGUSR2    ] = 1,
	[SIGCHLD    ] = 0,
	[SIGPWR     ] = 0,
	[SIGWINCH   ] = 0,
	[SIGURG     ] = 0,
	[SIGPOLL    ] = 0,
	[SIGSTOP    ] = 3,
	[SIGTSTP    ] = 3,
	[SIGCONT    ] = 4,
	[SIGTTIN    ] = 3,
	[SIGTTOUT   ] = 3,
	[SIGVTALRM  ] = 1,
	[SIGPROF    ] = 1,
	[SIGXCPU    ] = 2,
	[SIGXFSZ    ] = 2,
	[SIGWAITING ] = 0,
	[SIGDIAF    ] = 1,
	[SIGHATE    ] = 0,
	[SIGWINEVENT] = 0,
	[SIGCAT     ] = 0,
};

void handle_signal(process_t * proc, signal_t * sig, struct regs *r) {
	uintptr_t handler = sig->handler;
	uintptr_t signum  = sig->signum;
	free(sig);

	/* Are we being traced? */
	if (this_core->current_process->flags & PROC_FLAG_TRACE_SIGNALS) {
		signum = ptrace_signal(signum, 0);
	}

	if (proc->flags & PROC_FLAG_FINISHED) {
		return;
	}

	if (signum == 0 || signum >= NUMSIGNALS) {
		/* Ignore */
		return;
	}

	if (!handler) {
		char dowhat = isdeadly[signum];
		if (dowhat == 1 || dowhat == 2) {
			task_exit(((128 + signum) << 8) | signum);
			__builtin_unreachable();
		} else if (dowhat == 3) {
			__sync_or_and_fetch(&this_core->current_process->flags, PROC_FLAG_SUSPENDED);
			this_core->current_process->status = 0x7F;

			process_t * parent = process_get_parent((process_t *)this_core->current_process);

			if (parent && !(parent->flags & PROC_FLAG_FINISHED)) {
				wakeup_queue(parent->wait_queue);
			}

			switch_task(0);
		} else if (dowhat == 4) {
			switch_task(1);
			return;
		}
		/* XXX dowhat == 2: should dump core */
		/* XXX dowhat == 3: stop */
		return;
	}

	if (handler == 1) /* Ignore */ {
		return;
	}

	arch_enter_signal_handler(handler, signum, r);
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

	if (!receiver->signals[signal] && !isdeadly[signal]) {
		/* If we're blocking a signal and it's not going to kill us, don't deliver it */
		return 0;
	}

	if (isdeadly[signal] == 4) {
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
	memset(&sig->registers_before, 0x00, sizeof(struct regs));
	list_insert(receiver->signal_queue, sig);

	process_awaken_signal(receiver);

	if (!process_is_ready(receiver) || receiver == this_core->current_process) {
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
	if (this_core->current_process &&
		!(this_core->current_process->flags & PROC_FLAG_FINISHED) &&
		this_core->current_process->signal_queue &&
		this_core->current_process->signal_queue->length > 0) {

		node_t * node = list_dequeue(this_core->current_process->signal_queue);
		signal_t * sig = node->value;

		free(node);

		handle_signal((process_t*)this_core->current_process,sig,r);
	}
}

