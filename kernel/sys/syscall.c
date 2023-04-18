/**
 * @file kernel/sys/syscall.c
 * @brief System call handlers.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2011-2021 K. Lange
 */
#include <stdint.h>
#include <errno.h>
#include <sys/sysfunc.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/ptrace.h>
#include <sys/signal.h>
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
#include <kernel/ptrace.h>
#include <kernel/net/netif.h>

static char   hostname[256];
static size_t hostname_len = 0;

int ptr_validate(void * ptr, const char * syscall) {
	if (ptr) {
		if (!PTR_INRANGE(ptr)) {
			send_signal(this_core->current_process->id, SIGSEGV, 1);
			return 1;
		}
		if (!mmu_validate_user_pointer(ptr,1,0)) return 1;
	}
	return 0;
}

#define PTRCHECK(addr,size,flags) do { if (!mmu_validate_user_pointer(addr,size,flags)) return -EFAULT; } while (0)

long sys_sbrk(ssize_t size) {
	if (size & 0xFFF) return -EINVAL;
	volatile process_t * volatile proc = this_core->current_process;
	if (proc->group != 0) {
		proc = process_from_pid(proc->group);
	}
	if (!proc) return -EINVAL;
	spin_lock(proc->image.lock);
	uintptr_t out = proc->image.heap;
	for (uintptr_t i = out; i < out + size; i += 0x1000) {
		union PML * page = mmu_get_page(i, MMU_GET_MAKE);
		if (page->bits.page != 0) {
			printf("odd, %#zx is already allocated?\n", i);
		}
		mmu_frame_allocate(page, MMU_FLAG_WRITABLE);
	}
	proc->image.heap += size;
	spin_unlock(proc->image.lock);
	return (long)out;
}

extern int elf_module(char ** args);

