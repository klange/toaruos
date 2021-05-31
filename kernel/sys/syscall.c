#include <stdint.h>
#include <errno.h>
#include <sys/sysfunc.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/time.h>
#include <syscall_nums.h>
#include <kernel/printf.h>
#include <kernel/process.h>
#include <kernel/string.h>
#include <kernel/version.h>
#include <kernel/pipe.h>
#include <kernel/shm.h>
#include <kernel/mmu.h>
#include <kernel/pty.h>
#include <kernel/spinlock.h>
#include <kernel/signal.h>
#include <kernel/time.h>
#include <kernel/syscall.h>
#include <kernel/misc.h>

static char   hostname[256];
static size_t hostname_len = 0;

void ptr_validate(void * ptr, const char * syscall) {
	if (ptr && !PTR_INRANGE(ptr)) {
		printf("invalid pointer passed to %s (%p < %p)\n",
			syscall, ptr, (void*)this_core->current_process->image.entry);
		while (1) {}
	}
}

static long sys_sbrk(ssize_t size) {
	if (size & 0xFFF) return -EINVAL;
	volatile process_t * volatile proc = this_core->current_process;
	if (proc->group != 0) {
		proc = process_from_pid(proc->group);
	}
	spin_lock(proc->image.lock);
	uintptr_t out = proc->image.heap;
	for (uintptr_t i = out; i < out + size; i += 0x1000) {
		union PML * page = mmu_get_page(i, MMU_GET_MAKE);
		if (page->bits.page != 0) {
			printf("odd, %#zx is already allocated?\n", i);
		}
		mmu_frame_allocate(page, MMU_FLAG_WRITABLE);
		mmu_invalidate(i);
	}
	proc->image.heap += size;
	spin_unlock(proc->image.lock);
	return (long)out;
}

static long sys_sysfunc(long fn, char ** args) {
	/* FIXME: Most of these should be top-level, many are hacks/broken in Misaka */
	switch (fn) {
		case TOARU_SYS_FUNC_SYNC:
			/* FIXME: There is no sync ability in the VFS at the moment. */
			printf("sync: not implemented\n");
			return -EINVAL;
		case TOARU_SYS_FUNC_LOGHERE:
			/* FIXME: Needs to redirect kprintf to the argument */
			printf("loghere: not implemented\n");
			return -EINVAL;
		case TOARU_SYS_FUNC_SETFDS:
			/* XXX Unused */
			printf("setfds: not implemented\n");
			return -EINVAL;
		case TOARU_SYS_FUNC_WRITESDB:
			/* XXX Unused */
			printf("writesdb: not implemented\n");
			return -EINVAL;
		case TOARU_SYS_FUNC_KDEBUG:
			/* FIXME: Starts kernel debugger as a child task of this process */
			printf("kdebug: not implemented\n");
			return -EINVAL;
		case TOARU_SYS_FUNC_INSMOD:
			/* FIXME: Load module */
			printf("insmod: not implemented\n");
			return -EINVAL;
		/* Begin unpriv */
		case TOARU_SYS_FUNC_SETHEAP: {
			volatile process_t * volatile proc = this_core->current_process;
			if (proc->group != 0) proc = process_from_pid(proc->group);
			spin_lock(proc->image.lock);
			proc->image.heap = (uintptr_t)args[0];
			spin_unlock(proc->image.lock);
			return 0;
		}
		case TOARU_SYS_FUNC_MMAP: {
			/* FIXME: This whole thing should be removed, tbh */
			volatile process_t * volatile proc = this_core->current_process;
			if (proc->group != 0) proc = process_from_pid(proc->group);
			spin_lock(proc->image.lock);
			/* Align inputs */
			uintptr_t start = ((uintptr_t)args[0]) & 0xFFFFffffFFFFf000UL;
			uintptr_t end   = ((uintptr_t)args[0] + (size_t)args[1] + 0xFFF) & 0xFFFFffffFFFFf000UL;
			for (uintptr_t i = start; i < end; i += 0x1000) {
				union PML * page = mmu_get_page(i, MMU_GET_MAKE);
				mmu_frame_allocate(page, MMU_FLAG_WRITABLE);
				mmu_invalidate(i);
			}
			spin_unlock(proc->image.lock);
			return 0;
		}
		case TOARU_SYS_FUNC_THREADNAME: {
			/* This should probably be moved to a new system call. */
			int count = 0;
			char **arg = args;
			PTR_VALIDATE(args);
			while (*arg) {
				PTR_VALIDATE(*args);
				count++;
				arg++;
			}
			this_core->current_process->cmdline = malloc(sizeof(char*)*(count+1));
			int i = 0;
			while (i < count) {
				this_core->current_process->cmdline[i] = strdup(args[i]);
				i++;
			}
			this_core->current_process->cmdline[i] = NULL;
			return 0;
		}
		case TOARU_SYS_FUNC_DEBUGPRINT:
			/* XXX I think _xlog uses this? */
			printf("debugprint: not implemented\n");
			return -EINVAL;
		case TOARU_SYS_FUNC_SETVGACURSOR:
			/* XXX This should be a device driver, along with the text-mode window... */
			printf("setvgacursor: not implemented\n");
			return -EINVAL;
		case TOARU_SYS_FUNC_SETGSBASE:
			PTR_VALIDATE(args);
			this_core->current_process->thread.context.tls_base = (uintptr_t)args[0];
			arch_set_tls_base(this_core->current_process->thread.context.tls_base);
			return 0;
		default:
			printf("Bad system function: %ld\n", fn);
			return -EINVAL;
	}
}

