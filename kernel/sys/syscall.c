/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Syscall Tables
 *
 */
#include <system.h>
#include <syscall.h>
#include <process.h>
#include <logging.h>
#include <fs.h>
#include <pipe.h>
#include <version.h>

#define SPECIAL_CASE_STDIO

/*
 * System calls themselves
 */

void validate(void * ptr) {
	if (ptr && (uintptr_t)ptr < current_process->image.entry) {
		kprintf("SEGFAULT: Invalid pointer passed to syscall. (0x%x < 0x%x)\n", (uintptr_t)ptr, current_process->image.entry);
		HALT_AND_CATCH_FIRE("Segmentation fault", NULL);
	}
}

/*
 * print something to the debug terminal (serial)
 */
static int print(char * s) {
	validate((void *)s);
	serial_string(s);
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
	if (fd >= (int)current_process->fds.length || fd < 0) {
		return -1;
	}
	if (current_process->fds.entries[fd] == NULL) {
		return -1;
	}
	validate(ptr);
	fs_node_t * node = current_process->fds.entries[fd];
	uint32_t out = read_fs(node, node->offset, len, (uint8_t *)ptr);
	node->offset += out;
	return out;
}

static int readdir(int fd, int index, struct dirent * entry) {
	if (fd >= (int)current_process->fds.length || fd < 0) {
		return -1;
	}
	if (current_process->fds.entries[fd] == NULL) {
		return -1;
	}
	validate(entry);
	fs_node_t * node = current_process->fds.entries[fd];

	struct dirent * kentry = readdir_fs(node, (uint32_t)index);
	if (!kentry) {
		free(kentry);
		return 1;
	}

	memcpy(entry, kentry, sizeof(struct dirent));
	free(kentry);
	return 0;
}

static int write(int fd, char * ptr, int len) {
	if ((fd == 1 && !current_process->fds.entries[fd]) ||
		(fd == 2 && !current_process->fds.entries[fd])) {
		serial_string(ptr);
		return len;
	}
	if (fd >= (int)current_process->fds.length || fd < 0) {
		return -1;
	}
	if (current_process->fds.entries[fd] == NULL) {
		return -1;
	}
	validate(ptr);
	fs_node_t * node = current_process->fds.entries[fd];
	uint32_t out = write_fs(node, node->offset, len, (uint8_t *)ptr);
	node->offset += out;
	return out;
}

static int wait(int child) {
	if (child < 1) {
		kprintf("lol nope\n");
		return 0;
	}
	process_t * volatile child_task = process_from_pid(child);
	/* If the child task doesn't exist, bail */
	if (!child_task) {
		kprintf("Tried to wait for non-existent process\n");
	}
	/* Wait until it finishes (this is stupidly memory intensive,
	 * but we haven't actually implemented wait() yet, so there's
	 * not all that much we can do right now. */
	while (child_task->finished == 0) {
		if (child_task->finished != 0) break;
		switch_task();
	}
	/* Grab the child's return value */
	int ret = child_task->status;
	delete_process(child_task);
	return ret;
}

static int open(const char * file, int flags, int mode) {
	validate((void *)file);
	fs_node_t * node = kopen((char *)file, 0);
	if (!node) {
		return -1;
	}
	node->offset = 0;
	return process_append_fd((process_t *)current_process, node);
}

static int close(int fd) {
	if (fd >= (int)current_process->fds.length || fd < 0) { 
		return -1;
	}
	close_fs(current_process->fds.entries[fd]);
	return 0;
}

