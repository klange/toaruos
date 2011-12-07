/*
 * vim:tabstop=4
 * vim:noexpandtab
 */
#include <system.h>
#include <syscall.h>

#define SPECIAL_CASE_STDIO

/*
 * System calls themselves
 */

void validate(void * ptr) {
	if (ptr && (uintptr_t)ptr < current_task->entry) {
		kprintf("SEGFAULT: Invalid pointer passed to syscall. (0x%x < 0x%x)\n", (uintptr_t)ptr, current_task->entry);
		HALT_AND_CATCH_FIRE("Segmentation fault", NULL);
	}
}

/*
 * print something to the core terminal
 */
static int print(char * s) {
	validate((void *)s);
	ansi_print(s);
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
#ifdef SPECIAL_CASE_STDIO
	if (fd == 0) {
		IRQ_ON;
		kgets(ptr, len);
		IRQ_OFF;
		if (strlen(ptr) < (uint32_t)len) {
			int j = strlen(ptr);
			ptr[j] = '\n';
			ptr[j+1] = '\0';
		}
		return strlen(ptr);
	}
#endif
	if (fd >= current_task->next_fd || fd < 0) {
		return -1;
	}
	validate(ptr);
	fs_node_t * node = current_task->descriptors[fd];
	uint32_t out = read_fs(node, node->offset, len, (uint8_t *)ptr);
	node->offset += out;
	return out;
}

static int write(int fd, char * ptr, int len) {
#ifdef SPECIAL_CASE_STDIO
	if (fd == 1 || fd == 2) {
		for (int i = 0; i < len; ++i) {
			ansi_put(ptr[i]);
			serial_send(ptr[i]);
		}
		return len;
	}
#endif
	if (fd >= current_task->next_fd || fd < 0) {
		return -1;
	}
	validate(ptr);
	fs_node_t * node = current_task->descriptors[fd];
	uint32_t out = write_fs(node, node->offset, len, (uint8_t *)ptr);
	node->offset += out;
	return out;
}

static int wait(unsigned int child) {
	task_t * volatile child_task = gettask(child);
	/* If the child task doesn't exist, bail */
	if (!child_task) return -1;
	/* Wait until it finishes (this is stupidly memory intensive,
	 * but we haven't actually implemented wait() yet, so there's
	 * not all that much we can do right now. */
	while (child_task->finished == 0) {
		if (child_task->finished != 0) break;
		switch_task();
	}
	/* Grab the child's return value */
	return child_task->retval;
}

static int open(const char * file, int flags, int mode) {
	validate((void *)file);
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

static int sys_sbrk(int size) {
	uintptr_t ret = current_task->heap;
	current_task->heap += size;
	while (current_task->heap > current_task->heap_a) {
		current_task->heap_a += 0x1000;
		alloc_frame(get_page(current_task->heap_a, 1, current_directory), 0, 1);
	}
	return ret;
}

static int execve(const char * filename, char *const argv[], char *const envp[]) {
	validate((void *)argv);
	validate((void *)filename);
	validate((void *)envp);
	int i = 0;
	while (argv[i]) {
		++i;
	}
	char ** argv_ = malloc(sizeof(char *) * i);
	for (int j = 0; j < i; ++j) {
		argv_[j] = malloc((strlen(argv[j]) + 1) * sizeof(char));
		memcpy(argv_[j], argv[j], strlen(argv[j]) + 1);
	}
	/* Discard envp */
	exec((char *)filename, i, (char **)argv_);
	return -1;
}

static int sys_fork() {
	uint32_t f = fork();
	return f;
}

static int getgraphicsaddress() {
	return (int)bochs_get_address();
}

static volatile char kbd_last = 0;

static void kbd_direct_handler(char ch) {
	kbd_last = ch;
}

static int kbd_mode(int mode) {
	if (mode == 0) {
		if (keyboard_direct_handler) {
			keyboard_direct_handler = NULL;
		}
	} else {
		keyboard_direct_handler = kbd_direct_handler;
	}
	return 0;
}

static int kbd_get() {
	/* If we're requesting keyboard input, we better damn well be getting it */
	IRQ_ON;
	char x = kbd_last;
	kbd_last = 0;
	return (int)x;
}

static int seek(int fd, int offset, int whence) {
	if (fd >= current_task->next_fd || fd < 0) {
		return -1;
	}
	if (fd < 3) {
		return 0;
	}
	if (whence == 0) {
		current_task->descriptors[fd]->offset = offset;
	} else if (whence == 1) {
		current_task->descriptors[fd]->offset += offset;
	} else if (whence == 2) {
		current_task->descriptors[fd]->offset = current_task->descriptors[fd]->length + offset;
	}
	return current_task->descriptors[fd]->offset;
}

static int stat(int fd, uint32_t * st) {
	return 0;
}

static int setgraphicsoffset(int rows) {
	bochs_set_y_offset(rows);
	return 0;
}

/*
 * System Call Internals
 */
static void syscall_handler(struct regs * r);
static uintptr_t syscalls[] = {
	/* System Call Table */
	(uintptr_t)&exit,				/* 0 */
	(uintptr_t)&print,
	(uintptr_t)&open,
	(uintptr_t)&read,
	(uintptr_t)&write,				/* 4 */
	(uintptr_t)&close,
	(uintptr_t)&gettimeofday,
	(uintptr_t)&execve,
	(uintptr_t)&sys_fork,			/* 8 */
	(uintptr_t)&getpid,
	(uintptr_t)&sys_sbrk,
	(uintptr_t)&getgraphicsaddress,
	(uintptr_t)&kbd_mode,			/* 12 */
	(uintptr_t)&kbd_get,
	(uintptr_t)&seek,
	(uintptr_t)&stat,
	(uintptr_t)&setgraphicsoffset,	/* 16 */
	(uintptr_t)&wait,
};
uint32_t num_syscalls = 18;

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
	asm volatile (
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
