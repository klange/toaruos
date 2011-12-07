/* vim: tabstop=4 shiftwidth=4 noexpandtab
 */
#include <system.h>
#define KERNEL_STACK_SIZE 0x2000

volatile task_t * current_task = NULL;
volatile task_t * ready_queue  = NULL;

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

page_table_t *
clone_table(
		page_table_t * src,
		uintptr_t * physAddr
		) {
	page_table_t * table = (page_table_t *)kvmalloc_p(sizeof(page_table_t), physAddr);
	memset(table, 0, sizeof(page_directory_t));
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

	current_task = (task_t *)kmalloc(sizeof(task_t));
	ready_queue = current_task;
	current_task->id  = next_pid++;
	current_task->esp = 0;
	current_task->ebp = 0;
	current_task->eip = 0;
	current_task->stack = initial_esp + 1;
	current_task->page_directory = current_directory; //clone_directory(current_directory);
	current_task->next = 0;

	current_task->descriptors = (fs_node_t **)kmalloc(sizeof(fs_node_t *) * 1024);
	current_task->next_fd = 0;
	current_task->wd[0] = '/';
	current_task->wd[1] = 0;

	switch_page_directory(current_task->page_directory);

	IRQ_ON;
}

task_t *
gettask(
		uint32_t pid
	   ) {
	task_t * output = (task_t *)ready_queue;
	while (output != NULL && output->id != pid) {
		output = output->next;
	}
	return output;
}

uint32_t
fork() {
	IRQ_OFF;
	task_t * parent = (task_t *)current_task;
	page_directory_t * directory = clone_directory(current_directory);
	task_t * new_task = (task_t *)kmalloc(sizeof(task_t));
	new_task->id  = next_pid++;
	new_task->esp = 0;
	new_task->ebp = 0;
	new_task->eip = 0;
	new_task->page_directory = directory;
	new_task->next = NULL;
	new_task->stack = kvmalloc(KERNEL_STACK_SIZE) + KERNEL_STACK_SIZE;
	new_task->descriptors = (fs_node_t **)kmalloc(sizeof(fs_node_t *) * 1024);
	memcpy(new_task->descriptors, parent->descriptors, sizeof(fs_node_t *) * 1024);
	new_task->next_fd = 0;
	new_task->finished = 0;
	new_task->image_size = 0;
	/* Some stuff */
	new_task->entry  = current_task->entry;
	new_task->heap   = current_task->heap;
	new_task->heap_a = current_task->heap_a;
	new_task->image_size = current_task->image_size;
	for (uint32_t i = 0; i <= strlen((const char *)current_task->wd); ++i) {
		new_task->wd[i] = current_task->wd[i];
	}

	new_task->parent = parent;
	task_t * tmp_task = (task_t *)ready_queue;
	while (tmp_task->next) {
		tmp_task = tmp_task->next;
	}
	tmp_task->next = new_task;
	uintptr_t eip = read_eip();
	if (current_task == parent) {
		uintptr_t esp;
		uintptr_t ebp;
		asm volatile ("mov %%esp, %0" : "=r" (esp));
		asm volatile ("mov %%ebp, %0" : "=r" (ebp));
		if (current_task->stack > new_task->stack) {
			new_task->esp = esp - (current_task->stack - new_task->stack);
			new_task->ebp = ebp - (current_task->stack - new_task->stack);
		} else {
			new_task->esp = esp + (new_task->stack - current_task->stack);
			new_task->ebp = ebp - (current_task->stack - new_task->stack);
		}
		// kprintf("old: %x new: %x; end: %x %x\n", esp, new_task->esp, current_task->stack, new_task->stack);
		memcpy((void *)(new_task->stack - KERNEL_STACK_SIZE), (void *)(current_task->stack - KERNEL_STACK_SIZE), KERNEL_STACK_SIZE);
		new_task->eip = eip;
		IRQ_ON;
		return new_task->id;
	} else {
		return 0;
	}
}

uint32_t
getpid() {
	return current_task->id;
}

void
switch_task() {
	if (!current_task) {
		return;
	}
	if (!current_task->next && current_task == ready_queue) return;

	uintptr_t esp, ebp, eip;
	asm volatile ("mov %%esp, %0" : "=r" (esp));
	asm volatile ("mov %%ebp, %0" : "=r" (ebp));
	eip = read_eip();
	if (eip == 0x10000) {
		IRQ_ON;
		return;
	}
	current_task->eip = eip;
	current_task->esp = esp;
	current_task->ebp = ebp;
	current_task = current_task->next;
	if (!current_task) {
		current_task = ready_queue;
	}
	if (!current_task) {
		kprintf("Ran out of processes to run. Halting!\n");
		STOP;
	}
	eip = current_task->eip;
	esp = current_task->esp;
	ebp = current_task->ebp;
	IRQ_OFF;
	current_directory = current_task->page_directory;
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
	set_kernel_stack(current_task->stack);
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
	for (uintptr_t i = 0; i < current_task->image_size; i += 0x1000) {
#if 0
		if (kernel_directory->tables[i] != current_task->page_directory->tables[i] && current_task->page_directory->tables[i] != 0xFFFFFFFF) {
			//free_frame(get_page(current_task->entry + i, 0, current_task->page_directory));
		}
#endif
	}
	/* Dequeue us */
	task_t volatile * temp = ready_queue;
	task_t volatile * prev = NULL;
	while (temp != current_task && temp != NULL) {
		prev = temp;
		temp = temp->next;
	}
	if (prev == NULL) {
		ready_queue = current_task->next;
	} else {
		prev->next = current_task->next;
	}
	current_task->retval   = retval;
	current_task->finished = 1;
	//free((void *)(current_task->stack - KERNEL_STACK_SIZE));
	//free((void *)current_task->page_directory);
	//free((void *)current_task->descriptors);
	//free((void *)current_task);
	IRQ_ON;
}

void kexit(int retval) {
	task_exit(retval);
	STOP;
}
