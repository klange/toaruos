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
#include <utsname.h>

static char   hostname[256];
static size_t hostname_len = 0;

/*
 * System calls themselves
 */

void validate(void * ptr) {
	if (validate_safe(ptr)) {
		debug_print(ERROR, "SEGFAULT: Invalid pointer passed to syscall. (0x%x < 0x%x)", (uintptr_t)ptr, current_process->image.entry);
		HALT_AND_CATCH_FIRE("Segmentation fault", NULL);
	}
}

int validate_safe(void * ptr) {
	if (ptr && (uintptr_t)ptr < current_process->image.entry) {
		return 1;
	}
	return 0;
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

static int ioctl(int fd, int request, void * argp) {
	if (fd >= (int)current_process->fds->length || fd < 0) {
		return -1;
	}
	if (current_process->fds->entries[fd] == NULL) {
		return -1;
	}
	validate(argp);
	fs_node_t * node = current_process->fds->entries[fd];
	return ioctl_fs(node, request, argp);
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

static int advanced_wait(int child, int * status, int options) {
	process_t * volatile child_task;
	if (child == 0) {
		debug_print_process_tree();
		child_task = process_get_first_child((process_t *)current_process);
	} else if (child == -1) {
		debug_print(WARNING, "wait(-1) from %d", getpid());
		child_task = process_get_first_child((process_t *)current_process);
	} else if (child < 1) {
		debug_print(WARNING, "Process %d requested group wait, which we can not do! Pretending they asked for a single process (%d)", getpid(), -child);
		child_task = process_from_pid(-child);
	} else {
		child_task = process_from_pid(child);
	}
	/* If the child task doesn't exist, bail */
	if (!child_task) {
		if (status && !validate_safe(status)) {
			*status = 0;
		}
		debug_print(WARNING, "Tried to wait for non-existent process by pid %d", getpid());
		return -ECHILD;
	}
	debug_print(NOTICE, "pid=%d waiting on pid=%d", current_process->id, child_task->id);
	while (child_task->finished == 0) {
		/* Add us to the wait queue for this child */
		sleep_on(child_task->wait_queue);
	}
	/* Grab the child's return value */
	int ret = child_task->status;
	if (status && !validate_safe(status)) {
		*status = ret;
	}
	return child_task->id;
}

static int wait(int child) {
	process_t * volatile child_task;
	if (child == 0) {
		debug_print_process_tree();
		child_task = process_get_first_child((process_t *)current_process);
	} else if (child == -1) {
		debug_print(WARNING, "wait(-1) from %d", getpid());
		child_task = process_get_first_child((process_t *)current_process);
		if (child_task) {
			child = child_task->id;
		} else {
			return -1;
		}
	} else if (child < 1) {
		debug_print(WARNING, "Process %d requested group wait, which we can not do! Pretending they asked for a single process (%d)", getpid(), -child);
		child_task = process_from_pid(-child);
	} else {
		child_task = process_from_pid(child);
	}
	/* If the child task doesn't exist, bail */
	if (!child_task) {
		debug_print(WARNING, "Tried to wait for non-existent process %d by pid %d", child, getpid());
		return 0;
	}
	debug_print(NOTICE, "pid=%d waiting on pid=%d", current_process->id, child_task->id);
	while (child_task->finished == 0) {
		/* Add us to the wait queue for this child */
		sleep_on(child_task->wait_queue);
	}
	/* Grab the child's return value */
	int ret = child_task->status;
	return ret;
}

static int open(const char * file, int flags, int mode) {
	validate((void *)file);
	debug_print(NOTICE, "open(%s) flags=0x%x; mode=0x%x", file, flags, mode);
	fs_node_t * node = kopen((char *)file, flags);
	if (!node && (flags & O_CREAT)) {
		debug_print(NOTICE, "- file does not exist and create was requested.");
		/* Um, make one */
		if (!create_file_fs((char *)file, mode)) {
			node = kopen((char *)file, flags);
		}
	}
	if (!node) {
		debug_print(NOTICE, "File does not exist; someone should be setting errno?");
		return -1;
	}
	node->offset = 0;
	int fd = process_append_fd((process_t *)current_process, node);
	debug_print(INFO, "[open] pid=%d %s -> %d", getpid(), file, fd);
	return fd;
}

static int access(const char * file, int flags) {
	validate((void *)file);
	debug_print(INFO, "access(%s, 0x%x) from pid=%d", file, flags, getpid());
	fs_node_t * node = kopen((char *)file, 0);
	if (!node) return -1;
	close_fs(node);
	return 0;
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

static int sys_getpid(void) {
	/* The user actually wants the pid of the originating thread (which can be us). */
	if (current_process->group) {
		return current_process->group;
	} else {
		/* We are the leader */
		return current_process->id;
	}
}

/* Actual getpid() */
static int gettid(void) {
	return getpid();
}

static int execve(const char * filename, char *const argv[], char *const envp[]) {
	validate((void *)argv);
	validate((void *)filename);
	validate((void *)envp);
	debug_print(NOTICE, "%d = exec(%s, ...)", current_process->id, filename);
	int argc = 0, envc = 0;
	while (argv[argc]) { ++argc; }
	if (envp) {
		while (envp[envc]) { ++envc; }
	}
	debug_print(INFO, "Allocating space for arguments...");
	char ** argv_ = malloc(sizeof(char *) * (argc + 1));
	for (int j = 0; j < argc; ++j) {
		argv_[j] = malloc((strlen(argv[j]) + 1) * sizeof(char));
		memcpy(argv_[j], argv[j], strlen(argv[j]) + 1);
	}
	argv_[argc] = 0;
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

static int getgraphicsaddress(void) {
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

static int stat_node(fs_node_t * fn, uintptr_t st) {
	struct stat * f = (struct stat *)st;
	if (!fn) {
		memset(f, 0x00, sizeof(struct stat));
		debug_print(INFO, "stat: This file doesn't exist");
		return -1;
	}
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

	f->st_atime = fn->atime;
	f->st_mtime = fn->mtime;
	f->st_ctime = fn->ctime;

	if (fn->get_size) {
		f->st_size = fn->get_size(fn);
	}

	return 0;
}

static int stat_file(char * file, uintptr_t st) {
	int result;
	validate((void *)file);
	validate((void *)st);
	fs_node_t * fn = kopen(file, 0);
	result = stat_node(fn, st);
	if (fn) {
		close_fs(fn);
	}
	return result;
}

static int sys_chmod(char * file, int mode) {
	int result;
	validate((void *)file);
	fs_node_t * fn = kopen(file, 0);
	if (fn) {
		result = chmod_fs(fn, mode);
		close_fs(fn);
		return result;
	} else {
		return -1;
	}
}


static int stat(int fd, uintptr_t st) {
	validate((void *)st);
	if (fd >= (int)current_process->fds->length || fd < 0) {
		return -1;
	}
	fs_node_t * fn = current_process->fds->entries[fd];
	return stat_node(fn, st);
}

static int setgraphicsoffset(int rows) {
	bochs_set_y_offset(rows);
	return 0;
}

static int getgraphicswidth(void) {
	return lfb_resolution_x;
}

static int getgraphicsheight(void) {
	return lfb_resolution_y;
}

static int getgraphicsdepth(void) {
	return lfb_resolution_b;
}

static int mkpipe(void) {
	fs_node_t * node = make_pipe(4096 * 2);
	return process_append_fd((process_t *)current_process, node);
}

static int dup2(int old, int new) {
	process_move_fd((process_t *)current_process, old, new);
	return new;
}

static int getuid(void) {
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

static int uname(struct utsname * name) {
	validate((void *)name);
	char version_number[256];
	sprintf(version_number, __kernel_version_format,
			__kernel_version_major,
			__kernel_version_minor,
			__kernel_version_lower,
			__kernel_version_suffix);
	char version_string[256];
	sprintf(version_string, "%s %s %s",
			__kernel_version_codename,
			__kernel_build_date,
			__kernel_build_time);
	strcpy(name->sysname,  __kernel_name);
	strcpy(name->nodename, hostname);
	strcpy(name->release,  version_number);
	strcpy(name->version,  version_string);
	strcpy(name->machine,  __kernel_arch);
	strcpy(name->domainname, "");
	return 0;
}

int send_signal(pid_t process, uint32_t signal) {
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

extern void ext2_disk_sync(void);

static int reboot(void) {
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
	if (!buf) {
		debug_print(WARNING, "getcwd got NULL for buf, investigate");
		return NULL;
	}
	validate((void *)buf);
	memcpy(buf, current_process->wd_name, min(size, strlen(current_process->wd_name) + 1));
	return buf;
}

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

static int mousedevice(void) {
	extern fs_node_t * mouse_pipe;
	return process_append_fd((process_t *)current_process, mouse_pipe);
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
static int yield(void) {
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
			case 5:
				debug_print(ERROR, "this system function has been deprecated", getpid());
				break;
			case 6:
				debug_print(WARNING, "writing contents of file %s to sdb", args[0]);
				{
					fs_node_t * file = kopen((char *)args[0], 0);
					if (!file) {
						return -1;
					}
					size_t length = file->length;
					uint8_t * buffer = malloc(length);
					read_fs(file, 0, length, (uint8_t *)buffer);
					close_fs(file);
					debug_print(WARNING, "Finished reading file, going to write it now.\n");

#define DISK_PORT 0x1F0
					size_t i = 0;
					for (i = 0; i < length; i += 512) {
						debug_print(WARNING, "... %d / %d", i, length);
						ide_write_sector_retry(DISK_PORT, 1, i / 512, (uint8_t *)(&buffer[i]));
					}
					if (i < length) {
						uint8_t * tmp = malloc(512);
						memset(tmp, 0x00, 512);
						memcpy(tmp, &buffer[i], length % 512);
						ide_write_sector_retry(DISK_PORT, 1, i / 512, tmp);
						free(tmp);
					}

					free(buffer);
					return 0;
				}
			default:
				kprintf("Bad system function %d\n", fn);
				break;
		}
	}
	return -1; /* Bad system function or access failure */
}

static int sleep(unsigned long seconds, unsigned long subseconds) {
	/* Mark us as asleep until <some time period> */
	sleep_until((process_t *)current_process, seconds, subseconds);

	/* Switch without adding us to the queue */
	switch_task(0);

	if (seconds > timer_ticks || (seconds == timer_ticks && subseconds >= timer_subticks)) {
		return 0;
	} else {
		return 1;
	}
}

static int sleep_rel(unsigned long seconds, unsigned long subseconds) {
	unsigned long s, ss;
	relative_time(seconds, subseconds, &s, &ss);
	return sleep(s, ss);
}

static int sys_umask(int mode) {
	current_process->mask = mode & 0777;
	return 0;
}

static int sys_unlink(char * file) {
	return unlink_fs(file);
}

/*
 * System Call Internals
 */
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
	(uintptr_t)&uname,              /* 12 */
	(uintptr_t)&openpty,
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
	(uintptr_t)NULL,                /* 44 */
	(uintptr_t)&sleep,
	(uintptr_t)&sleep_rel,
	(uintptr_t)&ioctl,
	(uintptr_t)&access,             /* 48 */
	(uintptr_t)&stat_file,
	(uintptr_t)&sys_chmod,
	(uintptr_t)&sys_umask,
	(uintptr_t)&sys_unlink,         /* 52 */
	(uintptr_t)&advanced_wait,
	0
};
uint32_t num_syscalls;

typedef uint32_t (*scall_func)(unsigned int, ...);

void syscall_handler(struct regs * r) {
	if (r->eax >= num_syscalls) {
		return;
	}

	uintptr_t location = syscalls[r->eax];
	if (!location) {
		return;
	}

	/* Update the syscall registers for this process */
	current_process->syscall_registers = r;

	/* Call the syscall function */
	scall_func func = (scall_func)location;
	uint32_t ret = func(r->ebx, r->ecx, r->edx, r->esi, r->edi);

	if ((current_process->syscall_registers == r) ||
			(location != (uintptr_t)&fork && location != (uintptr_t)&clone)) {
		r->eax = ret;
	}
}

void syscalls_install(void) {
	for (num_syscalls = 0; syscalls[num_syscalls] != 0; ++num_syscalls);
	debug_print(NOTICE, "Initializing syscall table with %d functions", num_syscalls);
	isrs_install_handler(0x7F, &syscall_handler);
}

