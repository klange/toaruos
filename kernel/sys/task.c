/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2011-2014 Kevin Lange
 * Copyright (C) 2012 Markus Schober
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

#define PUSH(stack, type, item) stack -= sizeof(type); \
							*((type *) stack) = item

page_directory_t *kernel_directory;
page_directory_t *current_directory;

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

	/* And store it... */
	dir->physical_address = phys;
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
			if (i * 0x1000 * 1024 < SHM_START) {
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
				if (i * 0x1000 * 1024 < SHM_START) {
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

void release_directory_for_exec(page_directory_t * dir) {
	uint32_t i;
	/* This better be the only owner of this directory... */
	for (i = 0; i < 1024; ++i) {
		if (!dir->tables[i] || (uintptr_t)dir->tables[i] == (uintptr_t)0xFFFFFFFF) {
			continue;
		}
		if (kernel_directory->tables[i] != dir->tables[i]) {
			if (i * 0x1000 * 1024 < USER_STACK_BOTTOM) {
				for (uint32_t j = 0; j < 1024; ++j) {
					if (dir->tables[i]->pages[j].frame) {
						free_frame(&(dir->tables[i]->pages[j]));
					}
				}
				dir->physical_tables[i] = 0;
				free(dir->tables[i]);
				dir->tables[i] = 0;
			}
		}
	}
}

extern char * default_name;

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

uintptr_t frozen_stack = 0;

/*
 * Install multitasking functionality.
 */
void tasking_install(void) {
	IRQ_OFF; /* Disable interrupts */

	debug_print(NOTICE, "Initializing multitasking");

	/* Initialize the process tree */
	initialize_process_tree();
	/* Spawn the initial process */
	current_process = spawn_init();
	kernel_idle_task = spawn_kidle();
	/* Initialize the paging environment */
#if 0
	set_process_environment((process_t *)current_process, current_directory);
#endif
	/* Switch to the kernel directory */
	switch_page_directory(current_process->thread.page_directory);

	frozen_stack = (uintptr_t)valloc(KERNEL_STACK_SIZE);

	/* Reenable interrupts */
	IRQ_RES;
}

/*
 * Fork.
 *
 * @return To the parent: PID of the child; to the child: 0
 */
uint32_t fork(void) {
	IRQ_OFF;

	uintptr_t esp, ebp;

	current_process->syscall_registers->eax = 0;

	/* Make a pointer to the parent process (us) on the stack */
	process_t * parent = (process_t *)current_process;
	assert(parent && "Forked from nothing??");
	/* Clone the current process' page directory */
	page_directory_t * directory = clone_directory(current_directory);
	assert(directory && "Could not allocate a new page directory!");
	/* Spawn a new process from this one */
	debug_print(INFO,"\033[1;32mALLOC {\033[0m");
	process_t * new_proc = spawn_process(current_process, 0);
	debug_print(INFO,"\033[1;32m}\033[0m");
	assert(new_proc && "Could not allocate a new process!");
	/* Set the new process' page directory to clone */
	set_process_environment(new_proc, directory);

	struct regs r;
	memcpy(&r, current_process->syscall_registers, sizeof(struct regs));
	new_proc->syscall_registers = &r;

	esp = new_proc->image.stack;
	ebp = esp;

	new_proc->syscall_registers->eax = 0;

	PUSH(esp, struct regs, r);

	new_proc->thread.esp = esp;
	new_proc->thread.ebp = ebp;

	new_proc->is_tasklet = parent->is_tasklet;

	new_proc->thread.eip = (uintptr_t)&return_to_userspace;

	/* Add the new process to the ready queue */
	make_process_ready(new_proc);

	IRQ_RES;

	/* Return the child PID */
	return new_proc->id;
}

int create_kernel_tasklet(tasklet_t tasklet, char * name, void * argp) {
	IRQ_OFF;

	uintptr_t esp, ebp;

	if (current_process->syscall_registers) {
		current_process->syscall_registers->eax = 0;
	}

	page_directory_t * directory = kernel_directory;
	/* Spawn a new process from this one */
	process_t * new_proc = spawn_process(current_process, 0);
	assert(new_proc && "Could not allocate a new process!");
	/* Set the new process' page directory to the original process' */
	set_process_environment(new_proc, directory);
	directory->ref_count++;
	/* Read the instruction pointer */


	if (current_process->syscall_registers) {
		struct regs r;
		memcpy(&r, current_process->syscall_registers, sizeof(struct regs));
		new_proc->syscall_registers = &r;
	}

	esp = new_proc->image.stack;
	ebp = esp;

	if (current_process->syscall_registers) {
		new_proc->syscall_registers->eax = 0;
	}
	new_proc->is_tasklet = 1;
	new_proc->name = name;

	PUSH(esp, uintptr_t, (uintptr_t)name);
	PUSH(esp, uintptr_t, (uintptr_t)argp);
	PUSH(esp, uintptr_t, (uintptr_t)&task_exit);

	new_proc->thread.esp = esp;
	new_proc->thread.ebp = ebp;

	new_proc->thread.eip = (uintptr_t)tasklet;

	/* Add the new process to the ready queue */
	make_process_ready(new_proc);

	IRQ_RES;

	/* Return the child PID */
	return new_proc->id;
}


/*
 * clone the current thread and create a new one in the same
 * memory space with the given pointer as its new stack.
 */
uint32_t
clone(uintptr_t new_stack, uintptr_t thread_func, uintptr_t arg) {
	uintptr_t esp, ebp;

	IRQ_OFF;

	current_process->syscall_registers->eax = 0;

	/* Make a pointer to the parent process (us) on the stack */
	process_t * parent = (process_t *)current_process;
	assert(parent && "Cloned from nothing??");
	page_directory_t * directory = current_directory;
	/* Spawn a new process from this one */
	process_t * new_proc = spawn_process(current_process, 1);
	assert(new_proc && "Could not allocate a new process!");
	/* Set the new process' page directory to the original process' */
	set_process_environment(new_proc, directory);
	directory->ref_count++;
	/* Read the instruction pointer */

	struct regs r;
	memcpy(&r, current_process->syscall_registers, sizeof(struct regs));
	new_proc->syscall_registers = &r;

	esp = new_proc->image.stack;
	ebp = esp;

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
	PUSH(new_stack, uintptr_t, arg);
	PUSH(new_stack, uintptr_t, THREAD_RETURN);

	/* Set esp, ebp, and eip for the new thread */
	new_proc->syscall_registers->esp = new_stack;
	new_proc->syscall_registers->useresp = new_stack;

	PUSH(esp, struct regs, r);

	new_proc->thread.esp = esp;
	new_proc->thread.ebp = ebp;

	new_proc->is_tasklet = parent->is_tasklet;

	new_proc->thread.eip = (uintptr_t)&return_to_userspace;

	/* Add the new process to the ready queue */
	make_process_ready(new_proc);

	IRQ_RES;

	/* Return the child PID */
	return new_proc->id;
}

/*
 * Get the process ID of the current process.
 *
 * @return The PID of the current process.
 */
uint32_t getpid(void) {
	/* Fairly self-explanatory. */
	return current_process->id;
}

/*
 * Switch to the next ready task.
 *
 * This is called from the interrupt handler for the interval timer to
 * perform standard task switching.
 */
void switch_task(uint8_t reschedule) {
	if (!current_process) {
		/* Tasking is not yet installed. */
		return;
	}
	if (!current_process->running) {
		switch_next();
	}

	/* Collect the current kernel stack and instruction pointers */
	uintptr_t esp, ebp, eip;
	asm volatile ("mov %%esp, %0" : "=r" (esp));
	asm volatile ("mov %%ebp, %0" : "=r" (ebp));
	eip = read_eip();
	if (eip == 0x10000) {
		/* Returned from EIP after task switch, we have
		 * finished switching. */
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
	current_process->running = 0;

	/* Save floating point state */
	switch_fpu();

	if (reschedule && current_process != kernel_idle_task) {
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
void switch_next(void) {
	uintptr_t esp, ebp, eip;
	/* Get the next available process */
	current_process = next_ready_process();
	/* Retreive the ESP/EBP/EIP */
	eip = current_process->thread.eip;
	esp = current_process->thread.esp;
	ebp = current_process->thread.ebp;

	/* Validate */
	if ((eip < (uintptr_t)&code) || (eip > (uintptr_t)heap_end)) {
		debug_print(WARNING, "Skipping broken process %d! [eip=0x%x <0x%x or >0x%x]", current_process->id, eip, &code, &end);
		switch_next();
	}

	if (current_process->finished) {
		debug_print(WARNING, "Tried to switch to process %d, but it claims it is finished.", current_process->id);
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

	current_process->running = 1;

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

extern void enter_userspace(uintptr_t location, uintptr_t stack);

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

	PUSH(stack, uintptr_t, (uintptr_t)argv);
	PUSH(stack, int, argc);
	enter_userspace(location, stack);
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
	cleanup_process((process_t *)current_process, retval);

	process_t * parent = process_get_parent((process_t *)current_process);

	if (parent) {
		wakeup_queue(parent->wait_queue);
	}

	switch_next();
}

/*
 * Call task_exit() and immediately STOP if we can't.
 */
void kexit(int retval) {
	task_exit(retval);
	debug_print(CRITICAL, "Process returned from task_exit! Environment is definitely unclean. Stopping.");
	STOP;
}
