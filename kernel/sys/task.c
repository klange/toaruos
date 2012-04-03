/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Task Switching and Management Functions
 *
 */
#include <system.h>
#include <process.h>
#include <logging.h>
#include <shm.h>
#include <mem.h>

#define TASK_MAGIC 0xDEADBEEF

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
	dir->ref_count = 1;

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
			if (i * 0x1000 * 1024 < 0x20000000) {
				/* User tables must be cloned */
				uintptr_t phys;
				dir->tables[i] = clone_table(src->tables[i], &phys);
				dir->physical_tables[i] = phys | 0x07;
			}
		}
	}
	return dir;
}

/*
 * Free a directory and its tables
 */
void release_directory(page_directory_t * dir) {
	dir->ref_count--;

	if (dir->ref_count < 1) {
		uint32_t i;
		for (i = 0; i < 1024; ++i) {
			if (!dir->tables[i] || (uintptr_t)dir->tables[i] == (uintptr_t)0xFFFFFFFF) {
				continue;
			}
			if (kernel_directory->tables[i] != dir->tables[i]) {
				if (i * 0x1000 * 1024 < 0x20000000) {
					for (uint32_t j = 0; j < 1024; ++j) {
						if (dir->tables[i]->pages[j].frame) {
							free_frame(&(dir->tables[i]->pages[j]));
						}
					}
				}
				free(dir->tables[i]);
			}
		}
		free(dir);
	}
}

extern char * default_name;

void reap_process(process_t * proc) {
	kprintf("Reaping process %d; mem before = %d\n", proc->id, memory_use());
	list_free(proc->wait_queue);
	free(proc->wait_queue);
	list_free(proc->signal_queue);
	free(proc->signal_queue);
	free(proc->wd_name);
	shm_release_all(proc);
	free(proc->shm_mappings);
	if (proc->name != default_name) {
		free(proc->name);
	}
	if (proc->signal_kstack) {
		free(proc->signal_kstack);
	}
	proc->fds->refs--;
	if (proc->fds->refs == 0) {
		release_directory(proc->thread.page_directory);
		for (uint32_t i = 0; i < proc->fds->length; ++i) {
			close_fs(proc->fds->entries[i]);
			/* XXX
			 * We need to actually clean these up, but because
			 * of the way sharing operates, we're not able to do
			 * that yet. WE NEED A BETTER WAY TO SHARE FILE DESCRIPTORS!
			 */
			//free(proc->fds->entries[i]);
		}
		free(proc->fds->entries);
		free(proc->fds);
		free((void *)(proc->image.stack - KERNEL_STACK_SIZE));
	}
	kprintf("Reaped  process %d; mem after = %d\n", proc->id, memory_use());
	// These things are bad!
	//delete_process(proc);
	//free((void *)proc);
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
	blog("Initializing multitasking...");
	IRQ_OFF; /* Disable interrupts */

	LOG(NOTICE, "Initializing multitasking");

	/* Initialize the process tree */
	initialize_process_tree();
	/* Spawn the initial process */
	current_process = spawn_init();
	/* Initialize the paging environment */
	set_process_environment((process_t *)current_process, current_directory);
	/* Switch to the kernel directory */
	switch_page_directory(current_process->thread.page_directory);

	/* Reenable interrupts */
	IRQ_RES;
	bfinish(0);
}

/*
 * Fork.
 *
 * @return To the parent: PID of the child; to the child: 0
 */
