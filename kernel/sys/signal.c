/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Signal Handling
 */

#include <system.h>
#include <signal.h>

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

void handle_signal(process_t * proc, signal_t * sig) {
	if (proc->finished) {
		return;
	}

	if (sig->signum == 0 || sig->signum > NUMSIGNALS) {
		/* Ignore */
		return;
	}

	if (!sig->handler) {
		kprintf("[debug] Process %d killed by unhandled signal (%d).\n", proc->id, sig->signum);
		kexit(128 + sig->signum);
		__builtin_unreachable();
		return;
	}

	if (sig->handler == 1) /* Ignore */ {
		return;
	}

	uintptr_t stack = 0xFFFF0000;
	if (proc->syscall_registers->useresp < 0x10000100) {
		stack = proc->image.user_stack;
	} else {
		stack = proc->syscall_registers->useresp;
	}

	/* Not marked as ignored, must call signal */
	enter_signal_handler(sig->handler, sig->signum, stack);

}

list_t * rets_from_sig;

void return_from_signal_handler() {
#if 0
	kprintf("[debug] Return From Signal for process %d\n", current_process->id);
#endif

	if (__builtin_expect(!rets_from_sig, 0)) {
		rets_from_sig = list_create();
	}

	list_insert(rets_from_sig, (process_t *)current_process);

	switch_next();
}

void fix_signal_stacks() {
	uint8_t redo_me = 0;
	if (rets_from_sig) {
		while (rets_from_sig->head) {
			node_t * n = list_dequeue(rets_from_sig);
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
			make_process_ready(p);
		}
	}
	if (redo_me) {
		list_insert(rets_from_sig, (process_t *)current_process);
	}
}