__attribute__((noreturn))
static long sys_exit(long exitcode) {
	/* TODO remove print */
	//printf("(process %d [%s] exited with %ld)\n", current_process->id, current_process->name, exitcode);

	task_exit((exitcode & 0xFF) << 8);
	__builtin_unreachable();
}

static long sys_write(int fd, char * ptr, unsigned long len) {
	if (FD_CHECK(fd)) {
		PTR_VALIDATE(ptr);
		fs_node_t * node = FD_ENTRY(fd);
		if (!(FD_MODE(fd) & 2)) return -EACCES;
		int64_t out = write_fs(node, FD_OFFSET(fd), len, (uint8_t*)ptr);
		if (out > 0) {
			FD_OFFSET(fd) += out;
		}
		return out;
	}
	return -EBADF;
}

static long stat_node(fs_node_t * fn, uintptr_t st) {
	struct stat * f = (struct stat *)st;

	PTR_VALIDATE(f);

	if (!fn) {
		/* XXX: Does this need to zero the stat struct when returning -ENOENT? */
		memset(f, 0x00, sizeof(struct stat));
		return -ENOENT;
	}

	f->st_dev   = (uint16_t)(((uint64_t)fn->device & 0xFFFF0) >> 8);
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
	f->st_blksize = 512; /* whatever */

	if (fn->get_size) {
		f->st_size = fn->get_size(fn);
	}

	return 0;
}

static long sys_stat(int fd, uintptr_t st) {
	PTR_VALIDATE(st);
	if (FD_CHECK(fd)) {
		return stat_node(FD_ENTRY(fd), st);
	}
	return -EBADF;
}

