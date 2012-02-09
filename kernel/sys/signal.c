/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Signal Handling
 */

#include <system.h>
#include <signal.h>

void enter_signal_handler(uintptr_t location, int signum, uintptr_t stack) {
	IRQ_OFF;
	kprintf("[debug] Jumping to 0x%x with %d pushed and a stack at 0x%x\n", location, signum, stack);
	asm volatile(
			"mov %2, %%esp\n"
			"pushl %1\n"           /*          argument count   */
			"pushl $0xFFFFFFFF\n"
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
	kprintf("Yep, definitely an iret issue.\n");
}

void handle_signal(process_t * proc, signal_t * sig) {
	kprintf("[signal] Need to process signal %d for process %d\n", sig->signum, proc->id);

	if (!sig->handler) {
		kprintf("[debug] Process %d killed by unhandled signal.\n", proc->id);
		kprintf("Current process = %d\n", current_process->id);
		kexit(128 + sig->signum);
		kprintf("Still here.\n");
		return;
	}

	if (sig->handler == 1) /* Ignore */ {
		return;
	}

	uintptr_t stack = 0x100EFFFF;

	/* Not marked as ignored, must call signal */
	enter_signal_handler(sig->handler, sig->signum, stack);

}

list_t * rets_from_sig;

void return_from_signal_handler() {
	kprintf("[debug] Return From Signal for process %d\n", current_process->id);

	if (__builtin_expect(!rets_from_sig, 0)) {
		rets_from_sig = list_create();
	}

	list_insert(rets_from_sig, (process_t *)current_process);

	switch_next();
}

void fix_signal_stacks() {
	if (rets_from_sig) {
		while (rets_from_sig->head) {
			node_t * n = list_dequeue(rets_from_sig);
			process_t * p = n->value;
			p->thread.esp = p->signal_state.esp;
			p->thread.eip = p->signal_state.eip;
			p->thread.ebp = p->signal_state.ebp;
			memcpy((void *)(p->image.stack - KERNEL_STACK_SIZE), p->signal_kstack, KERNEL_STACK_SIZE);
			free(p->signal_kstack);
			make_process_ready(p);
			free(n);
		}
	}
}
