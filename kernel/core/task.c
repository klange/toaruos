/* vim: tabstop=4 shiftwidth=4 noexpandtab
 */
#include <system.h>
#include <process.h>

uint32_t next_pid = 0;

page_directory_t *
clone_directory(
		page_directory_t * src
		) {
	uintptr_t phys;
	page_directory_t * dir = (page_directory_t *)kvmalloc_p(sizeof(page_directory_t), &phys);
	memset(dir, 0, sizeof(page_directory_t));
	uintptr_t offset = (uintptr_t)dir->physical_tables - (uintptr_t)dir;
	dir->physical_address = phys + offset;
	uint32_t i;
	for (i = 0; i < 1024; ++i) {
		if (!src->tables[i] || (uintptr_t)src->tables[i] == (uintptr_t)0xFFFFFFFF) {
			continue;
		}
		if (kernel_directory->tables[i] == src->tables[i]) {
			dir->tables[i] = src->tables[i];
			dir->physical_tables[i] = src->physical_tables[i];
		} else {
			uintptr_t phys;
			dir->tables[i] = clone_table(src->tables[i], &phys);
			dir->physical_tables[i] = phys | 0x07;
		}
	}
	return dir;
}

void assertDir(page_directory_t * src) {
	for (uint32_t i = 0; i < 1024; ++i) {
		if (!src->tables[i] || (uintptr_t)src->tables[i] == (uintptr_t)0xFFFFFFFF) {
			continue;
		}
		assert(((uintptr_t)src->tables[i] >= 0x00010000) && "Failure!");
	}
}


page_table_t *
clone_table(
		page_table_t * src,
		uintptr_t * physAddr
		) {
	page_table_t * table = (page_table_t *)kvmalloc_p(sizeof(page_table_t), physAddr);
	memset(table, 0, sizeof(page_table_t));
	uint32_t i;
	for (i = 0; i < 1024; ++i) {
		if (!src->pages[i].frame) {
			continue;
		}
		alloc_frame(&table->pages[i], 0, 0);
		if (src->pages[i].present)	table->pages[i].present = 1;
		if (src->pages[i].rw)		table->pages[i].rw = 1;
		if (src->pages[i].user)		table->pages[i].user = 1;
		if (src->pages[i].accessed)	table->pages[i].accessed = 1;
		if (src->pages[i].dirty)	table->pages[i].dirty = 1;
		copy_page_physical(src->pages[i].frame * 0x1000, table->pages[i].frame * 0x1000);
	}
	return table;
}

void
tasking_install() {
	IRQ_OFF;

	initialize_process_tree();
	current_process = spawn_init();
	set_process_environment((process_t *)current_process, current_directory);

	switch_page_directory(current_process->thread.page_directory);

	IRQ_ON;
}

uint32_t
fork() {
	IRQ_OFF;
	process_t * parent = (process_t *)current_process;
	page_directory_t * directory = clone_directory(current_directory);

	process_t * new_proc = spawn_process(current_process);
	set_process_environment(new_proc, directory);

	uintptr_t eip = read_eip();
	if (current_process == parent) {
		uintptr_t esp;
		uintptr_t ebp;
		asm volatile ("mov %%esp, %0" : "=r" (esp));
		asm volatile ("mov %%ebp, %0" : "=r" (ebp));
		if (current_process->image.stack > new_proc->image.stack) {
			new_proc->thread.esp = esp - (current_process->image.stack - new_proc->image.stack);
			new_proc->thread.ebp = ebp - (current_process->image.stack - new_proc->image.stack);
		} else {
			new_proc->thread.esp = esp + (new_proc->image.stack - current_process->image.stack);
			new_proc->thread.ebp = ebp - (current_process->image.stack - new_proc->image.stack);
		}
		memcpy((void *)(new_proc->image.stack - KERNEL_STACK_SIZE), (void *)(current_process->image.stack - KERNEL_STACK_SIZE), KERNEL_STACK_SIZE);
		new_proc->thread.eip = eip;
		make_process_ready(new_proc);
		IRQ_ON;
		return new_proc->id;
	} else {
		return 0;
	}
}

uint32_t
getpid() {
	return current_process->id;
}

void
switch_task() {
	if (!current_process) {
		return;
	}
	if (!process_available()) {
		return;
	}

	uintptr_t esp, ebp, eip;
	asm volatile ("mov %%esp, %0" : "=r" (esp));
	asm volatile ("mov %%ebp, %0" : "=r" (ebp));
	eip = read_eip();
	if (eip == 0x10000) {
		IRQ_ON;
		return;
	}

	current_process->thread.eip = eip;
	current_process->thread.esp = esp;
	current_process->thread.ebp = ebp;
	make_process_ready((process_t *)current_process);

	switch_next();
}

void
switch_next() {
	uintptr_t esp, ebp, eip;
	current_process = next_ready_process();
	eip = current_process->thread.eip;
	esp = current_process->thread.esp;
	ebp = current_process->thread.ebp;
	IRQ_OFF;
	current_directory = current_process->thread.page_directory;
	//assertDir(current_directory);
	asm volatile (
			"mov %0, %%ebx\n"
			"mov %1, %%esp\n"
			"mov %2, %%ebp\n"
			"mov %3, %%cr3\n"
			"mov $0x10000, %%eax\n"
			"sti\n"
			"jmp *%%ebx"
			: : "r" (eip), "r" (esp), "r" (ebp), "r" (current_directory->physical_address)
			: "%ebx", "%esp", "%eax");

}

void
enter_user_jmp(uintptr_t location, int argc, char ** argv, uintptr_t stack) {
	set_kernel_stack(current_process->image.stack);
	asm volatile(
			"mov %3, %%esp\n"
			"mov $0x23, %%ax\n"
			"mov %%ax, %%ds\n"
			"mov %%ax, %%es\n"
			"mov %%ax, %%fs\n"
			"mov %%ax, %%gs\n"
			"mov %%esp, %%eax\n"
			"pushl $0x23\n"
			"pushl %%eax\n"
			"pushf\n"
			"popl %%eax\n"
			"orl  $0x200, %%eax\n"
			"pushl %%eax\n"
			"pushl $0x1B\n"
			"pushl %2\n"
			"pushl %1\n"
			"call  *%0\n"
			: : "m"(location), "m"(argc), "m"(argv), "r"(stack) : "%ax", "%esp", "%eax");
}

void task_exit(int retval) {
	IRQ_OFF;
	/* Free the image memory */
#if 0
	for (uintptr_t i = 0; i < current_process->image.size; i += 0x1000) {
		free_frame(get_page(current_process->image.entry + i, 0, current_process->image.page_directory));
	}
#endif
	current_process->status   = retval;
	current_process->finished = 1;
	//free((void *)(current_process->image.stack - KERNEL_STACK_SIZE));
	//free((void *)current_process->thread.page_directory);
	//free((void *)current_process->fds.entries);
	//free((void *)current_process);
	switch_next();
}

void kexit(int retval) {
	task_exit(retval);
	STOP;
}