uint32_t
fork() {
	IRQ_OFF;

	unsigned int magic = TASK_MAGIC;
	uintptr_t esp, ebp, eip;

	/* Make a pointer to the parent process (us) on the stack */
	process_t * parent = (process_t *)current_process;
	assert(parent && "Forked from nothing??");
	/* Clone the current process' page directory */
	page_directory_t * directory = clone_directory(current_directory);
	assert(directory && "Could not allocate a new page directory!");
	/* Spawn a new process from this one */
	kprintf("\033[1;32mALLOC {\033[0m\n");
	process_t * new_proc = spawn_process(current_process);
	kprintf("\033[1;32m}\033[0m\n");
	assert(new_proc && "Could not allocate a new process!");
	/* Set the new process' page directory to clone */
	set_process_environment(new_proc, directory);
	/* Read the instruction pointer */
	eip = read_eip();

	if (current_process == parent) {
		/* Returned as the parent */
		/* Verify magic */
		assert(magic == TASK_MAGIC && "Bad process fork magic (parent)!");
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

		/* Move the syscall_registers pointer */
		uintptr_t o_stack = ((uintptr_t)current_process->image.stack - KERNEL_STACK_SIZE);
		uintptr_t n_stack = ((uintptr_t)new_proc->image.stack - KERNEL_STACK_SIZE);
		uintptr_t offset  = ((uintptr_t)current_process->syscall_registers - o_stack);
		new_proc->syscall_registers = (struct regs *)(n_stack + offset);

		/* Set the new process instruction pointer (to the return from read_eip) */
		new_proc->thread.eip = eip;

		/* Clear page table tie-ins for shared memory mappings */
#if 0
		assert((new_proc->shm_mappings->length == 0) && "Spawned process had shared memory mappings!");
		foreach (n, current_process->shm_mappings) {
			shm_mapping_t * mapping = (shm_mapping_t *)n->value;

			for (uint32_t i = 0; i < mapping->num_vaddrs; i++) {
				/* Get the vpage address (it's the same for the cloned directory)... */
				uintptr_t vpage = mapping->vaddrs[i];
				assert(!(vpage & 0xFFF) && "shm_mapping_t contained a ptr to the middle of a page (bad)");

				/* ...and from that, the cloned dir's page entry... */
				page_t * page = get_page(vpage, 0, new_proc->thread.page_directory);
				assert(test_frame(page->frame * 0x1000) && "ptr wasn't mapped in?");

				/* ...which refers to a bogus frame that we don't want. */
				clear_frame(page->frame * 0x1000);
				memset(page, 0, sizeof(page_t));
			}
		}
#endif

		/* Add the new process to the ready queue */
		make_process_ready(new_proc);

		IRQ_RES;

		/* Return the child PID */
		return new_proc->id;
	} else {
		assert(magic == TASK_MAGIC && "Bad process fork magic (child)!");
		/* Child fork is complete, return */
		return 0;
	}
}

/*
 * clone the current thread and create a new one in the same
 * memory space with the given pointer as its new stack.
 */
