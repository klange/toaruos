/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Syscall Tables
 *
 */
#include <system.h>
#include <process.h>
#include <logging.h>
#include <fs.h>
#include <pipe.h>
#include <version.h>
#include <shm.h>

#define SPECIAL_CASE_STDIO

#define RESERVED 1

/*
 * System calls themselves
 */

void validate(void * ptr) {
	if (ptr && (uintptr_t)ptr < current_process->image.entry) {
		debug_print(ERROR, "SEGFAULT: Invalid pointer passed to syscall. (0x%x < 0x%x)", (uintptr_t)ptr, current_process->image.entry);
		HALT_AND_CATCH_FIRE("Segmentation fault", NULL);
	}
}

/*
 * print something to the debug terminal (serial)
 */
static int print(char * s) {
	validate((void *)s);
	kprintf("%s", s);
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
	if (fd >= (int)current_process->fds->length || fd < 0) {
		return -1;
	}
	if (current_process->fds->entries[fd] == NULL) {
		return -1;
	}
	validate(ptr);
	fs_node_t * node = current_process->fds->entries[fd];
	uint32_t out = read_fs(node, node->offset, len, (uint8_t *)ptr);
	node->offset += out;
	return out;
}

static int readdir(int fd, int index, struct dirent * entry) {
	if (fd >= (int)current_process->fds->length || fd < 0) {
		return -1;
	}
	if (current_process->fds->entries[fd] == NULL) {
		return -1;
	}
	validate(entry);
	fs_node_t * node = current_process->fds->entries[fd];

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
	if ((fd == 1 && !current_process->fds->entries[fd]) ||
		(fd == 2 && !current_process->fds->entries[fd])) {
		for (uint32_t i = 0; i < (uint32_t)len; ++i) {
			kprintf("%c", ptr[i]);
		}
		return len;
	}
	if (fd >= (int)current_process->fds->length || fd < 0) {
		return -1;
	}
	if (current_process->fds->entries[fd] == NULL) {
		return -1;
	}
	validate(ptr);
	fs_node_t * node = current_process->fds->entries[fd];
	uint32_t out = write_fs(node, node->offset, len, (uint8_t *)ptr);
	node->offset += out;
	return out;
}

static int wait(int child) {
	if (child < 1) {
		debug_print(WARNING, "Process %d requested group wait, which we can not do!", getpid());
		return 0;
	}
	process_t * volatile child_task = process_from_pid(child);
	/* If the child task doesn't exist, bail */
	if (!child_task) {
		debug_print(WARNING, "Tried to wait for non-existent process");
		return -1;
	}
	while (child_task->finished == 0) {
		/* Add us to the wait queue for this child */
		sleep_on(child_task->wait_queue);
	}
	/* Grab the child's return value */
	int ret = child_task->status;
	delete_process(child_task);
	return ret;
}

static int open(const char * file, int flags, int mode) {
	validate((void *)file);
	fs_node_t * node = kopen((char *)file, 0);
	if (!node && (flags & 0x600)) {
		/* Um, make one */
		if (!create_file_fs((char *)file, 0777)) {
			debug_print(NOTICE, "[creat] Creating file!");
			node = kopen((char *)file, 0);
		}
	}
	if (!node) {
		return -1;
	}
	node->offset = 0;
	int fd = process_append_fd((process_t *)current_process, node);
	debug_print(INFO, "[open] pid=%d %s -> %d", getpid(), file, fd);
	return fd;
}

