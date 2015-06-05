/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2011-2014 Kevin Lange
 * Copyright (C) 2012 Markus Schober
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
#include <printf.h>
#include <syscall_nums.h>

static char   hostname[256];
static size_t hostname_len = 0;

#define FD_INRANGE(FD) \
	((FD) < (int)current_process->fds->length && (FD) >= 0)
#define FD_ENTRY(FD) \
	(current_process->fds->entries[(FD)])
#define FD_CHECK(FD) \
	(FD_INRANGE(FD) && FD_ENTRY(FD))

#define PTR_INRANGE(PTR) \
	((uintptr_t)(PTR) > current_process->image.entry)
#define PTR_VALIDATE(PTR) \
	ptr_validate((void *)(PTR), __func__)

static void ptr_validate(void * ptr, const char * syscall) {
	if (ptr && !PTR_INRANGE(ptr)) {
		debug_print(ERROR, "SEGFAULT: invalid pointer passed to %s. (0x%x < 0x%x)",
			syscall, (uintptr_t)ptr, current_process->image.entry);
		HALT_AND_CATCH_FIRE("Segmentation fault", NULL);
	}
}

void validate(void * ptr) {
	ptr_validate(ptr, "syscall");
}

/*
 * Exit the current task.
 * DOES NOT RETURN!
 */
static int __attribute__((noreturn)) sys_exit(int retval) {
	/* Deschedule the current task */
	task_exit(retval);
	for (;;) ;
}

static int sys_read(int fd, char * ptr, int len) {
	if (FD_CHECK(fd)) {
		PTR_VALIDATE(ptr);
		fs_node_t * node = FD_ENTRY(fd);
		uint32_t out = read_fs(node, node->offset, len, (uint8_t *)ptr);
		node->offset += out;
		return (int)out;
	}
	return -1;
}

static int sys_ioctl(int fd, int request, void * argp) {
	if (FD_CHECK(fd)) {
		PTR_VALIDATE(argp);
		return ioctl_fs(FD_ENTRY(fd), request, argp);
	}
	return -1;
}

static int sys_readdir(int fd, int index, struct dirent * entry) {
	if (FD_CHECK(fd)) {
		PTR_VALIDATE(entry);
		struct dirent * kentry = readdir_fs(FD_ENTRY(fd), (uint32_t)index);
		if (kentry) {
			memcpy(entry, kentry, sizeof *entry);
			free(kentry);
			return 0;
		} else {
			return 1;
		}
	}
	return -1;
}

static int sys_write(int fd, char * ptr, int len) {
	if (FD_CHECK(fd)) {
		PTR_VALIDATE(ptr);
		fs_node_t * node = FD_ENTRY(fd);
		uint32_t out = write_fs(node, node->offset, len, (uint8_t *)ptr);
		node->offset += out;
		return out;
	}
	return -1;
}

static int sys_waitpid(int pid, int * status, int options) {
	if (status && !PTR_INRANGE(status)) {
		return -EINVAL;
	}
	return waitpid(pid, status, options);
}