static int sys_sbrk(int size) {
	uintptr_t ret = current_process->image.heap;
	uintptr_t i_ret = ret;
	while (ret % 0x1000) {
		ret++;
	}
	current_process->image.heap += (ret - i_ret) + size;
	while (current_process->image.heap > current_process->image.heap_actual) {
		current_process->image.heap_actual += 0x1000;
		assert(current_process->image.heap_actual % 0x1000 == 0);
		alloc_frame(get_page(current_process->image.heap_actual, 1, current_directory), 0, 1);
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
	IRQ_RES;
	char x = kbd_last;
	kbd_last = 0;
	return (int)x;
}

static int seek(int fd, int offset, int whence) {
	if (fd >= (int)current_process->fds.length || fd < 0) {
		return -1;
	}
	if (fd < 3) {
		return 0;
	}
	if (whence == 0) {
		current_process->fds.entries[fd]->offset = offset;
	} else if (whence == 1) {
		current_process->fds.entries[fd]->offset += offset;
	} else if (whence == 2) {
		current_process->fds.entries[fd]->offset = current_process->fds.entries[fd]->length + offset;
	}
	return current_process->fds.entries[fd]->offset;
}

static int stat(int fd, uint32_t st) {
	if (fd >= (int)current_process->fds.length || fd < 0) {
		return -1;
	}
	fs_node_t * fn = current_process->fds.entries[fd];
	struct stat * f = (struct stat *)st;
	f->st_dev   = 0;
	f->st_ino   = fn->inode;

	uint32_t flags = 0;
	if (fn->flags & FS_FILE)        { flags |= _IFREG; }
	if (fn->flags & FS_DIRECTORY)   { flags |= _IFDIR; }
	if (fn->flags & FS_CHARDEVICE)  { flags |= _IFCHR; }
	if (fn->flags & FS_BLOCKDEVICE) { flags |= _IFBLK; }
	if (fn->flags & FS_PIPE)        { flags |= _IFIFO; }
	if (fn->flags & FS_SYMLINK)     { flags |= _IFLNK; }

	f->st_mode  = fn->mask | flags;
	f->st_nlink = 0;
	f->st_uid   = fn->uid;
	f->st_gid   = fn->gid;
	f->st_rdev  = 0;
	f->st_size  = fn->length;

	return 0;
}

static int setgraphicsoffset(int rows) {
	bochs_set_y_offset(rows);
	return 0;
}

static int getgraphicswidth() {
	return bochs_resolution_x;
}

static int getgraphicsheight() {
	return bochs_resolution_y;
}

static int getgraphicsdepth() {
	return bochs_resolution_b;
}

static int mkpipe() {
	fs_node_t * node = make_pipe(4096 * 2);
	return process_append_fd((process_t *)current_process, node);
}

static int dup2(int old, int new) {
	process_move_fd((process_t *)current_process, old, new);
	return new;
}

static int getuid() {
	return current_process->user;
}

static int setuid(user_t new_uid) {
	if (current_process->user == USER_ROOT_UID) {
		current_process->user = new_uid;
		return 0;
	}
	return -1;
}

static int kernel_name_XXX(char * buffer) {
	char version_number[1024];
	sprintf(version_number, __kernel_version_format,
			__kernel_version_major,
			__kernel_version_minor,
			__kernel_version_lower,
			__kernel_version_suffix);
	return sprintf(buffer, "%s %s %s %s %s %s",
			__kernel_name,
			version_number,
			__kernel_version_codename,
			__kernel_build_date,
			__kernel_build_time,
			__kernel_arch);
}

static int reboot() {
	kprintf("[kernel] Reboot requested from process %d by user #%d\n", current_process->id, current_process->user);
	kprintf("[kernel] Good bye!\n");
	if (current_process->user != 0) {
		return -1;
	} else {
		/* Goodbye, cruel world */
		uint8_t out = 0x02;
		while ((out & 0x02) != 0) {
			out = inportb(0x64);
		}
		outportb(0x64, 0xFE); /* Reset */
		STOP;
	}
	return 0;
}

static int chdir(char * newdir) {
	char * path = canonicalize_path(current_process->wd_name, newdir);
	fs_node_t * chd = kopen(path, 0);
	if (chd) {
		if ((chd->flags & FS_DIRECTORY) == 0) {
			return -1;
		}
		free(current_process->wd_name);
		current_process->wd_name = malloc(strlen(path) + 1);
		memcpy(current_process->wd_name, path, strlen(path) + 1);
		return 0;
	} else {
		return -1;
	}
}

static char * getcwd(char * buf, size_t size) {
	if (!buf) return NULL;
	validate((void *)buf);
	memcpy(buf, current_process->wd_name, min(size, strlen(current_process->wd_name) + 1));
	return buf;
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
	(uintptr_t)&fork,				/* 8 */
	(uintptr_t)&getpid,
	(uintptr_t)&sys_sbrk,
	(uintptr_t)&getgraphicsaddress,
	(uintptr_t)&kbd_mode,			/* 12 */
	(uintptr_t)&kbd_get,
	(uintptr_t)&seek,
	(uintptr_t)&stat,
	(uintptr_t)&setgraphicsoffset,	/* 16 */
	(uintptr_t)&wait,
	(uintptr_t)&getgraphicswidth,
	(uintptr_t)&getgraphicsheight,
	(uintptr_t)&getgraphicsdepth,	/* 20 */
	(uintptr_t)&mkpipe,
	(uintptr_t)&dup2,
	(uintptr_t)&getuid,
	(uintptr_t)&setuid,				/* 24 */
	(uintptr_t)&kernel_name_XXX,
	(uintptr_t)&reboot,
	(uintptr_t)&readdir,
	(uintptr_t)&chdir,				/* 28 */
	(uintptr_t)&getcwd,
	(uintptr_t)&clone,
	0
};
uint32_t num_syscalls;

void
syscalls_install() {
	blog("Initializing syscall table...");
	for (num_syscalls = 0; syscalls[num_syscalls] != 0; ++num_syscalls);
	LOG(INFO, "Initializing syscall table with %d functions", num_syscalls);
	isrs_install_handler(0x7F, &syscall_handler);
	bfinish(0);
}

void
syscall_handler(
		struct regs * r
		) {
	if (r->eax >= num_syscalls) {
		return;
	}
	uintptr_t location = syscalls[r->eax];

	/* Update the syscall registers for this process */
	current_process->syscall_registers = r;

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

	/* The syscall handler may have moved the register pointer
	 * (ie, by creating a new stack)
	 * Update the pointer */
	r = current_process->syscall_registers;
	r->eax = ret;
}
