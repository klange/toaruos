/*
 * vim:tabstop=4
 * vim:noexpandtab
 */
#include <system.h>
#define KERNEL_STACK_SIZE 0x2000

__volatile__ task_t * current_task = NULL;
__volatile__ task_t * ready_queue  = NULL;

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
		if (!src->tables[i]) {
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
	__asm__ __volatile__ ("cli");

	current_task = (task_t *)kmalloc(sizeof(task_t));
	ready_queue = current_task;
	current_task->id  = next_pid++;
	current_task->esp = 0;
	current_task->ebp = 0;
	current_task->eip = 0;
	current_task->stack = initial_esp;
	current_task->page_directory = current_directory; //clone_directory(current_directory);
	current_task->next = 0;

	current_task->descriptors = (fs_node_t **)kmalloc(sizeof(fs_node_t *) * 1024);
	current_task->next_fd = 0;

	//switch_page_directory(current_task->page_directory);

	__asm__ __volatile__ ("sti");
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
	__asm__ __volatile__ ("cli");
	task_t * parent = (task_t *)current_task;
	page_directory_t * directory = clone_directory(current_directory);
	task_t * new_task = (task_t *)kmalloc(sizeof(task_t));
	new_task->id  = next_pid++;
	new_task->esp = 0;
	new_task->ebp = 0;
	new_task->eip = 0;
	new_task->page_directory = directory;
	new_task->next = NULL;
	new_task->stack = kvmalloc(KERNEL_STACK_SIZE);
	new_task->descriptors = (fs_node_t **)kmalloc(sizeof(fs_node_t *) * 1024);
	memcpy(new_task->descriptors, parent->descriptors, sizeof(fs_node_t *) * 1024);
	new_task->next_fd = 0;
	new_task->finished = 0;
	new_task->image_size = 0;
	new_task->entry = 0xFFFFFFFF;
	task_t * tmp_task = (task_t *)ready_queue;
	new_task->parent = parent;
	while (tmp_task->next) {
		tmp_task = tmp_task->next;
	}
	tmp_task->next = new_task;
	uintptr_t eip = read_eip();
	if (current_task == parent) {
		uintptr_t esp;
		uintptr_t ebp;
		__asm__ __volatile__ ("mov %%esp, %0" : "=r" (esp));
		__asm__ __volatile__ ("mov %%ebp, %0" : "=r" (ebp));
		signed int old_stack_offset;
		if (current_task->stack > new_task->stack) {
			old_stack_offset = -(current_task->stack - new_task->stack);
		} else {
			old_stack_offset = new_task->stack - current_task->stack;
		}
		new_task->esp = esp + old_stack_offset;
		memcpy((void *)(new_task->esp),(void*)esp,current_task->stack + KERNEL_STACK_SIZE - esp);
		new_task->ebp = ebp + old_stack_offset;
		new_task->eip = eip;
		__asm__ __volatile__ ("sti");
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
	__asm__ __volatile__ ("mov %%esp, %0" : "=r" (esp));
	__asm__ __volatile__ ("mov %%ebp, %0" : "=r" (ebp));
	eip = read_eip();
	if (eip == 0x10000) {
		__asm__ __volatile__ ("sti");
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
		HALT_AND_CATCH_FIRE("Empty ready queue!", NULL);
	}
	eip = current_task->eip;
	esp = current_task->esp;
	ebp = current_task->ebp;
	__asm__ __volatile__ (
			"cli\n"
			"mov %0, %%ecx\n"
			"mov %1, %%esp\n"
			"mov %2, %%ebp\n"
			"mov %3, %%cr3\n"
			"mov $0x10000, %%eax\n"
			"sti\n"
			"jmp *%%ecx"
			: : "r" (eip), "r" (esp), "r" (ebp), "r" (current_directory->physical_address));
	switch_page_directory(current_task->page_directory);
}

void
enter_user_mode() {
	set_kernel_stack(current_task->stack + KERNEL_STACK_SIZE);
	__asm__ __volatile__(
			"mov $0x23, %ax\n"
			"mov %ax, %ds\n"
			"mov %ax, %es\n"
			"mov %ax, %fs\n"
			"mov %ax, %gs\n"
			"mov %esp, %eax\n"
			"pushl $0x23\n"
			"pushl %eax\n"
			"pushf\n"
			"popl %eax\n"
			"orl  $0x200, %eax\n"
			"pushl %eax\n"
			"pushl $0x1B\n"
			"push $1f\n"
			"iret\n"
		"1:\n"
			"mov %ax, %ax\n");
}

void
enter_user_jmp(uintptr_t location, int argc, char ** argv) {
	set_kernel_stack(current_task->stack + KERNEL_STACK_SIZE);
	__asm__ __volatile__(
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
			: : "m"(location), "m"(argc), "m"(argv));
}

void task_exit(int retval) {
	__asm__ __volatile__ ("cli");
	current_task->retval   = retval;
	current_task->finished = 1;
	/* Free the image memory */
	for (uintptr_t i = 0; i < current_task->image_size; i += 0x1000) {
		free_frame(get_page(current_task->entry + i, 0, current_directory));
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
	free((void *)current_task->stack);
	free((void *)current_task->page_directory);
	free((void *)current_task->descriptors);
	//free((void *)current_task);
	__asm__ __volatile__ ("sti");
}

void kexit(int retval) {
	task_exit(retval);
	while (1) {
		__asm__ __volatile__("hlt");
	}
}