static int close(int fd) {
	if (fd >= (int)current_process->fds->length || fd < 0) { 
		return -1;
	}
	close_fs(current_process->fds->entries[fd]);
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

static int sys_getpid() {
	/* The user actually wants the pid of the originating thread (which can be us). */
	if (current_process->group) {
		return current_process->group;
	} else {
		/* We are the leader */
		return current_process->id;
	}
}

/* Actual getpid() */
static int gettid() {
	return getpid();
}

static int execve(const char * filename, char *const argv[], char *const envp[]) {
	validate((void *)argv);
	validate((void *)filename);
	validate((void *)envp);
	int argc = 0, envc = 0;
	while (argv[argc]) { ++argc; }
	if (envp) {
		while (envp[envc]) { ++envc; }
	}
	debug_print(INFO, "Allocating space for arguments...");
	char ** argv_ = malloc(sizeof(char *) * argc);
	for (int j = 0; j < argc; ++j) {
		argv_[j] = malloc((strlen(argv[j]) + 1) * sizeof(char));
		memcpy(argv_[j], argv[j], strlen(argv[j]) + 1);
	}
	char ** envp_;
	if (envp && envc) {
		envp_ = malloc(sizeof(char *) * (envc + 1));
		for (int j = 0; j < envc; ++j) {
			envp_[j] = malloc((strlen(envp[j]) + 1) * sizeof(char));
			memcpy(envp_[j], envp[j], strlen(envp[j]) + 1);
		}
		envp_[envc] = 0;
	} else {
		envp_ = malloc(sizeof(char *));
		envp_[0] = NULL;
	}
	debug_print(INFO,"Releasing all shmem regions...");
	shm_release_all((process_t *)current_process);

	debug_print(INFO,"Executing...");
	/* Discard envp */
	exec((char *)filename, argc, (char **)argv_, (char **)envp_);
	return -1;
}

static int getgraphicsaddress() {
	return (int)lfb_get_address();
}

static int seek(int fd, int offset, int whence) {
	if (fd >= (int)current_process->fds->length || fd < 0) {
		return -1;
	}
	if (fd < 3) {
		return 0;
	}
	if (whence == 0) {
		current_process->fds->entries[fd]->offset = offset;
	} else if (whence == 1) {
		current_process->fds->entries[fd]->offset += offset;
	} else if (whence == 2) {
		current_process->fds->entries[fd]->offset = current_process->fds->entries[fd]->length + offset;
	}
	return current_process->fds->entries[fd]->offset;
}

static int stat(int fd, uint32_t st) {
	if (fd >= (int)current_process->fds->length || fd < 0) {
		return -1;
	}
	fs_node_t * fn = current_process->fds->entries[fd];
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

	if (fn->flags & FS_PIPE) {
		/* Pipes have dynamic sizes */
		f->st_size = pipe_size(fn);
	}

	return 0;
}

static int setgraphicsoffset(int rows) {
	bochs_set_y_offset(rows);
	return 0;
}

static int getgraphicswidth() {
	return lfb_resolution_x;
}

static int getgraphicsheight() {
	return lfb_resolution_y;
}

static int getgraphicsdepth() {
	return lfb_resolution_b;
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

static int send_signal(pid_t process, uint32_t signal) {
	process_t * receiver = process_from_pid(process);

	if (!receiver) {
		/* Invalid pid */
		return 1;
	}

	if (receiver->user != current_process->user && current_process->user != USER_ROOT_UID) {
		/* No way in hell. */
		return 1;
	}

	if (signal > NUMSIGNALS) {
		/* Invalid signal */
		return 1;
	}

	if (receiver->finished) {
		/* Can't send signals to finished processes */
		return 1;
	}

	/* Append signal to list */
	signal_t * sig = malloc(sizeof(signal_t));
	sig->handler = (uintptr_t)receiver->signals.functions[signal];
	sig->signum  = signal;
	memset(&sig->registers_before, 0x00, sizeof(regs_t));

	if (!process_is_ready(receiver)) {
		make_process_ready(receiver);
	}

	list_insert(receiver->signal_queue, sig);

	return 0;
}

static uintptr_t sys_signal(uint32_t signum, uintptr_t handler) {
	if (signum > NUMSIGNALS) {
		return -1;
	}
	uintptr_t old = current_process->signals.functions[signum];
	current_process->signals.functions[signum] = handler;
	return old;
}

/*
static void inspect_memory (uintptr_t vaddr) {
	// Please use this scary hack of a function as infrequently as possible.
	shmem_debug_frame(vaddr);
}
*/

extern void ext2_disk_sync();

static int reboot() {
	debug_print(NOTICE, "[kernel] Reboot requested from process %d by user #%d", current_process->id, current_process->user);
	if (current_process->user != USER_ROOT_UID) {
		return -1;
	} else {
		debug_print(NOTICE, "[kernel] Good bye!");
		ext2_disk_sync();
		/* Goodbye, cruel world */
		IRQ_OFF;
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

static char   hostname[256];
static size_t hostname_len = 0;

static int sethostname(char * new_hostname) {
	if (current_process->user == USER_ROOT_UID) {
		size_t len = strlen(new_hostname) + 1;
		if (len > 256) {
			return 1;
		}
		hostname_len = len;
		memcpy(hostname, new_hostname, hostname_len);
		return 0;
	} else {
		return 1;
	}
}

static int gethostname(char * buffer) {
	memcpy(buffer, hostname, hostname_len);
	return hostname_len;
}

static int mousedevice() {
	extern fs_node_t * mouse_pipe;
	return process_append_fd((process_t *)current_process, mouse_pipe);
}

static int open_serial(int device) {
	return process_append_fd((process_t *)current_process, serial_device_create(device));
}

extern int mkdir_fs(char *name, uint16_t permission);

static int sys_mkdir(char * path, uint32_t mode) {
	return mkdir_fs(path, 0777);
}

/**
 * share_fd: Make a file descriptor available to another process.
 */
static uintptr_t share_fd(int fd, int pid) {
	if (fd >= (int)current_process->fds->length || fd < 0) {
		return 0;
	}
	fs_node_t * fn = current_process->fds->entries[fd];
	fn->shared_with = pid;
	return (uintptr_t)fn;
}

/**
 * get_fd: Retreive a file descriptor (by key == pointer to fs_node_t) from
 *         another proces.
 */
static int get_fd(uintptr_t fn) {
	fs_node_t * node = (fs_node_t *)fn;
	if (node->shared_with == current_process->id || node->shared_with == current_process->group) {
		return process_append_fd((process_t *)current_process, node);
	} else {
		return -1;
	}
}

/*
 * Yield the rest of the quantum;
 * useful for busy waiting and other such things
 */
static int yield() {
	switch_task(1);
	return 1;
}

/*
 * System Function
 */
static int system_function(int fn, char ** args) {
	/* System Functions are special debugging system calls */
	if (current_process->user == USER_ROOT_UID) {
		switch (fn) {
			case 1:
				/* Print memory information */
				/* Future: /proc/meminfo */
				kprintf("Memory used:      %d\n", memory_use());
				kprintf("Memory available: %d\n", memory_total());
				return 0;
			case 2:
				/* Print process tree */
				/* Future: /proc in general */
				debug_print_process_tree();
				return 0;
			case 3:
				ext2_disk_sync();
				return 0;
			case 4:
				/* Request kernel output to file descriptor in arg0*/
				kprintf("Setting output to file object in process %d's fd=%d!\n", getpid(), (int)args);
				kprint_to_file = current_process->fds->entries[(int)args];
				break;
			default:
				kprintf("Bad system function %d\n", fn);
				break;
		}
	}
	return -1; /* Bad system function or access failure */
}

/*
 * System Call Internals
 */
static void syscall_handler(struct regs * r);
static uintptr_t syscalls[] = {
	/* System Call Table */
	(uintptr_t)&exit,               /* 0 */
	(uintptr_t)&print,
	(uintptr_t)&open,
	(uintptr_t)&read,
	(uintptr_t)&write,              /* 4 */
	(uintptr_t)&close,
	(uintptr_t)&gettimeofday,
	(uintptr_t)&execve,
	(uintptr_t)&fork,               /* 8 */
	(uintptr_t)&sys_getpid,
	(uintptr_t)&sys_sbrk,
	(uintptr_t)&getgraphicsaddress,
	(uintptr_t)RESERVED,            /* 12 */
	(uintptr_t)RESERVED,
	(uintptr_t)&seek,
	(uintptr_t)&stat,
	(uintptr_t)&setgraphicsoffset,  /* 16 */
	(uintptr_t)&wait,
	(uintptr_t)&getgraphicswidth,
	(uintptr_t)&getgraphicsheight,
	(uintptr_t)&getgraphicsdepth,   /* 20 */
	(uintptr_t)&mkpipe,
	(uintptr_t)&dup2,
	(uintptr_t)&getuid,
	(uintptr_t)&setuid,             /* 24 */
	(uintptr_t)&kernel_name_XXX,
	(uintptr_t)&reboot,
	(uintptr_t)&readdir,
	(uintptr_t)&chdir,              /* 28 */
	(uintptr_t)&getcwd,
	(uintptr_t)&clone,
	(uintptr_t)&sethostname,
	(uintptr_t)&gethostname,        /* 32 */
	(uintptr_t)&mousedevice,
	(uintptr_t)&sys_mkdir,
	(uintptr_t)&shm_obtain,
	(uintptr_t)&shm_release,        /* 36 */
	(uintptr_t)&send_signal,
	(uintptr_t)&sys_signal,
	(uintptr_t)&share_fd,
	(uintptr_t)&get_fd,             /* 40 */
	(uintptr_t)&gettid,
	(uintptr_t)&yield,
	(uintptr_t)&system_function,
	(uintptr_t)&open_serial,        /* 44 */
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

	if (location == 1) {
		return;
	}

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
