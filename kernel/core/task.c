/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Task Switching and Management Functions
 *
 */
#include <system.h>
#include <process.h>

uint32_t next_pid = 0;

/*
 * Clone a page directory and its contents.
 * (If you do not intend to clone the contents, do it yourself!)
 * 
 * @param  src Pointer to source directory to clone from.
 * @return A pointer to a new directory.
 */
page_directory_t *
clone_directory(
		page_directory_t * src
		) {
	/* Allocate a new page directory */
	uintptr_t phys;
	page_directory_t * dir = (page_directory_t *)kvmalloc_p(sizeof(page_directory_t), &phys);
	/* Clear it out */
	memset(dir, 0, sizeof(page_directory_t));
	/* Calculate the physical address offset */
	uintptr_t offset = (uintptr_t)dir->physical_tables - (uintptr_t)dir;
	/* And store it... */
	dir->physical_address = phys + offset;
	uint32_t i;
	for (i = 0; i < 1024; ++i) {
		/* Copy each table */
		if (!src->tables[i] || (uintptr_t)src->tables[i] == (uintptr_t)0xFFFFFFFF) {
			continue;
		}
		if (kernel_directory->tables[i] == src->tables[i]) {
			/* Kernel tables are simply linked together */
			dir->tables[i] = src->tables[i];
			dir->physical_tables[i] = src->physical_tables[i];
		} else {
			/* User tables must be cloned */
			uintptr_t phys;
			dir->tables[i] = clone_table(src->tables[i], &phys);
			dir->physical_tables[i] = phys | 0x07;
		}
	}
	return dir;
}

/*
 * Clone a page table
 *
 * @param src      Pointer to a page table to clone.
 * @param physAddr [out] Pointer to the physical address of the new page table
 * @return         A pointer to a new page table.
 */
page_table_t *
clone_table(
		page_table_t * src,
		uintptr_t * physAddr
		) {
	/* Allocate a new page table */
	page_table_t * table = (page_table_t *)kvmalloc_p(sizeof(page_table_t), physAddr);
	memset(table, 0, sizeof(page_table_t));
	uint32_t i;
	for (i = 0; i < 1024; ++i) {
		/* For each frame in the table... */
		if (!src->pages[i].frame) {
			continue;
		}
		/* Allocate a new frame */
		alloc_frame(&table->pages[i], 0, 0);
		/* Set the correct access bit */
		if (src->pages[i].present)	table->pages[i].present = 1;
		if (src->pages[i].rw)		table->pages[i].rw = 1;
		if (src->pages[i].user)		table->pages[i].user = 1;
		if (src->pages[i].accessed)	table->pages[i].accessed = 1;
		if (src->pages[i].dirty)	table->pages[i].dirty = 1;
		/* Copy the contents of the page from the old table to the new one */
		copy_page_physical(src->pages[i].frame * 0x1000, table->pages[i].frame * 0x1000);
	}
	return table;
}

/*
 * Install multitasking functionality.
 */
void
tasking_install() {
	IRQ_OFF; /* Disable interrupts */

	/* Initialize the process tree */
	initialize_process_tree();
	/* Spawn the initial process */
	current_process = spawn_init();
	/* Initialize the paging environment */
	set_process_environment((process_t *)current_process, current_directory);
	/* Switch to the kernel directory */
	switch_page_directory(current_process->thread.page_directory);

	/* Reenable interrupts */
	IRQ_ON;
}

/*
 * Fork.
 *
 * @return To the parent: PID of the child; to the child: 0
 */
uint32_t
fork() {
	/* Disable interrupts */
	IRQ_OFF;

	/* Make a pointer to the parent process (us) on the stack */
	process_t * parent = (process_t *)current_process;
	/* Clone the current process' page directory */
	page_directory_t * directory = clone_directory(current_directory);
	/* Spawn a new process from this one */
	process_t * new_proc = spawn_process(current_process);
	/* Set the new process' page directory to clone */
	set_process_environment(new_proc, directory);

	/* Read the instruction pointer */
	uintptr_t eip = read_eip();

	if (current_process == parent) {
		/* Returned as the parent */
		uintptr_t esp;
		uintptr_t ebp;
		/* Collect the stack and base pointers */
		asm volatile ("mov %%esp, %0" : "=r" (esp));
		asm volatile ("mov %%ebp, %0" : "=r" (ebp));
		/* Calculate new ESP and EBP for the child process */
		if (current_process->image.stack > new_proc->image.stack) {
			new_proc->thread.esp = esp - (current_process->image.stack - new_proc->image.stack);
			new_proc->thread.ebp = ebp - (current_process->image.stack - new_proc->image.stack);
		} else {
			new_proc->thread.esp = esp + (new_proc->image.stack - current_process->image.stack);
			new_proc->thread.ebp = ebp - (current_process->image.stack - new_proc->image.stack);
		}
		/* Copy the kernel stack from this process to new process */
		memcpy((void *)(new_proc->image.stack - KERNEL_STACK_SIZE), (void *)(current_process->image.stack - KERNEL_STACK_SIZE), KERNEL_STACK_SIZE);
		/* Set the new process instruction pointer (to the return from read_eip) */
		new_proc->thread.eip = eip;
		/* Add the new process to the ready queue */
		make_process_ready(new_proc);
		/* Reenable interrupts */
		IRQ_ON;
		/* Return the child PID */
		return new_proc->id;
	} else {
		/* Child fork is complete, return */
		return 0;
	}
}