static long sys_statf(char * file, uintptr_t st) {
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

static long sys_symlink(char * target, char * name) {
	PTR_VALIDATE(target);
	PTR_VALIDATE(name);
	return symlink_fs(target, name);
}

static long sys_readlink(const char * file, char * ptr, long len) {
	PTR_VALIDATE(file);
	fs_node_t * node = kopen((char *) file, O_PATH | O_NOFOLLOW);
	if (!node) {
		return -ENOENT;
	}
	long rv = readlink_fs(node, ptr, len);
	close_fs(node);
	return rv;
}

static long sys_lstat(char * file, uintptr_t st) {
	PTR_VALIDATE(file);
	PTR_VALIDATE(st);
	fs_node_t * fn = kopen(file, O_PATH | O_NOFOLLOW);
	long result = stat_node(fn, st);
	if (fn) {
		close_fs(fn);
	}
	return result;
}

static long sys_open(const char * file, long flags, long mode) {
	PTR_VALIDATE(file);
	fs_node_t * node = kopen((char *)file, flags);

	int access_bits = 0;

	if (node && (flags & O_CREAT) && (flags & O_EXCL)) {
		close_fs(node);
		return -EEXIST;
	}

	if (!(flags & O_WRONLY) || (flags & O_RDWR)) {
		if (node && !has_permission(node, 04)) {
			close_fs(node);
			return -EACCES;
		} else {
			access_bits |= 01;
		}
	}

	if ((flags & O_RDWR) || (flags & O_WRONLY)) {
		if (node && !has_permission(node, 02)) {
			close_fs(node);
			return -EACCES;
		}
		if (node && (node->flags & FS_DIRECTORY)) {
			return -EISDIR;
		}
		if ((flags & O_RDWR) || (flags & O_WRONLY)) {
			/* truncate doesn't grant write permissions */
			access_bits |= 02;
		}
	}

	if (!node && (flags & O_CREAT)) {
		/* TODO check directory permissions */
		int result = create_file_fs((char *)file, mode);
		if (!result) {
			node = kopen((char *)file, flags);
		} else {
			return result;
		}
	}

	if (node && (flags & O_DIRECTORY)) {
		if (!(node->flags & FS_DIRECTORY)) {
			return -ENOTDIR;
		}
	}

	if (node && (flags & O_TRUNC)) {
		if (!(access_bits & 02)) {
			close_fs(node);
			return -EINVAL;
		}
		truncate_fs(node);
	}

	if (!node) {
		return -ENOENT;
	}
	if (node && (flags & O_CREAT) && (node->flags & FS_DIRECTORY)) {
		close_fs(node);
		return -EISDIR;
	}
	int fd = process_append_fd((process_t *)this_core->current_process, node);
	FD_MODE(fd) = access_bits;
	if (flags & O_APPEND) {
		FD_OFFSET(fd) = node->length;
	} else {
		FD_OFFSET(fd) = 0;
	}
	return fd;
}

static long sys_close(int fd) {
	if (FD_CHECK(fd)) {
		close_fs(FD_ENTRY(fd));
		FD_ENTRY(fd) = NULL;
		return 0;
	}
	return -EBADF;
}

static long sys_seek(int fd, long offset, long whence) {
	if (FD_CHECK(fd)) {
		if ((FD_ENTRY(fd)->flags & FS_PIPE) || (FD_ENTRY(fd)->flags & FS_CHARDEVICE)) return -ESPIPE;
		switch (whence) {
			case 0:
				FD_OFFSET(fd) = offset;
				break;
			case 1:
				FD_OFFSET(fd) += offset;
				break;
			case 2:
				FD_OFFSET(fd) = FD_ENTRY(fd)->length + offset;
				break;
			default:
				return -EINVAL;
		}
		return FD_OFFSET(fd);
	}
	return -EBADF;
}

static long sys_read(int fd, char * ptr, unsigned long len) {
	if (FD_CHECK(fd)) {
		PTR_VALIDATE(ptr);

		fs_node_t * node = FD_ENTRY(fd);
		if (!(FD_MODE(fd) & 01)) {
			return -EACCES;
		}
		uint64_t out = read_fs(node, FD_OFFSET(fd), len, (uint8_t *)ptr);
		FD_OFFSET(fd) += out;
		return out;
	}
	return -EBADF;
}

static long sys_ioctl(int fd, int request, void * argp) {
	if (FD_CHECK(fd)) {
		PTR_VALIDATE(argp);
		return ioctl_fs(FD_ENTRY(fd), request, argp);
	}
	return -EBADF;
}

static long sys_readdir(int fd, long index, struct dirent * entry) {
	if (FD_CHECK(fd)) {
		PTR_VALIDATE(entry);
		struct dirent * kentry = readdir_fs(FD_ENTRY(fd), (uint64_t)index);
		if (kentry) {
			memcpy(entry, kentry, sizeof *entry);
			free(kentry);
			return 1;
		} else {
			return 0;
		}
	}
	return -EBADF;
}

static long sys_mkdir(char * path, uint64_t mode) {
	return mkdir_fs(path, mode);
}

static long sys_access(const char * file, long flags) {
	PTR_VALIDATE(file);
	fs_node_t * node = kopen((char *)file, 0);
	if (!node) return -ENOENT;
	close_fs(node);
	return 0;
}

static long sys_chmod(char * file, long mode) {
	PTR_VALIDATE(file);
	fs_node_t * fn = kopen(file, 0);
	if (fn) {
		/* Can group members change bits? I think it's only owners. */
		if (this_core->current_process->user != 0 && this_core->current_process->user != fn->uid) {
			close_fs(fn);
			return -EACCES;
		}
		long result = chmod_fs(fn, mode);
		close_fs(fn);
		return result;
	} else {
		return -ENOENT;
	}
}

static long sys_chown(char * file, uid_t uid, uid_t gid) {
	PTR_VALIDATE(file);
	fs_node_t * fn = kopen(file, 0);
	if (fn) {
		/* TODO: Owners can change groups... */
		if (this_core->current_process->user != 0) {
			close_fs(fn);
			return -EACCES;
		}
		long result = chown_fs(fn, uid, gid);
		close_fs(fn);
		return result;
	} else {
		return -ENOENT;
	}
}

static long sys_gettimeofday(struct timeval * tv, void * tz) {
	PTR_VALIDATE(tv);
	PTR_VALIDATE(tz);
	return gettimeofday(tv, tz);
}

static long sys_getuid(void) {
	return (long)this_core->current_process->real_user;
}

static long sys_geteuid(void) {
	return (long)this_core->current_process->user;
}

static long sys_setuid(uid_t new_uid) {
	if (this_core->current_process->user == USER_ROOT_UID) {
		this_core->current_process->user = new_uid;
		this_core->current_process->real_user = new_uid;
		return 0;
	}
	return -EPERM;
}

static long sys_getpid(void) {
	/* The user actually wants the pid of the originating thread (which can be us). */
	return this_core->current_process->group ? (long)this_core->current_process->group : (long)this_core->current_process->id;
}

static long sys_gettid(void) {
	return (long)this_core->current_process->id;
}

static long sys_setsid(void) {
	if (this_core->current_process->job == this_core->current_process->group) {
		return -EPERM;
	}
	this_core->current_process->session = this_core->current_process->group;
	this_core->current_process->job = this_core->current_process->group;
	return this_core->current_process->session;
}

static long sys_setpgid(pid_t pid, pid_t pgid) {
	if (pgid < 0) {
		return -EINVAL;
	}
	process_t * proc = NULL;
	if (pid == 0) {
		proc = (process_t*)this_core->current_process;
	} else {
		proc = process_from_pid(pid);
	}

	if (!proc) {
		return -ESRCH;
	}
	if (proc->session != this_core->current_process->session || proc->session == proc->group) {
		return -EPERM;
	}

	if (pgid == 0) {
		proc->job = proc->group;
	} else {
		process_t * pgroup = process_from_pid(pgid);

		if (!pgroup || pgroup->session != proc->session) {
			return -EPERM;
		}

		proc->job = pgid;
	}
	return 0;
}

static long sys_getpgid(pid_t pid) {
	process_t * proc;
	if (pid == 0) {
		proc = (process_t*)this_core->current_process;
	} else {
		proc = NULL; process_from_pid(pid);
	}

	if (!proc) {
		return -ESRCH;
	}

	return proc->job;
}

static long sys_uname(struct utsname * name) {
	PTR_VALIDATE(name);
	char version_number[256];
	snprintf(version_number, 255, __kernel_version_format,
			__kernel_version_major,
			__kernel_version_minor,
			__kernel_version_lower,
			__kernel_version_suffix);
	char version_string[256];
	snprintf(version_string, 255, "%s %s %s",
			__kernel_version_codename,
			__kernel_build_date,
			__kernel_build_time);
	strcpy(name->sysname,  __kernel_name);
	strcpy(name->nodename, hostname);
	strcpy(name->release,  version_number);
	strcpy(name->version,  version_string);
	strcpy(name->machine,  __kernel_arch);
	strcpy(name->domainname, ""); /* TODO */
	return 0;
}

static long sys_chdir(char * newdir) {
	PTR_VALIDATE(newdir);
	char * path = canonicalize_path(this_core->current_process->wd_name, newdir);
	fs_node_t * chd = kopen(path, 0);
	if (chd) {
		if ((chd->flags & FS_DIRECTORY) == 0) {
			close_fs(chd);
			return -ENOTDIR;
		}
		if (!has_permission(chd, 01)) {
			close_fs(chd);
			return -EACCES;
		}
		close_fs(chd);
		free(this_core->current_process->wd_name);
		this_core->current_process->wd_name = malloc(strlen(path) + 1);
		memcpy(this_core->current_process->wd_name, path, strlen(path) + 1);
		return 0;
	} else {
		return -ENOENT;
	}
}

static long sys_getcwd(char * buf, size_t size) {
	if (buf) {
		PTR_VALIDATE(buf);
		size_t len = strlen(this_core->current_process->wd_name) + 1;
		return (long)memcpy(buf, this_core->current_process->wd_name, size < len ? size : len);
	}
	return 0;
}

static long sys_dup2(int old, int new) {
	return process_move_fd((process_t *)this_core->current_process, old, new);
}

static long sys_sethostname(char * new_hostname) {
	if (this_core->current_process->user == USER_ROOT_UID) {
		PTR_VALIDATE(new_hostname);
		size_t len = strlen(new_hostname) + 1;
		if (len > 256) {
			return -ENAMETOOLONG;
		}
		hostname_len = len;
		memcpy(hostname, new_hostname, hostname_len);
		return 0;
	} else {
		return -EPERM;
	}
}

static long sys_gethostname(char * buffer) {
	PTR_VALIDATE(buffer);
	memcpy(buffer, hostname, hostname_len);
	return hostname_len;
}

static long sys_mount(char * arg, char * mountpoint, char * type, unsigned long flags, void * data) {
	/* TODO: Make use of flags and data from mount command. */
	(void)flags;
	(void)data;

	if (this_core->current_process->user != USER_ROOT_UID) {
		return -EPERM;
	}

	if (PTR_INRANGE(arg) && PTR_INRANGE(mountpoint) && PTR_INRANGE(type)) {
		return vfs_mount_type(type, arg, mountpoint);
	}

	return -EFAULT;
}

static long sys_umask(long mode) {
	this_core->current_process->mask = mode & 0777;
	return 0;
}

static long sys_unlink(char * file) {
	PTR_VALIDATE(file);
	return unlink_fs(file);
}

static long sys_execve(const char * filename, char *const argv[], char *const envp[]) {
	PTR_VALIDATE(filename);
	PTR_VALIDATE(argv);
	PTR_VALIDATE(envp);

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

	char **argv_ = malloc(sizeof(char*) * (argc + 1));
	for (int j = 0; j < argc; ++j) {
		argv_[j] = malloc(strlen(argv[j]) + 1);
		memcpy(argv_[j], argv[j], strlen(argv[j]) + 1);
	}
	argv_[argc] = 0;
	char ** envp_;
	if (envp && envc) {
		envp_ = malloc(sizeof(char*) * (envc + 1));
		for (int j = 0; j < envc; ++j) {
			envp_[j] = malloc(strlen(envp[j]) + 1);
			memcpy(envp_[j], envp[j], strlen(envp[j]) + 1);
		}
		envp_[envc] = 0;
	} else {
		envp_ = malloc(sizeof(char*));
		envp_[0] = NULL;
	}

	// shm_release_all
	this_core->current_process->cmdline = argv_;
	return exec(filename, argc, argv_, envp_, 0);
}

static long sys_fork(void) {
	return fork();
}

static long sys_clone(uintptr_t new_stack, uintptr_t thread_func, uintptr_t arg) {
	if (!new_stack || !PTR_INRANGE(new_stack)) return -EINVAL;
	if (!thread_func || !PTR_INRANGE(thread_func)) return -EINVAL;
	return (int)clone(new_stack, thread_func, arg);
}

static long sys_waitpid(int pid, int * status, int options) {
	if (status && !PTR_INRANGE(status)) return -EINVAL;
	return waitpid(pid, status, options);
}

static long sys_yield(void) {
	switch_task(1);
	return 1;
}

static long sys_sleepabs(unsigned long seconds, unsigned long subseconds) {
	/* Mark us as asleep until <some time period> */
	sleep_until((process_t *)this_core->current_process, seconds, subseconds);

	/* Switch without adding us to the queue */
	//printf("process %p (pid=%d) entering sleep until %ld.%06ld\n", current_process, current_process->id, seconds, subseconds);
	switch_task(0);

	unsigned long timer_ticks = 0, timer_subticks = 0;
	relative_time(0,0,&timer_ticks,&timer_subticks);
	//printf("process %p (pid=%d) resumed from sleep at %ld.%06ld\n", current_process, current_process->id, timer_ticks, timer_subticks);

	if (seconds > timer_ticks || (seconds == timer_ticks && subseconds >= timer_subticks)) {
		return 1;
	} else {
		return 0;
	}
}

static long sys_sleep(unsigned long seconds, unsigned long subseconds) {
	unsigned long s, ss;
	relative_time(seconds, subseconds * 10000, &s, &ss);
	return sys_sleepabs(s, ss);
}

static long sys_pipe(int pipes[2]) {
	if (pipes && !PTR_INRANGE(pipes)) {
		return -EFAULT;
	}

	fs_node_t * outpipes[2];

	make_unix_pipe(outpipes);

	open_fs(outpipes[0], 0);
	open_fs(outpipes[1], 0);

	pipes[0] = process_append_fd((process_t *)this_core->current_process, outpipes[0]);
	pipes[1] = process_append_fd((process_t *)this_core->current_process, outpipes[1]);
	FD_MODE(pipes[0]) = 03;
	FD_MODE(pipes[1]) = 03;
	return 0;
}

static long sys_signal(long signum, uintptr_t handler) {
	if (signum > NUMSIGNALS) {
		return -EINVAL;
	}
	uintptr_t old = this_core->current_process->signals[signum];
	this_core->current_process->signals[signum] = handler;
	return old;
}

static long sys_fswait(int c, int fds[]) {
	PTR_VALIDATE(fds);
	for (int i = 0; i < c; ++i) {
		if (!FD_CHECK(fds[i])) return -EBADF;
	}
	fs_node_t ** nodes = malloc(sizeof(fs_node_t *)*(c+1));
	for (int i = 0; i < c; ++i) {
		nodes[i] = FD_ENTRY(fds[i]);
	}
	nodes[c] = NULL;

	int result = process_wait_nodes((process_t *)this_core->current_process, nodes, -1);
	free(nodes);
	return result;
}

static long sys_fswait_timeout(int c, int fds[], int timeout) {
	PTR_VALIDATE(fds);
	for (int i = 0; i < c; ++i) {
		if (!FD_CHECK(fds[i])) return -EBADF;
	}
	fs_node_t ** nodes = malloc(sizeof(fs_node_t *)*(c+1));
	for (int i = 0; i < c; ++i) {
		nodes[i] = FD_ENTRY(fds[i]);
	}
	nodes[c] = NULL;

	int result = process_wait_nodes((process_t *)this_core->current_process, nodes, timeout);
	free(nodes);
	return result;
}

static long sys_fswait_multi(int c, int fds[], int timeout, int out[]) {
	PTR_VALIDATE(fds);
	PTR_VALIDATE(out);
	int has_match = -1;
	for (int i = 0; i < c; ++i) {
		if (!FD_CHECK(fds[i])) {
			return -EBADF;
		}
		if (selectcheck_fs(FD_ENTRY(fds[i])) == 0) {
			out[i] = 1;
			has_match = (has_match == -1) ? i : has_match;
		} else {
			out[i] = 0;
		}
	}

	/* Already found a match, return immediately with the first match */
	if (has_match != -1) return has_match;

	int result = sys_fswait_timeout(c, fds, timeout);
	if (result >= 0) out[result] = 1;
	return result;
}

static long sys_shm_obtain(char * path, size_t * size) {
	PTR_VALIDATE(path);
	PTR_VALIDATE(size);
	return (long)shm_obtain(path, size);
}

static long sys_shm_release(char * path) {
	PTR_VALIDATE(path);
	return shm_release(path);
}

static long sys_openpty(int * master, int * slave, char * name, void * _ign0, void * size) {
	/* We require a place to put these when we are done. */
	if (!master || !slave) return -EINVAL;
	if (master && !PTR_INRANGE(master)) return -EINVAL;
	if (slave && !PTR_INRANGE(slave)) return -EINVAL;
	if (size && !PTR_INRANGE(size)) return -EINVAL;

	/* Create a new pseudo terminal */
	fs_node_t * fs_master;
	fs_node_t * fs_slave;

	pty_create(size, &fs_master, &fs_slave);

	/* Append the master and slave to the calling process */
	*master = process_append_fd((process_t *)this_core->current_process, fs_master);
	*slave  = process_append_fd((process_t *)this_core->current_process, fs_slave);

	FD_MODE(*master) = 03;
	FD_MODE(*slave) = 03;

	open_fs(fs_master, 0);
	open_fs(fs_slave, 0);

	/* Return success */
	return 0;
}

static long sys_kill(pid_t process, int signal) {
	if (process < -1) {
		return group_send_signal(-process, signal, 0);
	} else if (process == 0) {
		return group_send_signal(this_core->current_process->job, signal, 0);
	} else {
		return send_signal(process, signal, 0);
	}
}

static long sys_reboot(void) {
	if (this_core->current_process->user != USER_ROOT_UID) {
		return -EPERM;
	}

	return arch_reboot();
}

extern long net_socket();
extern long net_setsockopt();
extern long net_bind();
extern long net_accept();
extern long net_listen();
extern long net_connect();
extern long net_getsockopt();
extern long net_recv();
extern long net_send();
extern long net_shutdown();

static long (*syscalls[])() = {
	/* System Call Table */
	[SYS_EXT]          = sys_exit,
	[SYS_GETEUID]      = sys_geteuid,
	[SYS_OPEN]         = sys_open,
	[SYS_READ]         = sys_read,
	[SYS_WRITE]        = sys_write,
	[SYS_CLOSE]        = sys_close,
	[SYS_GETTIMEOFDAY] = sys_gettimeofday,
	[SYS_GETPID]       = sys_getpid,
	[SYS_SBRK]         = sys_sbrk,
	[SYS_UNAME]        = sys_uname,
	[SYS_SEEK]         = sys_seek,
	[SYS_STAT]         = sys_stat,
	[SYS_GETUID]       = sys_getuid,
	[SYS_SETUID]       = sys_setuid,
	[SYS_READDIR]      = sys_readdir,
	[SYS_CHDIR]        = sys_chdir,
	[SYS_GETCWD]       = sys_getcwd,
	[SYS_SETHOSTNAME]  = sys_sethostname,
	[SYS_GETHOSTNAME]  = sys_gethostname,
	[SYS_MKDIR]        = sys_mkdir,
	[SYS_GETTID]       = sys_gettid,
	[SYS_SYSFUNC]      = sys_sysfunc,
	[SYS_IOCTL]        = sys_ioctl,
	[SYS_ACCESS]       = sys_access,
	[SYS_STATF]        = sys_statf,
	[SYS_CHMOD]        = sys_chmod,
	[SYS_UMASK]        = sys_umask,
	[SYS_UNLINK]       = sys_unlink,
	[SYS_MOUNT]        = sys_mount,
	[SYS_SYMLINK]      = sys_symlink,
	[SYS_READLINK]     = sys_readlink,
	[SYS_LSTAT]        = sys_lstat,
	[SYS_CHOWN]        = sys_chown,
	[SYS_SETSID]       = sys_setsid,
	[SYS_SETPGID]      = sys_setpgid,
	[SYS_GETPGID]      = sys_getpgid,
	[SYS_DUP2]         = sys_dup2,
	[SYS_EXECVE]       = sys_execve,
	[SYS_FORK]         = sys_fork,
	[SYS_WAITPID]      = sys_waitpid,
	[SYS_YIELD]        = sys_yield,
	[SYS_SLEEPABS]     = sys_sleepabs,
	[SYS_SLEEP]        = sys_sleep,
	[SYS_PIPE]         = sys_pipe,
	[SYS_FSWAIT]       = sys_fswait,
	[SYS_FSWAIT2]      = sys_fswait_timeout,
	[SYS_FSWAIT3]      = sys_fswait_multi,
	[SYS_CLONE]        = sys_clone,
	[SYS_OPENPTY]      = sys_openpty,
	[SYS_SHM_OBTAIN]   = sys_shm_obtain,
	[SYS_SHM_RELEASE]  = sys_shm_release,
	[SYS_SIGNAL]       = sys_signal,
	[SYS_KILL]         = sys_kill,
	[SYS_REBOOT]       = sys_reboot,

	[SYS_SOCKET]       = net_socket,
	[SYS_SETSOCKOPT]   = net_setsockopt,
	[SYS_BIND]         = net_bind,
	[SYS_ACCEPT]       = net_accept,
	[SYS_LISTEN]       = net_listen,
	[SYS_CONNECT]      = net_connect,
	[SYS_GETSOCKOPT]   = net_getsockopt,
	[SYS_RECV]         = net_recv,
	[SYS_SEND]         = net_send,
	[SYS_SHUTDOWN]     = net_shutdown,
};

static long num_syscalls = sizeof(syscalls) / sizeof(*syscalls);
typedef long (*scall_func)();

void syscall_handler(struct regs * r) {

	if (arch_syscall_number(r) >= num_syscalls) {
		arch_syscall_return(r, -EINVAL);
		return;
	}

	scall_func func = syscalls[arch_syscall_number(r)];
	this_core->current_process->syscall_registers = r;
	arch_syscall_return(r, func(
		arch_syscall_arg0(r), arch_syscall_arg1(r), arch_syscall_arg2(r),
		arch_syscall_arg3(r), arch_syscall_arg4(r)));
}
