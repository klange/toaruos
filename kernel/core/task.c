/*
 * vim:tabstop=4
 * vim:noexpandtab
 */
#include <system.h>
#define KERNEL_STACK_SIZE 0x1000

__volatile__ task_t * current_task;
__volatile__ task_t * ready_queue;

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
move_stack(
		void *new_stack_start,
		size_t size
		) {
	uintptr_t i;
	for (	i = (uintptr_t)new_stack_start;
			i >= ((uintptr_t)new_stack_start - size);
			i -= 0x1000) {
		alloc_frame(get_page(i, 1, current_directory), 0 /* user */, 1 /* writable */);
	}
	uintptr_t pd_addr;
	__asm__ __volatile__ ("mov %%cr3, %0" : "=r" (pd_addr));
	__asm__ __volatile__ ("mov %0, %%cr3" : : "r" (pd_addr));
	uintptr_t old_stack_pointer;
	__asm__ __volatile__ ("mov %%esp, %0" : "=r" (old_stack_pointer));
	uintptr_t old_base_pointer;
	__asm__ __volatile__ ("mov %%ebp, %0" : "=r" (old_base_pointer));
	uintptr_t offset = (uintptr_t)new_stack_start - initial_esp;
	uintptr_t new_stack_pointer = old_stack_pointer + offset;
	uintptr_t new_base_pointer  = old_base_pointer  + offset;
	memcpy((void *)new_stack_pointer, (void *)old_stack_pointer, initial_esp - old_stack_pointer);
	for (i = (uintptr_t)new_stack_start; i > (uintptr_t)new_stack_start - size; i -= 4) {
		uintptr_t temp = *(uintptr_t*)i;
		if ((old_stack_pointer < temp) && (temp < initial_esp)) {
			temp = temp + offset;
			uintptr_t *temp2 = (uintptr_t *)i;
			*temp2 = temp;
		}
	}
	__asm__ __volatile__ ("mov %0, %%esp" : : "r" (new_stack_pointer));
	__asm__ __volatile__ ("mov %0, %%ebp" : : "r" (new_base_pointer));
}

void
tasking_install() {
	__asm__ __volatile__ ("cli");
	move_stack((void *)0xE000000, 0x2000);

	current_task = (task_t *)kmalloc(sizeof(task_t));
	ready_queue = current_task;
	current_task->id  = next_pid++;
	current_task->esp = 0;
	current_task->ebp = 0;
	current_task->eip = 0;
	current_task->page_directory = current_directory;
	current_task->next = 0;

	__asm__ __volatile__ ("sti");
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
	task_t * tmp_task = (task_t *)ready_queue;
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
		new_task->esp = esp;
		new_task->ebp = ebp;
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
	uintptr_t esp, ebp, eip;
	__asm__ __volatile__ ("mov %%esp, %0" : "=r" (esp));
	__asm__ __volatile__ ("mov %%ebp, %0" : "=r" (ebp));
	eip = read_eip();
	if (eip == 0x10000) {
		return;
	}
	current_task->eip = eip;
	current_task->esp = esp;
	current_task->ebp = ebp;
	current_task = current_task->next;
	if (!current_task) {
		current_task = ready_queue;
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