/*
 * Get the process ID of the current process.
 *
 * @return The PID of the current process.
 */
uint32_t
getpid() {
	/* Fairly self-explanatory. */
	return current_process->id;
}

/*
 * Switch to the next ready task.
 *
 * This is called from the interrupt handler for the interval timer to
 * perform standard task switching.
 */
void
switch_task() {
	if (!current_process) {
		/* Tasking is not yet installed. */
		return;
	}
	if (!process_available()) {
		/* There is no process available in the queue, do not bother switching */
		return;
	}

	/* Collect the current kernel stack and instruction pointers */
	uintptr_t esp, ebp, eip;
	asm volatile ("mov %%esp, %0" : "=r" (esp));
	asm volatile ("mov %%ebp, %0" : "=r" (ebp));
	eip = read_eip();
	if (eip == 0x10000) {
		/* Returned from EIP after task switch, we have
		 * finished switching. */
		IRQ_ON;
		return;
	}

	/* Remember this process' ESP/EBP/EIP */
	current_process->thread.eip = eip;
	current_process->thread.esp = esp;
	current_process->thread.ebp = ebp;
	/* And reinsert it into the ready queue */
	make_process_ready((process_t *)current_process);

	/* Switch to the next task */
	switch_next();
}

/*
 * Immediately switch to the next task.
 *
 * Does not store the ESP/EBP/EIP of the current thread.
 */
void
switch_next() {
	uintptr_t esp, ebp, eip;
	/* Get the next available process */
	current_process = next_ready_process();
	/* Retreive the ESP/EBP/EIP */
	eip = current_process->thread.eip;
	esp = current_process->thread.esp;
	ebp = current_process->thread.ebp;
	/* Disable interrupts */
	IRQ_OFF;
	/* Set the page directory */
	current_directory = current_process->thread.page_directory;
	/* Set the kernel stack in the TSS */
	set_kernel_stack(current_process->image.stack);
	/* Jump, baby, jump */
	asm volatile (
			"mov %0, %%ebx\n"
			"mov %1, %%esp\n"
			"mov %2, %%ebp\n"
			"mov %3, %%cr3\n"
			"mov $0x10000, %%eax\n" /* read_eip() will return 0x10000 */
			"sti\n" /* Reenable interrupts */
			"jmp *%%ebx"
			: : "r" (eip), "r" (esp), "r" (ebp), "r" (current_directory->physical_address)
			: "%ebx", "%esp", "%eax");

}

/*
 * Enter ring 3 and jump to `location`.
 *
 * @param location Address to jump to in user space
 * @param argc     Argument count
 * @param argv     Argument pointers
 * @param stack    Userspace stack address
 */
void
enter_user_jmp(uintptr_t location, int argc, char ** argv, uintptr_t stack) {
	set_kernel_stack(current_process->image.stack);
	asm volatile(
			"mov %3, %%esp\n"
			"pushl $0\n"           /* Push the null terminator  */
			"pushl %2\n"           /* Push the argument pointer */
			"pushl %1\n"           /*          argument count   */
			"pushl $1\n"           /* [backwards-compatibility] */
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
			: : "m"(location), "m"(argc), "m"(argv), "r"(stack) : "%ax", "%esp", "%eax");
}

/*
 * Dequeue the current task and set it as finished
 *
 * @param retval Set the return value to this.
 */
void task_exit(int retval) {
	IRQ_OFF;
	/* Free the image memory */
	current_process->status   = retval;
	current_process->finished = 1;
#if 0
	/*
	 * These things should be done by another thread.
	 */
#if 0
	for (uintptr_t i = 0; i < current_process->image.size; i += 0x1000) {
		free_frame(get_page(current_process->image.entry + i, 0, current_process->image.page_directory));
	}
#endif
	free((void *)(current_process->image.stack - KERNEL_STACK_SIZE));
	free((void *)current_process->thread.page_directory);
	free((void *)current_process->fds.entries);
	free((void *)current_process);
#endif
	switch_next();
}

/*
 * Call task_exit() and immediately STOP if we can't.
 */
void kexit(int retval) {
	task_exit(retval);
	STOP;
}