long sys_sysfunc(long fn, char ** args) {
	/* FIXME: Most of these should be top-level, many are hacks/broken in Misaka */
	switch (fn) {
		case TOARU_SYS_FUNC_SYNC:
			/* FIXME: There is no sync ability in the VFS at the moment.
			 * XXX: Should this just be an ioctl on individual devices?
			 *      Or possibly even an ioctl we can send to arbitrary files? */
			printf("sync: not implemented\n");
			return -EINVAL;

		case TOARU_SYS_FUNC_LOGHERE:
			/* FIXME: The entire kernel logging system needs to be revamped as
			 *        Misaka switched everything to raw printfs, and then also
			 *        removed most of them for cleanliness... first task would
			 *        be to reintroduce kernel fprintf() to printf to fs_nodes. */
			//if (this_core->current_process->user != 0) return -EACCES;
			printf("\033[32m%s\033[0m", (char*)args);
			return -EINVAL;

		case TOARU_SYS_FUNC_KDEBUG:
			/* FIXME: The kernel debugger is completely deprecated and fully removed
			 *        in Misaka, and I'm not sure I want to add it back... */
			if (this_core->current_process->user != 0) return -EACCES;
			printf("kdebug: not implemented\n");
			return -EINVAL;

		case 42:
			#ifdef __aarch64__
			PTR_VALIDATE(&args[0]);
			PTR_VALIDATE(&args[1]);
			extern void arch_clear_icache(uintptr_t,uintptr_t);
			arch_clear_icache((uintptr_t)args[0], (uintptr_t)args[1]);
			#endif
			return 0;

		case 43: {
			extern void mmu_unmap_user(uintptr_t addr, size_t size);
			PTR_VALIDATE(&args[0]);
			PTR_VALIDATE(&args[1]);
			volatile process_t * volatile proc = this_core->current_process;
			if (proc->group != 0) proc = process_from_pid(proc->group);
			if (!proc) return -EFAULT;
			spin_lock(proc->image.lock);
			mmu_unmap_user((uintptr_t)args[0], (size_t)args[1]);
			spin_unlock(proc->image.lock);
			return 0;
		}

		case TOARU_SYS_FUNC_INSMOD:
			/* Linux has init_module as a system call? */
			if (this_core->current_process->user != 0) return -EACCES;
			PTR_VALIDATE(args);
			if (!args) return -EFAULT;
			PTR_VALIDATE(args[0]);
			if (!args[0]) return -EFAULT;
			for (char ** aa = args; *aa; ++aa) { PTR_VALIDATE(*aa); }
			return elf_module(args);

		case TOARU_SYS_FUNC_SETHEAP: {
			/* I'm not really sure how this should be done...
			 * traditional brk() would be expected to map everything in-between,
			 * but we use this to move the heap in ld.so, and we don't want
			 * the stuff in the middle to be mapped necessarily... */
			PTR_VALIDATE(args);
			if (!args) return -EFAULT;
			if (!PTR_INRANGE(args[0])) return -EFAULT;
			if (!args[0]) return -EFAULT;
			volatile process_t * volatile proc = this_core->current_process;
			if (proc->group != 0) proc = process_from_pid(proc->group);
			if (!proc) return -EFAULT;
			spin_lock(proc->image.lock);
			proc->image.heap = (uintptr_t)args[0];
			spin_unlock(proc->image.lock);
			return 0;
		}

		case TOARU_SYS_FUNC_MMAP: {
			/* FIXME: This whole thing should be removed; we need a proper mmap interface,
			 *        preferrably with all of the file mapping options, too. And it should
			 *        probably also interact with the SHM subsystem... */
			PTR_VALIDATE(args);
			if (!args) return -EFAULT;
			volatile process_t * volatile proc = this_core->current_process;
			if (proc->group != 0) proc = process_from_pid(proc->group);
			if (!proc) return -EFAULT;
			spin_lock(proc->image.lock);
			/* Align inputs */
			uintptr_t start = ((uintptr_t)args[0]) & 0xFFFFffffFFFFf000UL;
			uintptr_t end   = ((uintptr_t)args[0] + (size_t)args[1] + 0xFFF) & 0xFFFFffffFFFFf000UL;
			if (!PTR_INRANGE(start)) return -EFAULT;
			if (!PTR_INRANGE(end)) return -EFAULT;
			for (uintptr_t i = start; i < end; i += 0x1000) {
				union PML * page = mmu_get_page(i, MMU_GET_MAKE);
				mmu_frame_allocate(page, MMU_FLAG_WRITABLE);
			}
			spin_unlock(proc->image.lock);
			return 0;
		}

		case TOARU_SYS_FUNC_THREADNAME: {
			/* This should probably be moved to a new system call. */
			int count = 0;
			char **arg = args;
			PTR_VALIDATE(args);
			if (!args) return -EFAULT;
			while (*arg) {
				PTR_VALIDATE(*arg);
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

		case TOARU_SYS_FUNC_SETGSBASE:
			/* This should be a new system call; see what Linux, et al., call it. */
			PTR_VALIDATE(args);
			if (!args) return -EFAULT;
			PTR_VALIDATE(args[0]);
			this_core->current_process->thread.context.tls_base = (uintptr_t)args[0];
			arch_set_tls_base(this_core->current_process->thread.context.tls_base);
			return 0;

		case TOARU_SYS_FUNC_NPROC:
			return processor_count;

		default:
			printf("Bad system function: %ld\n", fn);
			return -EINVAL;
	}
	return -EINVAL;
}

__attribute__((noreturn))
long sys_exit(long exitcode) {
	task_exit((exitcode & 0xFF) << 8);
	__builtin_unreachable();
}

long sys_write(int fd, char * ptr, unsigned long len) {
#if 0
	/* Enable this to force stderr output to always be printed by the kernel. */
	if (fd == 2) {
		printf_output(len,ptr);
	}
#endif
	if (FD_CHECK(fd)) {
		PTRCHECK(ptr,len,MMU_PTR_NULL);
		fs_node_t * node = FD_ENTRY(fd);
		if (!(FD_MODE(fd) & 2)) return -EACCES;
		if (len && !ptr) {
			return -EFAULT;
		}
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
	if (fn->flags & FS_SOCKET)      { flags |= _IFSOCK; }

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

long sys_stat(int fd, uintptr_t st) {
	PTR_VALIDATE(st);
	if (!st) return -EFAULT;
	if (FD_CHECK(fd)) {
		return stat_node(FD_ENTRY(fd), st);
	}
	return -EBADF;
}

long sys_statf(char * file, uintptr_t st) {
	int result;
	PTR_VALIDATE(file);
	PTR_VALIDATE(st);

	if (!file || !st) return -EFAULT;

	fs_node_t * fn = kopen(file, 0);
	result = stat_node(fn, st);
	if (fn) {
		close_fs(fn);
	}
	return result;
}

long sys_symlink(char * target, char * name) {
	PTR_VALIDATE(target);
	PTR_VALIDATE(name);
	if (!target || !name) return -EFAULT;
	return symlink_fs(target, name);
}

long sys_readlink(const char * file, char * ptr, long len) {
	PTR_VALIDATE(file);
	PTRCHECK(ptr,len,0);
	if (!file) return -EFAULT;
	fs_node_t * node = kopen((char *) file, O_PATH | O_NOFOLLOW);
	if (!node) {
		return -ENOENT;
	}
	long rv = readlink_fs(node, ptr, len);
	close_fs(node);
	return rv;
}

long sys_lstat(char * file, uintptr_t st) {
	PTR_VALIDATE(file);
	PTR_VALIDATE(st);
	if (!file || !st) return -EFAULT;
	fs_node_t * fn = kopen(file, O_PATH | O_NOFOLLOW);
	long result = stat_node(fn, st);
	if (fn) {
		close_fs(fn);
	}
	return result;
}

long sys_open(const char * file, long flags, long mode) {
	PTR_VALIDATE(file);
	if (!file) return -EFAULT;
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

long sys_close(int fd) {
	if (FD_CHECK(fd)) {
		close_fs(FD_ENTRY(fd));
		FD_ENTRY(fd) = NULL;
		return 0;
	}
	return -EBADF;
}

long sys_seek(int fd, long offset, long whence) {
	if (FD_CHECK(fd)) {
		if ((FD_ENTRY(fd)->flags & FS_PIPE) || (FD_ENTRY(fd)->flags & FS_CHARDEVICE) || (FD_ENTRY(fd)->flags & FS_SOCKET)) return -ESPIPE;
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

long sys_read(int fd, char * ptr, unsigned long len) {
	if (FD_CHECK(fd)) {
		PTRCHECK(ptr,len,MMU_PTR_NULL|MMU_PTR_WRITE);
		if (len && !ptr) {
			return -EFAULT;
		}

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

long sys_ioctl(int fd, unsigned long request, void * argp) {
	if (FD_CHECK(fd)) {
		PTR_VALIDATE(argp);
		return ioctl_fs(FD_ENTRY(fd), request, argp);
	}
	return -EBADF;
}

long sys_readdir(int fd, long index, struct dirent * entry) {
	if (FD_CHECK(fd)) {
		PTR_VALIDATE(entry);
		if (!entry) return -EFAULT;
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

long sys_mkdir(char * path, uint64_t mode) {
	PTR_VALIDATE(path);
	if (!path) return -EFAULT;
	return mkdir_fs(path, mode);
}

long sys_access(const char * file, long flags) {
	PTR_VALIDATE(file);
	if (!file) return -EFAULT;
	fs_node_t * node = kopen((char *)file, 0);
	if (!node) return -ENOENT;
	close_fs(node);
	return 0;
}

long sys_chmod(char * file, long mode) {
	PTR_VALIDATE(file);
	if (!file) return -EFAULT;
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

static int current_group_matches(gid_t gid) {
	if (gid == this_core->current_process->user_group) return 1;
	for (int i = 0; i < this_core->current_process->supplementary_group_count; ++i) {
		if (gid == this_core->current_process->supplementary_group_list[i]) return 1;
	}
	return 0;
}

long sys_chown(char * file, uid_t uid, uid_t gid) {
	PTR_VALIDATE(file);
	if (!file) return -EFAULT;
	fs_node_t * fn = kopen(file, 0);
	if (fn) {

		/* Only a privileged user can change the owner of a file. */
		if (this_core->current_process->user != USER_ROOT_UID && uid != -1) {
			goto _access;
		}

		if (this_core->current_process->user != USER_ROOT_UID && gid != -1) {
			/* The owner of a file... */
			if (this_core->current_process->user != fn->uid) {
				goto _access;
			}

			/* May change the group of the file to one that the owner is a member of... */
			if (!current_group_matches(gid)) {
				goto _access;
			}
		}

		if ((uid != -1 || gid != -1) && (fn->mask & 0x800)) {
			/* Whenever the owner or group of a setuid executable is changed, it
			 * loses the setuid bit. */
			 chmod_fs(fn, fn->mask & (~0x800));
		}

		long result = chown_fs(fn, uid, gid);
		close_fs(fn);
		return result;
	} else {
		return -ENOENT;
	}

_access:
	close_fs(fn);
	return -EACCES;
}

long sys_gettimeofday(struct timeval * tv, void * tz) {
	PTR_VALIDATE(tv);
	PTR_VALIDATE(tz);
	if (!tv) return -EFAULT;
	return gettimeofday(tv, tz);
}

long sys_settimeofday(struct timeval * tv, void * tz) {
	extern int settimeofday(struct timeval * t, void *z);
	if (this_core->current_process->user != USER_ROOT_UID) return -EPERM;
	PTR_VALIDATE(tv);
	PTR_VALIDATE(tz);
	return settimeofday(tv,tz);
}

long sys_getuid(void) {
	return (long)this_core->current_process->real_user;
}

long sys_geteuid(void) {
	return (long)this_core->current_process->user;
}

long sys_setuid(uid_t new_uid) {
	if (this_core->current_process->user == USER_ROOT_UID) {
		this_core->current_process->user = new_uid;
		this_core->current_process->real_user = new_uid;
		return 0;
	}
	return -EPERM;
}

long sys_getgid(void) {
	return (long)this_core->current_process->real_user_group;
}

long sys_getegid(void) {
	return (long)this_core->current_process->user_group;
}

long sys_setgid(gid_t new_gid) {
	if (this_core->current_process->user == USER_ROOT_UID) {
		this_core->current_process->user_group = new_gid;
		this_core->current_process->real_user_group = new_gid;
		return 0;
	}
	return -EPERM;
}

long sys_getgroups(int size, gid_t list[]) {
	if (size == 0) {
		return this_core->current_process->supplementary_group_count;
	} else if (size < this_core->current_process->supplementary_group_count) {
		return -EINVAL;
	} else {
		PTR_VALIDATE(list);
		if (!list) return -EFAULT;
		for (int i = 0; i < this_core->current_process->supplementary_group_count; ++i) {
			PTR_VALIDATE(list + i);
			list[i] = this_core->current_process->supplementary_group_list[i];
		}
		return this_core->current_process->supplementary_group_count;
	}
}

long sys_setgroups(int size, const gid_t list[]) {
	if (this_core->current_process->user != USER_ROOT_UID) return -EPERM;
	if (size < 0) return -EINVAL;
	if (size > 32) return -EINVAL; /* Arbitrary decision */

	/* Free the current set. */
	if (this_core->current_process->supplementary_group_count) {
		free(this_core->current_process->supplementary_group_list);
		this_core->current_process->supplementary_group_list = NULL;
	}

	this_core->current_process->supplementary_group_count = size;
	if (size == 0) return 0;

	this_core->current_process->supplementary_group_list = malloc(sizeof(gid_t) * size);

	PTR_VALIDATE(list);
	if (!list) return -EFAULT;

	for (int i = 0; i < size; ++i) {
		PTR_VALIDATE(list + i);
		this_core->current_process->supplementary_group_list[i] = list[i];
	}

	return 0;
}


long sys_getpid(void) {
	/* The user actually wants the pid of the originating thread (which can be us). */
	return this_core->current_process->group ? (long)this_core->current_process->group : (long)this_core->current_process->id;
}

long sys_gettid(void) {
	return (long)this_core->current_process->id;
}

long sys_setsid(void) {
	if (this_core->current_process->job == this_core->current_process->group) {
		return -EPERM;
	}
	this_core->current_process->session = this_core->current_process->group;
	this_core->current_process->job = this_core->current_process->group;
	return this_core->current_process->session;
}

long sys_setpgid(pid_t pid, pid_t pgid) {
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

long sys_getpgid(pid_t pid) {
	process_t * proc;
	if (pid == 0) {
		proc = (process_t*)this_core->current_process;
	} else {
		proc = process_from_pid(pid);
	}

	if (!proc) {
		return -ESRCH;
	}

	return proc->job;
}

long sys_uname(struct utsname * name) {
	PTR_VALIDATE(name);
	if (!name) return -EFAULT;
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

long sys_chdir(char * newdir) {
	PTR_VALIDATE(newdir);
	if (!newdir) return -EFAULT;
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

long sys_getcwd(char * buf, size_t size) {
	if (buf) {
		PTR_VALIDATE(buf);
		size_t len = strlen(this_core->current_process->wd_name) + 1;
		return (long)memcpy(buf, this_core->current_process->wd_name, size < len ? size : len);
	}
	return 0;
}

long sys_dup2(int old, int new) {
	return process_move_fd((process_t *)this_core->current_process, old, new);
}

long sys_sethostname(char * new_hostname) {
	if (this_core->current_process->user == USER_ROOT_UID) {
		PTR_VALIDATE(new_hostname);
		if (!new_hostname) return -EFAULT;
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

long sys_gethostname(char * buffer) {
	PTR_VALIDATE(buffer);
	if (!buffer) return -EFAULT;
	memcpy(buffer, hostname, hostname_len);
	return hostname_len;
}

long sys_mount(char * arg, char * mountpoint, char * type, unsigned long flags, void * data) {
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

long sys_umask(long mode) {
	this_core->current_process->mask = mode & 0777;
	return 0;
}

long sys_unlink(char * file) {
	PTR_VALIDATE(file);
	if (!file) return -EFAULT;
	return unlink_fs(file);
}

long sys_execve(const char * filename, char *const argv[], char *const envp[]) {
	PTR_VALIDATE(filename);
	PTR_VALIDATE(argv);
	PTR_VALIDATE(envp);

	if (!filename || !argv) return -EFAULT;

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

	/**
	 * FIXME: For legacy reasons, we're just going to close everything >2 for now,
	 *        but we should really implement proper CLOEXEC semantics...
	 */
	for (unsigned int i = 3; i < this_core->current_process->fds->length; ++i) {
		if (this_core->current_process->fds->entries[i]) {
			close_fs(this_core->current_process->fds->entries[i]);
			this_core->current_process->fds->entries[i] = NULL;
		}
	}

	shm_release_all((process_t *)this_core->current_process);

	this_core->current_process->cmdline = argv_;
	return exec(filename, argc, argv_, envp_, 0);
}

long sys_fork(void) {
	return fork();
}

long sys_clone(uintptr_t new_stack, uintptr_t thread_func, uintptr_t arg) {
	if (!new_stack || !PTR_INRANGE(new_stack)) return -EINVAL;
	if (!thread_func || !PTR_INRANGE(thread_func)) return -EINVAL;
	return (int)clone(new_stack, thread_func, arg);
}

long sys_waitpid(int pid, int * status, int options) {
	if (status && !PTR_INRANGE(status)) return -EINVAL;
	return waitpid(pid, status, options);
}

long sys_yield(void) {
	switch_task(1);
	return 1;
}

long sys_sleepabs(unsigned long seconds, unsigned long subseconds) {
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

long sys_sleep(unsigned long seconds, unsigned long subseconds) {
	unsigned long s, ss;
	relative_time(seconds, subseconds * 10000, &s, &ss);
	return sys_sleepabs(s, ss);
}

long sys_pipe(int pipes[2]) {
	if (!pipes || !PTR_INRANGE(pipes)) {
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

long sys_signal(long signum, uintptr_t handler) {
	if (signum >= NUMSIGNALS || signum < 0) return -EINVAL;
	if (signum == SIGKILL || signum == SIGSTOP) return -EINVAL;
	uintptr_t old = this_core->current_process->signals[signum].handler;
	this_core->current_process->signals[signum].handler = handler;
	this_core->current_process->signals[signum].flags = SA_RESTART;
	return old;
}

long sys_sigaction(int signum, struct sigaction *act, struct sigaction *oldact) {
	if (act) PTRCHECK(act,sizeof(struct sigaction),0);
	if (oldact) PTRCHECK(oldact,sizeof(struct sigaction),MMU_PTR_WRITE);

	if (signum >= NUMSIGNALS || signum < 0) return -EINVAL;
	if (signum == SIGKILL || signum == SIGSTOP) return -EINVAL;

	if (oldact) {
		oldact->sa_handler = (_sig_func_ptr)this_core->current_process->signals[signum].handler;
		oldact->sa_mask    = this_core->current_process->signals[signum].mask;
		oldact->sa_flags   = this_core->current_process->signals[signum].flags;
	}

	if (act) {
		this_core->current_process->signals[signum].handler = (uintptr_t)act->sa_handler;
		this_core->current_process->signals[signum].mask    = act->sa_mask;
		this_core->current_process->signals[signum].flags   = act->sa_flags;
	}

	return 0;
}

long sys_sigpending(sigset_t * set) {
	PTRCHECK(set,sizeof(sigset_t),MMU_PTR_WRITE);
	*set = this_core->current_process->pending_signals;
	return 0;
}

long sys_sigprocmask(int how, sigset_t *restrict set, sigset_t * restrict oset) {
	if (oset) {
		PTRCHECK(oset,sizeof(sigset_t),MMU_PTR_WRITE);
		*oset = this_core->current_process->blocked_signals;
	}

	if (set) {
		PTRCHECK(set,sizeof(sigset_t),0);
		switch (how) {
			case SIG_SETMASK:
				this_core->current_process->blocked_signals = *set;
				break;
			case SIG_BLOCK:
				this_core->current_process->blocked_signals |= *set;
				break;
			case SIG_UNBLOCK:
				this_core->current_process->blocked_signals &= ~*set;
				break;
			default:
				return -EINVAL;
		}
	}

	return 0;
}

long sys_sigsuspend_cur(void) {
	switch_task(0);
	return -EINTR;
}

long sys_sigwait(sigset_t * set, int * sig) {
	PTRCHECK(set,sizeof(sigset_t),0);
	PTRCHECK(sig,sizeof(int),MMU_PTR_WRITE);

	/* Silently ignore attempts to wait on KILL or STOP */
	sigset_t awaited = *set & ~((1 << SIGKILL) | (1 << SIGSTOP));

	/* Don't let processes wait on unblocked signals */
	if (awaited & ~this_core->current_process->blocked_signals) return -EINVAL;

	return signal_await(awaited, sig);
}

long sys_fswait(int c, int fds[]) {
	PTR_VALIDATE(fds);
	if (!fds || c < 0) return -EFAULT;
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

long sys_fswait_timeout(int c, int fds[], int timeout) {
	PTR_VALIDATE(fds);
	if (!fds || c < 0) return -EFAULT;
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

long sys_fswait_multi(int c, int fds[], int timeout, int out[]) {
	PTR_VALIDATE(fds);
	PTR_VALIDATE(out);
	if (!fds || !out || c < 0) return -EFAULT;
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
	if (result >= 0 && result < c) out[result] = 1;
	return result;
}

long sys_shm_obtain(char * path, size_t * size) {
	PTR_VALIDATE(path);
	PTR_VALIDATE(size);
	if (!path || !size) return -EFAULT;
	return (long)shm_obtain(path, size);
}

long sys_shm_release(char * path) {
	PTR_VALIDATE(path);
	if (!path) return -EFAULT;
	return shm_release(path);
}

long sys_openpty(int * master, int * slave, char * name, void * _ign0, void * size) {
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

long sys_kill(pid_t process, int signal) {
	if (process < -1) {
		return group_send_signal(-process, signal, 0);
	} else if (process == 0) {
		return group_send_signal(this_core->current_process->job, signal, 0);
	} else {
		return send_signal(process, signal, 0);
	}
}

long sys_reboot(void) {
	if (this_core->current_process->user != USER_ROOT_UID) {
		return -EPERM;
	}

	return arch_reboot();
}

long sys_times(struct tms *buf) {
	if (buf) {
		PTR_VALIDATE(buf);

		buf->tms_utime  = this_core->current_process->time_total        / arch_cpu_mhz();
		buf->tms_stime  = this_core->current_process->time_sys          / arch_cpu_mhz();
		buf->tms_cutime = this_core->current_process->time_children     / arch_cpu_mhz();
		buf->tms_cstime = this_core->current_process->time_sys_children / arch_cpu_mhz();
	}

	return arch_perf_timer() / arch_cpu_mhz();
}

extern long ptrace_handle(long,pid_t,void*,void*);

typedef long (*scall_func)(long,long,long,long,long);

static scall_func syscalls[] = {
	/* System Call Table */
	[SYS_EXT]          = (scall_func)(uintptr_t)sys_exit,
	[SYS_GETEUID]      = (scall_func)(uintptr_t)sys_geteuid,
	[SYS_OPEN]         = (scall_func)(uintptr_t)sys_open,
	[SYS_READ]         = (scall_func)(uintptr_t)sys_read,
	[SYS_WRITE]        = (scall_func)(uintptr_t)sys_write,
	[SYS_CLOSE]        = (scall_func)(uintptr_t)sys_close,
	[SYS_GETTIMEOFDAY] = (scall_func)(uintptr_t)sys_gettimeofday,
	[SYS_GETPID]       = (scall_func)(uintptr_t)sys_getpid,
	[SYS_SBRK]         = (scall_func)(uintptr_t)sys_sbrk,
	[SYS_UNAME]        = (scall_func)(uintptr_t)sys_uname,
	[SYS_SEEK]         = (scall_func)(uintptr_t)sys_seek,
	[SYS_STAT]         = (scall_func)(uintptr_t)sys_stat,
	[SYS_GETUID]       = (scall_func)(uintptr_t)sys_getuid,
	[SYS_SETUID]       = (scall_func)(uintptr_t)sys_setuid,
	[SYS_READDIR]      = (scall_func)(uintptr_t)sys_readdir,
	[SYS_CHDIR]        = (scall_func)(uintptr_t)sys_chdir,
	[SYS_GETCWD]       = (scall_func)(uintptr_t)sys_getcwd,
	[SYS_SETHOSTNAME]  = (scall_func)(uintptr_t)sys_sethostname,
	[SYS_GETHOSTNAME]  = (scall_func)(uintptr_t)sys_gethostname,
	[SYS_MKDIR]        = (scall_func)(uintptr_t)sys_mkdir,
	[SYS_GETTID]       = (scall_func)(uintptr_t)sys_gettid,
	[SYS_SYSFUNC]      = (scall_func)(uintptr_t)sys_sysfunc,
	[SYS_IOCTL]        = (scall_func)(uintptr_t)sys_ioctl,
	[SYS_ACCESS]       = (scall_func)(uintptr_t)sys_access,
	[SYS_STATF]        = (scall_func)(uintptr_t)sys_statf,
	[SYS_CHMOD]        = (scall_func)(uintptr_t)sys_chmod,
	[SYS_UMASK]        = (scall_func)(uintptr_t)sys_umask,
	[SYS_UNLINK]       = (scall_func)(uintptr_t)sys_unlink,
	[SYS_MOUNT]        = (scall_func)(uintptr_t)sys_mount,
	[SYS_SYMLINK]      = (scall_func)(uintptr_t)sys_symlink,
	[SYS_READLINK]     = (scall_func)(uintptr_t)sys_readlink,
	[SYS_LSTAT]        = (scall_func)(uintptr_t)sys_lstat,
	[SYS_CHOWN]        = (scall_func)(uintptr_t)sys_chown,
	[SYS_SETSID]       = (scall_func)(uintptr_t)sys_setsid,
	[SYS_SETPGID]      = (scall_func)(uintptr_t)sys_setpgid,
	[SYS_GETPGID]      = (scall_func)(uintptr_t)sys_getpgid,
	[SYS_DUP2]         = (scall_func)(uintptr_t)sys_dup2,
	[SYS_EXECVE]       = (scall_func)(uintptr_t)sys_execve,
	[SYS_FORK]         = (scall_func)(uintptr_t)sys_fork,
	[SYS_WAITPID]      = (scall_func)(uintptr_t)sys_waitpid,
	[SYS_YIELD]        = (scall_func)(uintptr_t)sys_yield,
	[SYS_SLEEPABS]     = (scall_func)(uintptr_t)sys_sleepabs,
	[SYS_SLEEP]        = (scall_func)(uintptr_t)sys_sleep,
	[SYS_PIPE]         = (scall_func)(uintptr_t)sys_pipe,
	[SYS_FSWAIT]       = (scall_func)(uintptr_t)sys_fswait,
	[SYS_FSWAIT2]      = (scall_func)(uintptr_t)sys_fswait_timeout,
	[SYS_FSWAIT3]      = (scall_func)(uintptr_t)sys_fswait_multi,
	[SYS_CLONE]        = (scall_func)(uintptr_t)sys_clone,
	[SYS_OPENPTY]      = (scall_func)(uintptr_t)sys_openpty,
	[SYS_SHM_OBTAIN]   = (scall_func)(uintptr_t)sys_shm_obtain,
	[SYS_SHM_RELEASE]  = (scall_func)(uintptr_t)sys_shm_release,
	[SYS_SIGNAL]       = (scall_func)(uintptr_t)sys_signal,
	[SYS_KILL]         = (scall_func)(uintptr_t)sys_kill,
	[SYS_REBOOT]       = (scall_func)(uintptr_t)sys_reboot,
	[SYS_GETGID]       = (scall_func)(uintptr_t)sys_getgid,
	[SYS_GETEGID]      = (scall_func)(uintptr_t)sys_getegid,
	[SYS_SETGID]       = (scall_func)(uintptr_t)sys_setgid,
	[SYS_GETGROUPS]    = (scall_func)(uintptr_t)sys_getgroups,
	[SYS_SETGROUPS]    = (scall_func)(uintptr_t)sys_setgroups,
	[SYS_TIMES]        = (scall_func)(uintptr_t)sys_times,
	[SYS_PTRACE]       = (scall_func)(uintptr_t)ptrace_handle,
	[SYS_SETTIMEOFDAY] = (scall_func)(uintptr_t)sys_settimeofday,
	[SYS_SIGACTION]    = (scall_func)(uintptr_t)sys_sigaction,
	[SYS_SIGPENDING]   = (scall_func)(uintptr_t)sys_sigpending,
	[SYS_SIGPROCMASK]  = (scall_func)(uintptr_t)sys_sigprocmask,
	[SYS_SIGSUSPEND]   = (scall_func)(uintptr_t)sys_sigsuspend_cur,
	[SYS_SIGWAIT]      = (scall_func)(uintptr_t)sys_sigwait,

	[SYS_SOCKET]       = (scall_func)(uintptr_t)net_socket,
	[SYS_SETSOCKOPT]   = (scall_func)(uintptr_t)net_setsockopt,
	[SYS_BIND]         = (scall_func)(uintptr_t)net_bind,
	[SYS_ACCEPT]       = (scall_func)(uintptr_t)net_accept,
	[SYS_LISTEN]       = (scall_func)(uintptr_t)net_listen,
	[SYS_CONNECT]      = (scall_func)(uintptr_t)net_connect,
	[SYS_GETSOCKOPT]   = (scall_func)(uintptr_t)net_getsockopt,
	[SYS_RECV]         = (scall_func)(uintptr_t)net_recv,
	[SYS_SEND]         = (scall_func)(uintptr_t)net_send,
	[SYS_SHUTDOWN]     = (scall_func)(uintptr_t)net_shutdown,
	[SYS_GETSOCKNAME]  = (scall_func)(uintptr_t)net_getsockname,
	[SYS_GETPEERNAME]  = (scall_func)(uintptr_t)net_getpeername,
};

static long num_syscalls = sizeof(syscalls) / sizeof(*syscalls);

void syscall_handler(struct regs * r) {

	if (arch_syscall_number(r) >= num_syscalls) {
		arch_syscall_return(r, -EINVAL);
		return;
	}

	scall_func func = syscalls[arch_syscall_number(r)];
	this_core->current_process->syscall_registers = r;

	if (this_core->current_process->flags & PROC_FLAG_TRACE_SYSCALLS) {
		ptrace_signal(SIGTRAP, PTRACE_EVENT_SYSCALL_ENTER);
	}

	long result = func(
		arch_syscall_arg0(r), arch_syscall_arg1(r), arch_syscall_arg2(r),
		arch_syscall_arg3(r), arch_syscall_arg4(r));

	if (result == -ERESTARTSYS) {
		this_core->current_process->interrupted_system_call = arch_syscall_number(r);
	}

	arch_syscall_return(r, result);

	if (this_core->current_process->flags & PROC_FLAG_TRACE_SYSCALLS) {
		ptrace_signal(SIGTRAP, PTRACE_EVENT_SYSCALL_EXIT);
	}
}
