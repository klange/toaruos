/*
 * vim:tabstop=4
 * vim:noexpandtab
 */
#include <system.h>
#include <syscall.h>


/*
 * System calls themselves
 */

/*
 * print something to the core terminal
 */
static int print(char * s) {
	kprintf(s);
	return 0;
}

/*
 * Exit the current task.
 * DOES NOT RETURN!
 */
static int exit(int retval) {
	/* Deschedule the current task */
	task_exit(retval);
	while (1) { };
	return retval;
}

static int read(int fd, char * ptr, int len) {
	if (fd >= current_task->next_fd || fd < 0) {
		return -1;
	}
	fs_node_t * node = current_task->descriptors[fd];
	uint32_t out = read_fs(node, node->offset, len, (uint8_t *)ptr);
	node->offset += out;
	return out;
}

static int write(int fd, char * ptr, int len) {
	if (fd >= current_task->next_fd || fd < 0) {
		return -1;
	}
	fs_node_t * node = current_task->descriptors[fd];
	uint32_t out = write_fs(node, node->offset, len, (uint8_t *)ptr);
	node->offset += out;
	return out;
}

static int open(const char * file, int flags, int mode) {
	fs_node_t * node = kopen(file, 0);
	if (!node) {
		return -1;
	}
	current_task->descriptors[current_task->next_fd] = node;
	node->offset = 0;
	return current_task->next_fd++;
}

static int close(int fd) {
	if (fd <= current_task->next_fd || fd < 0) { 
		return -1;
	}
	close_fs(current_task->descriptors[fd]);
	return 0;
}

/*
 * System Call Internals
 */
static void syscall_handler(struct regs * r);
static uintptr_t syscalls[] = {
	/* System Call Table */
	(uintptr_t)&exit,
	(uintptr_t)&print,
	(uintptr_t)&open,
	(uintptr_t)&read,
	(uintptr_t)&write,
	(uintptr_t)&close
};
uint32_t num_syscalls = 6;

void
syscalls_install() {
	isrs_install_handler(0x7F, &syscall_handler);
}

void
syscall_handler(
		struct regs * r
		) {
	if (r->eax >= num_syscalls) {
		return;
	}
	uintptr_t location = syscalls[r->eax];

	uint32_t ret;
	__asm__ __volatile__ (
			"push %1\n"
			"push %2\n"
			"push %3\n"
			"push %4\n"
			"push %5\n"
			"call *%6\n"
			"pop %%ebx\n"
			"pop %%ebx\n"
			"pop %%ebx\n"
			"pop %%ebx\n"
			"pop %%ebx\n"
			: "=a" (ret) : "r" (r->edi), "r" (r->esi), "r" (r->edx), "r" (r->ecx), "r" (r->ebx), "r" (location));
	r->eax = ret;
}