static int sys_open(const char * file, int flags, int mode) {
	PTR_VALIDATE(file);
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

static int sys_access(const char * file, int flags) {
	PTR_VALIDATE(file);
	debug_print(INFO, "access(%s, 0x%x) from pid=%d", file, flags, getpid());
	fs_node_t * node = kopen((char *)file, 0);
	if (!node) return -1;
	close_fs(node);
	return 0;
}

static int sys_close(int fd) {
	if (FD_CHECK(fd)) {
		close_fs(FD_ENTRY(fd));
		FD_ENTRY(fd) = NULL;
		return 0;
	}
	return -1;
}

static int sys_sbrk(int size) {
	process_t * proc = (process_t *)current_process;
	if (proc->group != 0) {
		proc = process_from_pid(proc->group);
	}
	spin_lock(proc->image.lock);
	uintptr_t ret = proc->image.heap;
	uintptr_t i_ret = ret;
	while (ret % 0x1000) {
		ret++;
	}
	proc->image.heap += (ret - i_ret) + size;
	while (proc->image.heap > proc->image.heap_actual) {
		proc->image.heap_actual += 0x1000;
		assert(proc->image.heap_actual % 0x1000 == 0);
		alloc_frame(get_page(proc->image.heap_actual, 1, current_directory), 0, 1);
		invalidate_tables_at(proc->image.heap_actual);
	}
	spin_unlock(proc->image.lock);
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
static int sys_gettid(void) {
	return getpid();
}

static int sys_execve(const char * filename, char *const argv[], char *const envp[]) {
	PTR_VALIDATE(argv);
	PTR_VALIDATE(filename);
	PTR_VALIDATE(envp);

	debug_print(NOTICE, "%d = exec(%s, ...)", current_process->id, filename);

	int argc = 0;
	int envc = 0;
	while (argv[argc]) {
		PTR_VALIDATE(argv[argc]);
		++argc;
	}

	if (envp) {
		while (envp[envc]) {
			PTR_VALIDATE(envp[envc]);
			++envc;
		}
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

static int sys_seek(int fd, int offset, int whence) {
	if (FD_CHECK(fd)) {
		if (fd < 3) {
			return 0;
		}
		switch (whence) {
			case 0:
				FD_ENTRY(fd)->offset = offset;
				break;
			case 1:
				FD_ENTRY(fd)->offset += offset;
				break;
			case 2:
				FD_ENTRY(fd)->offset = FD_ENTRY(fd)->length + offset;
				break;
		}
		return FD_ENTRY(fd)->offset;
	}
	return -1;
}

static int stat_node(fs_node_t * fn, uintptr_t st) {
	struct stat * f = (struct stat *)st;

	PTR_VALIDATE(f);

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
	f->st_nlink = fn->nlink;
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

static int sys_statf(char * file, uintptr_t st) {
	int result;
	PTR_VALIDATE(file);
	PTR_VALIDATE(st);
	fs_node_t * fn = kopen(file, 0);
	result = stat_node(fn, st);
	if (fn) {
		close_fs(fn);
	}
	return result;
}

static int sys_chmod(char * file, int mode) {
	int result;
	PTR_VALIDATE(file);
	fs_node_t * fn = kopen(file, 0);
	if (fn) {
		result = chmod_fs(fn, mode);
		close_fs(fn);
		return result;
	} else {
		return -1;
	}
}


static int sys_stat(int fd, uintptr_t st) {
	PTR_VALIDATE(st);
	if (FD_CHECK(fd)) {
		return stat_node(FD_ENTRY(fd), st);
	}
	return -1;
}

static int sys_mkpipe(void) {
	fs_node_t * node = make_pipe(4096 * 2);
	open_fs(node, 0);
	return process_append_fd((process_t *)current_process, node);
}

static int sys_dup2(int old, int new) {
	process_move_fd((process_t *)current_process, old, new);
	return new;
}

static int sys_getuid(void) {
	return current_process->user;
}

static int sys_setuid(user_t new_uid) {
	if (current_process->user == USER_ROOT_UID) {
		current_process->user = new_uid;
		return 0;
	}
	return -1;
}

static int sys_uname(struct utsname * name) {
	PTR_VALIDATE(name);
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

static int sys_signal(uint32_t signum, uintptr_t handler) {
	if (signum > NUMSIGNALS) {
		return -1;
	}
	uintptr_t old = current_process->signals.functions[signum];
	current_process->signals.functions[signum] = handler;
	return (int)old;
}

/*
static void inspect_memory (uintptr_t vaddr) {
	// Please use this scary hack of a function as infrequently as possible.
	shmem_debug_frame(vaddr);
}
*/

static int sys_reboot(void) {
	debug_print(NOTICE, "[kernel] Reboot requested from process %d by user #%d", current_process->id, current_process->user);
	if (current_process->user != USER_ROOT_UID) {
		return -1;
	} else {
		debug_print(NOTICE, "[kernel] Good bye!");
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

static int sys_chdir(char * newdir) {
	PTR_VALIDATE(newdir);
	char * path = canonicalize_path(current_process->wd_name, newdir);
	fs_node_t * chd = kopen(path, 0);
	if (chd) {
		if ((chd->flags & FS_DIRECTORY) == 0) {
			close_fs(chd);
			return -1;
		}
		close_fs(chd);
		free(current_process->wd_name);
		current_process->wd_name = malloc(strlen(path) + 1);
		memcpy(current_process->wd_name, path, strlen(path) + 1);
		return 0;
	} else {
		return -1;
	}
}

static int sys_getcwd(char * buf, size_t size) {
	if (buf) {
		PTR_VALIDATE(buf);
		size_t len = strlen(current_process->wd_name) + 1;
		return (int)memcpy(buf, current_process->wd_name, MIN(size, len));
	}
	return 0;
}

static int sys_sethostname(char * new_hostname) {
	if (current_process->user == USER_ROOT_UID) {
		PTR_VALIDATE(new_hostname);
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

static int sys_gethostname(char * buffer) {
	PTR_VALIDATE(buffer);
	memcpy(buffer, hostname, hostname_len);
	return hostname_len;
}

extern int mkdir_fs(char *name, uint16_t permission);

static int sys_mkdir(char * path, uint32_t mode) {
	return mkdir_fs(path, 0777);
}

/*
 * Yield the rest of the quantum;
 * useful for busy waiting and other such things
 */
static int sys_yield(void) {
	switch_task(1);
	return 1;
}

/*
 * System Function
 */
static int sys_sysfunc(int fn, char ** args) {
	/* System Functions are special debugging system calls */
	if (current_process->user == USER_ROOT_UID) {
		switch (fn) {
			case 3:
				debug_print(ERROR, "sync is currently unimplemented");
				//ext2_disk_sync();
				return 0;
			case 4:
				/* Request kernel output to file descriptor in arg0*/
				debug_print(NOTICE, "Setting output to file object in process %d's fd=%d!", getpid(), (int)args);
				debug_file = current_process->fds->entries[(int)args];
				break;
			case 5:
				{
					char *arg;
					PTR_VALIDATE(args);
					for (arg = args[0]; arg; arg++)
						PTR_VALIDATE(arg);
					debug_print(NOTICE, "Replacing process %d's file descriptors with pointers to %s", getpid(), (char *)args);
					fs_node_t * repdev = kopen((char *)args, 0);
					while (current_process->fds->length < 3) {
						process_append_fd((process_t *)current_process, repdev);
					}
					FD_ENTRY(0) = repdev;
					FD_ENTRY(1) = repdev;
					FD_ENTRY(2) = repdev;
				}
				break;
			case 6:
				debug_print(WARNING, "writing contents of file %s to sdb", args[0]);
				{
					PTR_VALIDATE(args);
					PTR_VALIDATE(args[0]);
					fs_node_t * file = kopen((char *)args[0], 0);
					if (!file) {
						return -1;
					}
					size_t length = file->length;
					uint8_t * buffer = malloc(length);
					read_fs(file, 0, length, (uint8_t *)buffer);
					close_fs(file);
					debug_print(WARNING, "Finished reading file, going to write it now.");

					fs_node_t * f = kopen("/dev/sdb", 0);
					if (!f) {
						return 1;
					}

					write_fs(f, 0, length, buffer);

					free(buffer);
					return 0;
				}
			case 7:
				debug_print(NOTICE, "Spawning debug hook as child of process %d.", getpid());
				if (debug_hook) {
					fs_node_t * tty = FD_ENTRY(0);
					return create_kernel_tasklet(debug_hook, "[kttydebug]", tty);
				} else {
					return -1;
				}
			default:
				debug_print(ERROR, "Bad system function %d", fn);
				break;
		}
	}
	return -1; /* Bad system function or access failure */
}

static int sys_sleepabs(unsigned long seconds, unsigned long subseconds) {
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

static int sys_sleep(unsigned long seconds, unsigned long subseconds) {
	unsigned long s, ss;
	relative_time(seconds, subseconds * 10, &s, &ss);
	return sys_sleepabs(s, ss);
}

static int sys_umask(int mode) {
	current_process->mask = mode & 0777;
	return 0;
}

static int sys_unlink(char * file) {
	PTR_VALIDATE(file);
	return unlink_fs(file);
}

static int sys_fork(void) {
	return (int)fork();
}

static int sys_clone(uintptr_t new_stack, uintptr_t thread_func, uintptr_t arg) {
	if (!new_stack || !PTR_INRANGE(new_stack)) return -1;
	if (!thread_func || !PTR_INRANGE(thread_func)) return -1;
	return (int)clone(new_stack, thread_func, arg);
}

static int sys_shm_obtain(char * path, size_t * size) {
	PTR_VALIDATE(path);
	PTR_VALIDATE(size);

	return (int)shm_obtain(path, size);
}

static int sys_shm_release(char * path) {
	PTR_VALIDATE(path);

	return shm_release(path);
}

static int sys_kill(pid_t process, uint32_t signal) {
	return send_signal(process, signal);
}

static int sys_gettimeofday(struct timeval * tv, void * tz) {
	PTR_VALIDATE(tv);
	PTR_VALIDATE(tz);

	return gettimeofday(tv, tz);
}

static int sys_openpty(int * master, int * slave, char * name, void * _ign0, void * size) {
	/* We require a place to put these when we are done. */
	if (!master || !slave) return -1;
	if (master && !PTR_INRANGE(master)) return -1;
	if (slave && !PTR_INRANGE(slave)) return -1;
	if (size && !PTR_INRANGE(size)) return -1;

	/* Create a new pseudo terminal */
	fs_node_t * fs_master;
	fs_node_t * fs_slave;

	pty_create(size, &fs_master, &fs_slave);

	/* Append the master and slave to the calling process */
	*master = process_append_fd((process_t *)current_process, fs_master);
	*slave  = process_append_fd((process_t *)current_process, fs_slave);

	open_fs(fs_master, 0);
	open_fs(fs_slave, 0);

	/* Return success */
	return 0;
}

static int sys_pipe(int pipes[2]) {
	if (pipes && !PTR_INRANGE(pipes)) {
		return -EFAULT;
	}

	fs_node_t * outpipes[2];

	make_unix_pipe(outpipes);

	open_fs(outpipes[0], 0);
	open_fs(outpipes[1], 0);

	pipes[0] = process_append_fd((process_t *)current_process, outpipes[0]);
	pipes[1] = process_append_fd((process_t *)current_process, outpipes[1]);
	return 0;
}

static int sys_mount(char * arg, char * mountpoint, char * type, unsigned long flags, void * data) {
	/* TODO: Make use of flags and data from mount command. */
	(void)flags;
	(void)data;

	if (current_process->user != USER_ROOT_UID) {
		return -EPERM;
	}

	if (PTR_INRANGE(arg) && PTR_INRANGE(mountpoint) && PTR_INRANGE(type)) {
		return vfs_mount_type(type, arg, mountpoint);
	}

	return -EFAULT;
}

static int sys_symlink(char * target, char * name) {
	PTR_VALIDATE(target);
	PTR_VALIDATE(name);
	return symlink_fs(target, name);
}

static int sys_readlink(const char * file, char * ptr, int len) {
	PTR_VALIDATE(file);
	fs_node_t * node = kopen((char *) file, O_PATH | O_NOFOLLOW);
	if (!node) {
		return -ENOENT;
	}
	int rv = readlink_fs(node, ptr, len);
	close_fs(node);
	return rv;
}

static int sys_lstat(char * file, uintptr_t st) {
	int result;
	PTR_VALIDATE(file);
	PTR_VALIDATE(st);
	fs_node_t * fn = kopen(file, O_PATH | O_NOFOLLOW);
	result = stat_node(fn, st);
	if (fn) {
		close_fs(fn);
	}
	return result;
}

/*
 * System Call Internals
 */
static int (*syscalls[])() = {
	/* System Call Table */
	[SYS_EXT]          = sys_exit,
	[SYS_OPEN]         = sys_open,
	[SYS_READ]         = sys_read,
	[SYS_WRITE]        = sys_write,
	[SYS_CLOSE]        = sys_close,
	[SYS_GETTIMEOFDAY] = sys_gettimeofday,
	[SYS_EXECVE]       = sys_execve,
	[SYS_FORK]         = sys_fork,
	[SYS_GETPID]       = sys_getpid,
	[SYS_SBRK]         = sys_sbrk,
	[SYS_UNAME]        = sys_uname,
	[SYS_OPENPTY]      = sys_openpty,
	[SYS_SEEK]         = sys_seek,
	[SYS_STAT]         = sys_stat,
	[SYS_MKPIPE]       = sys_mkpipe,
	[SYS_DUP2]         = sys_dup2,
	[SYS_GETUID]       = sys_getuid,
	[SYS_SETUID]       = sys_setuid,
	[SYS_REBOOT]       = sys_reboot,
	[SYS_READDIR]      = sys_readdir,
	[SYS_CHDIR]        = sys_chdir,
	[SYS_GETCWD]       = sys_getcwd,
	[SYS_CLONE]        = sys_clone,
	[SYS_SETHOSTNAME]  = sys_sethostname,
	[SYS_GETHOSTNAME]  = sys_gethostname,
	[SYS_MKDIR]        = sys_mkdir,
	[SYS_SHM_OBTAIN]   = sys_shm_obtain,
	[SYS_SHM_RELEASE]  = sys_shm_release,
	[SYS_KILL]         = sys_kill,
	[SYS_SIGNAL]       = sys_signal,
	[SYS_GETTID]       = sys_gettid,
	[SYS_YIELD]        = sys_yield,
	[SYS_SYSFUNC]      = sys_sysfunc,
	[SYS_SLEEPABS]     = sys_sleepabs,
	[SYS_SLEEP]        = sys_sleep,
	[SYS_IOCTL]        = sys_ioctl,
	[SYS_ACCESS]       = sys_access,
	[SYS_STATF]        = sys_statf,
	[SYS_CHMOD]        = sys_chmod,
	[SYS_UMASK]        = sys_umask,
	[SYS_UNLINK]       = sys_unlink,
	[SYS_WAITPID]      = sys_waitpid,
	[SYS_PIPE]         = sys_pipe,
	[SYS_MOUNT]        = sys_mount,
	[SYS_SYMLINK]      = sys_symlink,
	[SYS_READLINK]     = sys_readlink,
	[SYS_LSTAT]        = sys_lstat,
};

uint32_t num_syscalls = sizeof(syscalls) / sizeof(*syscalls);

typedef uint32_t (*scall_func)(unsigned int, ...);

void syscall_handler(struct regs * r) {
	if (r->eax >= num_syscalls) {
		return;
	}

	uintptr_t location = (uintptr_t)syscalls[r->eax];
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
	debug_print(NOTICE, "Initializing syscall table with %d functions", num_syscalls);
	isrs_install_handler(0x7F, &syscall_handler);
}

