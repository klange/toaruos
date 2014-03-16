/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Signal Handling
 */

#include <system.h>
#include <signal.h>
#include <logging.h>

void enter_signal_handler(uintptr_t location, int signum, uintptr_t stack) {
	IRQ_OFF;
	asm volatile(
			"mov %2, %%esp\n"
			"pushl %1\n"           /*          argument count   */
			"pushl $" STRSTR(SIGNAL_RETURN) "\n"
			"mov $0x23, %%ax\n"    /* Segment selector */
			"mov %%ax, %%ds\n"
			"mov %%ax, %%es\n"
			"mov %%ax, %%fs\n"
			"mov %%ax, %%gs\n"
			"mov %%esp, %%eax\n"   /* Stack -> EAX */
			"pushl $0x23\n"        /* Segment selector again */
			"pushl %%eax\n"
			"pushf\n"              /* Push flags */
			"popl %%eax\n"         /* Fix the Interrupt flag */
			"orl  $0x200, %%eax\n"
			"pushl %%eax\n"
			"pushl $0x1B\n"
			"pushl %0\n"           /* Push the entry point */
			"iret\n"
			: : "m"(location), "m"(signum), "r"(stack) : "%ax", "%esp", "%eax");

	kprintf("Failed to jump to signal handler!\n");
}

static uint8_t volatile sig_lock;
static uint8_t volatile sig_lock_b;

char isdeadly[] = {
	0, /* 0? */
	1, /* SIGHUP     */
	1, /* SIGINT     */
	2, /* SIGQUIT    */
	2, /* SIGILL     */
	2, /* SIGTRAP    */
	2, /* SIGABRT    */
	2, /* SIGEMT     */
	2, /* SIGFPE     */
	1, /* SIGKILL    */
	2, /* SIGBUS     */
	2, /* SIGSEGV    */
	2, /* SIGSYS     */
	1, /* SIGPIPE    */
	1, /* SIGALRM    */
	1, /* SIGTERM    */
	1, /* SIGUSR1    */
	1, /* SIGUSR2    */
	0, /* SIGCHLD    */
	0, /* SIGPWR     */
	0, /* SIGWINCH   */
	0, /* SIGURG     */
	0, /* SIGPOLL    */
	3, /* SIGSTOP    */
	3, /* SIGTSTP    */
	0, /* SIGCONT    */
	3, /* SIGTTIN    */
	3, /* SIGTTOUT   */
	1, /* SIGVTALRM  */
	1, /* SIGPROF    */
	2, /* SIGXCPU    */
	2, /* SIGXFSZ    */
	0, /* SIGWAITING */
	1, /* SIGDIAF    */
	0, /* SIGHATE    */
	0, /* SIGWINEVENT*/
	0, /* SIGCAT     */
};

void handle_signal(process_t * proc, signal_t * sig) {
	uintptr_t handler = sig->handler;
	uintptr_t signum  = sig->signum;
	free(sig);

	if (proc->finished) {
		return;
	}

	if (signum == 0 || signum >= NUMSIGNALS) {
		/* Ignore */
		return;
	}

	if (!handler) {
		char dowhat = isdeadly[signum];
		if (dowhat == 1 || dowhat == 2) {
			debug_print(WARNING, "Process %d killed by unhandled signal (%d)", proc->id, signum);
			kexit(128 + signum);
			__builtin_unreachable();
		} else {
			debug_print(WARNING, "Ignoring signal %d by default in pid %d", signum, proc->id);
		}
		/* XXX dowhat == 2: should dump core */
		/* XXX dowhat == 3: stop */
		return;
	}

	if (handler == 1) /* Ignore */ {
		return;
	}

	debug_print(NOTICE, "handling signal in process %d (%d)", proc->id, signum);

	uintptr_t stack = 0xFFFF0000;
	if (proc->syscall_registers->useresp < 0x10000100) {
		stack = proc->image.user_stack;
	} else {
		stack = proc->syscall_registers->useresp;
	}

	/* Not marked as ignored, must call signal */
	enter_signal_handler(handler, signum, stack);

}

list_t * rets_from_sig;

void return_from_signal_handler(void) {
#if 0
	kprintf("[debug] Return From Signal for process %d\n", current_process->id);
#endif

	if (__builtin_expect(!rets_from_sig, 0)) {
		rets_from_sig = list_create();
	}

	spin_lock(&sig_lock);
	list_insert(rets_from_sig, (process_t *)current_process);
	spin_unlock(&sig_lock);

	switch_next();
}

void fix_signal_stacks(void) {
	uint8_t redo_me = 0;
	if (rets_from_sig) {
		spin_lock(&sig_lock_b);
		while (rets_from_sig->head) {
			spin_lock(&sig_lock);
			node_t * n = list_dequeue(rets_from_sig);
			spin_unlock(&sig_lock);
			if (!n) {
				continue;
			}
			process_t * p = n->value;
			free(n);
			if (p == current_process) {
				redo_me = 1;
				continue;
			}
			p->thread.esp = p->signal_state.esp;
			p->thread.eip = p->signal_state.eip;
			p->thread.ebp = p->signal_state.ebp;
			memcpy((void *)(p->image.stack - KERNEL_STACK_SIZE), p->signal_kstack, KERNEL_STACK_SIZE);
			free(p->signal_kstack);
			p->signal_kstack = NULL;
			make_process_ready(p);
		}
		spin_unlock(&sig_lock_b);
	}
	if (redo_me) {
		spin_lock(&sig_lock);
		list_insert(rets_from_sig, (process_t *)current_process);
		spin_unlock(&sig_lock);
		switch_next();
	}
}