uint32_t
clone(uintptr_t new_stack, uintptr_t thread_func, uintptr_t arg) {
	unsigned int magic = TASK_MAGIC;
	uintptr_t esp, ebp, eip;

	/* Make a pointer to the parent process (us) on the stack */
	process_t * parent = (process_t *)current_process;
	assert(parent && "Cloned from nothing??");
	page_directory_t * directory = current_directory;
	/* Spawn a new process from this one */
	process_t * new_proc = spawn_process(current_process);
	assert(new_proc && "Could not allocate a new process!");
	/* Set the new process' page directory to the original process' */
	set_process_environment(new_proc, directory);
	directory->ref_count++;
	/* Read the instruction pointer */
	eip = read_eip();

	if (current_process == parent) {
		/* Returned as the parent */
		/* Verify magic */
		assert(magic == TASK_MAGIC && "Bad process fork magic (parent clone)!");
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

		/* Move the syscall_registers pointer */
		uintptr_t o_stack = ((uintptr_t)current_process->image.stack - KERNEL_STACK_SIZE);
		uintptr_t n_stack = ((uintptr_t)new_proc->image.stack - KERNEL_STACK_SIZE);
		uintptr_t offset  = ((uintptr_t)current_process->syscall_registers - o_stack);
		new_proc->syscall_registers = (struct regs *)(n_stack + offset);

		/* Set the gid */
		if (current_process->group) {
			new_proc->group = current_process->group;
		} else {
			/* We are the session leader */
			new_proc->group = current_process->id;
		}

		new_proc->syscall_registers->ebp = new_stack;
		new_proc->syscall_registers->eip = thread_func;

		/* Push arg, bogus return address onto the new thread's stack */
		new_stack -= sizeof(uintptr_t);
		*((uintptr_t *)new_stack) = arg;
		new_stack -= sizeof(uintptr_t);
		*((uintptr_t *)new_stack) = THREAD_RETURN;

		/* Set esp, ebp, and eip for the new thread */
		new_proc->syscall_registers->esp = new_stack;
		new_proc->syscall_registers->useresp = new_stack;

		free(new_proc->fds);
		new_proc->fds = current_process->fds;
		new_proc->fds->refs++;

		/* Set the new process instruction pointer (to the return from read_eip) */
		new_proc->thread.eip = eip;
		/* Add the new process to the ready queue */
		make_process_ready(new_proc);

		/* Return the child PID */
		return new_proc->id;
	} else {
		assert(magic == TASK_MAGIC && "Bad process clone magic (child clone)!");
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

void
switch_from_cross_thread_lock() {
	if (!process_available()) {
		IRQS_ON_AND_PAUSE;
	}
	switch_task(1);
}

/*
 * Switch to the next ready task.
 *
 * This is called from the interrupt handler for the interval timer to
 * perform standard task switching.
 */
void
switch_task(uint8_t reschedule) {
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
		while (should_reap()) {
			process_t * proc = next_reapable_process();
			if (proc) {
				reap_process(proc);
			}
		}
		fix_signal_stacks();
		/* XXX: Signals */
		if (!current_process->finished) {
			if (current_process->signal_queue->length > 0) {
				node_t * node = list_dequeue(current_process->signal_queue);
				signal_t * sig = node->value;
				free(node);
				handle_signal((process_t *)current_process, sig);
			}
		}

		return;
	}

	/* Remember this process' ESP/EBP/EIP */
	current_process->thread.eip = eip;
	current_process->thread.esp = esp;
	current_process->thread.ebp = ebp;

	if (reschedule) {
		/* And reinsert it into the ready queue */
		make_process_ready((process_t *)current_process);
	}

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
	while (!process_available()) {
		/* Uh, no. */
		return;
	}
	current_process = next_ready_process();
	/* Retreive the ESP/EBP/EIP */
	eip = current_process->thread.eip;
	esp = current_process->thread.esp;
	ebp = current_process->thread.ebp;

	/* Validate */
	if ((eip < (uintptr_t)&code) || (eip > (uintptr_t)&end)) {
		kprintf("[warning] Skipping broken process %d!\n", current_process->id);
		switch_next();
	}

	if (current_process->finished) {
		switch_next();
	}

	/* Set the page directory */
	current_directory = current_process->thread.page_directory;
	switch_page_directory(current_directory);
	/* Set the kernel stack in the TSS */
	set_kernel_stack(current_process->image.stack);

	if (current_process->started) {
		if (!current_process->signal_kstack) {
			if (current_process->signal_queue->length > 0) {
				current_process->signal_kstack  = malloc(KERNEL_STACK_SIZE);
				current_process->signal_state.esp = current_process->thread.esp;
				current_process->signal_state.eip = current_process->thread.eip;
				current_process->signal_state.ebp = current_process->thread.ebp;
				memcpy(current_process->signal_kstack, (void *)(current_process->image.stack - KERNEL_STACK_SIZE), KERNEL_STACK_SIZE);
			}
		}
	} else {
		current_process->started = 1;
	}

	/* Jump, baby, jump */
	asm volatile (
			"mov %0, %%ebx\n"
			"mov %1, %%esp\n"
			"mov %2, %%ebp\n"
			"mov %3, %%cr3\n"
			"mov $0x10000, %%eax\n" /* read_eip() will return 0x10000 */
			"jmp *%%ebx"
			: : "r" (eip), "r" (esp), "r" (ebp), "r" (current_directory->physical_address)
			: "%ebx", "%esp", "%eax");

}

#define PUSH(stack, type, item) stack -= sizeof(type); \
							*((type *) stack) = item

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
	IRQ_OFF;
	set_kernel_stack(current_process->image.stack);

	/* Push arg, bogus return address onto the new thread's stack */
#if 0
	location -= sizeof(uintptr_t);
	*((uintptr_t *)location) = arg;
	location -= sizeof(uintptr_t);
	*((uintptr_t *)location) = THREAD_RETURN;
#endif

	PUSH(stack, uintptr_t, (uintptr_t)argv);
	PUSH(stack, int, argc);

	asm volatile(
			"mov %3, %%esp\n"
			"pushl $0xDECADE21\n"  /* Magic */
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
			: : "m"(location), "r"(argc), "r"(argv), "r"(stack) : "%ax", "%esp", "%eax");
}

/*
 * Dequeue the current task and set it as finished
 *
 * @param retval Set the return value to this.
 */
void task_exit(int retval) {
	/* Free the image memory */
	if (__builtin_expect(current_process->id == 0,0)) {
		/* This is probably bad... */
		switch_next();
		return;
	}
	current_process->status   = retval;
	current_process->finished = 1;
	kprintf("[%d] Waking up %d processes...\n", getpid(), current_process->wait_queue->length);
	wakeup_queue(current_process->wait_queue);
	make_process_reapable((process_t *)current_process);
	switch_next();
}

/*
 * Call task_exit() and immediately STOP if we can't.
 */
void kexit(int retval) {
	task_exit(retval);
	STOP;
}
