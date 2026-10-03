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
#include <sys/types.h>
#include <bits/errno.h>
#include <bits/timespec.h>
#include <sys/utsname.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/ptrace.h>
#include <sys/signal.h>
#include <sys/resource.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <kernel/printf.h>
#include <kernel/process.h>
#include <kernel/string.h>
#include <kernel/version.h>
#include <kernel/pipe.h>
#include <kernel/mmu.h>
#include <kernel/pty.h>
#include <kernel/spinlock.h>
#include <kernel/signal.h>
#include <kernel/time.h>
#include <kernel/syscall.h>
#include <kernel/misc.h>
#include <kernel/ptrace.h>
#include <kernel/mman.h>
#include <kernel/net/netif.h>

static char   hostname[256];
static size_t hostname_len = 0;

int ptr_validate(void * ptr, const char * syscall) {
	if (ptr && (!PTR_INRANGE(ptr) || !mmu_validate_user_pointer(ptr,1,0))) return 1;
	return 0;
}

#define PTRCHECK(addr,size,flags) do { if (!mmu_validate_user_pointer(addr,size,flags)) return -EFAULT; } while (0)

static int check_user_string(const char * str) {
	/* This should really do a copy operation that we can then discard later,
	 * but we're continuing to be lazy. This still needs to scan the whole
	 * string in case it comes from a binary that isn't mapped, and to ensure
	 * the whole string is actually valid for reading at least at call time. */
	const char * c = str;
	if (!c) return 1;
	while (1) {
		if (!mmu_validate_user_pointer(c, 1, 0)) return 1;
		do {
			if (!*c) return 0;
			c++;
		} while ((uintptr_t)c & 0xFFF);
	}
}

static struct fs_file_description * do_dirfd(int dirfd) {
	if (dirfd == AT_FDCWD) return this_core->current_process->wd;
	else if (FD_CHECK(dirfd)) return FD_FILE(dirfd);
	return NULL;
}

__attribute__((noreturn))
long sys_exit(long exitcode) {
	task_exit(((unsigned char)exitcode) << 8);
	__builtin_unreachable();
}

long sys_write(int fd, char * ptr, unsigned long len) {
	if (!FD_CHECK(fd)) return -EBADF;
	if (!(FD_MODE(fd) & PROC_FD_MODE_WRITE)) return -EBADF;
	PTRCHECK(ptr,len,MMU_PTR_NULL);

	fs_node_t * node = FD_ENTRY(fd);
	if (!len) return 0;

	off_t off = FD_OFFSET(fd);
	if (FD_MODE(fd) & PROC_FD_MODE_APPEND) {
		off = FD_OFFSET(fd) = node->length;
	}

	int64_t out = write_fs(node, off, len, (uint8_t*)ptr);
	if (out > 0) FD_OFFSET(fd) = off + out;
	return out;
}

long sys_pwrite(int fd, void * ptr, size_t count, off_t offset) {
	if (!FD_CHECK(fd)) return -EBADF;
	if (!(FD_MODE(fd) & PROC_FD_MODE_WRITE)) return -EBADF;
	if ((FD_ENTRY(fd)->type == INO_FIFO) || (FD_ENTRY(fd)->type == INO_CHR) || (FD_ENTRY(fd)->type == INO_SOCK)) return -ESPIPE;
	PTRCHECK(ptr,count,MMU_PTR_NULL);

	fs_node_t * node = FD_ENTRY(fd);
	if (!count) return 0;

	return write_fs(node, offset, count, (uint8_t*)ptr);
}

long sys_read(int fd, char * ptr, unsigned long len) {
	if (!FD_CHECK(fd)) return -EBADF;
	if (!(FD_MODE(fd) & PROC_FD_MODE_READ)) return -EBADF;
	PTRCHECK(ptr,len,MMU_PTR_NULL|MMU_PTR_WRITE);

	fs_node_t * node = FD_ENTRY(fd);
	if (!len) return 0;

	off_t off = FD_OFFSET(fd);
	int64_t out = read_fs(node, off, len, (uint8_t *)ptr);
	if (out > 0) FD_OFFSET(fd) = off + out;
	return out;
}

long sys_pread(int fd, void * ptr, size_t count, off_t offset) {
	if (!FD_CHECK(fd)) return -EBADF;
	if (!(FD_MODE(fd) & PROC_FD_MODE_READ)) return -EBADF;
	if ((FD_ENTRY(fd)->type == INO_FIFO) || (FD_ENTRY(fd)->type == INO_CHR) || (FD_ENTRY(fd)->type == INO_SOCK)) return -ESPIPE;
	PTRCHECK(ptr,count,MMU_PTR_NULL|MMU_PTR_WRITE);

	fs_node_t * node = FD_ENTRY(fd);
	if (!count) return 0;

	return read_fs(node, offset, count, (uint8_t *)ptr);
}

static long stat_node(fs_node_t * fn, struct stat * f) {
	f->st_dev   = fs_device_identifier(fn);
	f->st_ino   = fn->inode;

	uint32_t flags = 0;
	switch (fn->type) {
		case INO_REG:  flags = _IFREG; break;
		case INO_DIR:  flags = _IFDIR; break;
		case INO_CHR:  flags = _IFCHR; break;
		case INO_BLK:  flags = _IFBLK; break;
		case INO_FIFO: flags = _IFIFO; break;
		case INO_LNK:  flags = _IFLNK; break;
		case INO_SOCK: flags = _IFSOCK; break;
	}

	f->st_mode  = fn->mask | flags;
	f->st_nlink = fn->nlink;
	f->st_uid   = fn->uid;
	f->st_gid   = fn->gid;
	f->st_rdev  = 0;
	f->st_size  = fn->length;

	f->st_atim.tv_sec = fn->atime;
	f->st_atim.tv_nsec = fn->atime_nsec;
	f->st_mtim.tv_sec = fn->mtime;
	f->st_mtim.tv_nsec = fn->mtime_nsec;
	f->st_ctim.tv_sec = fn->ctime;
	f->st_ctim.tv_nsec = fn->ctime_nsec;
	f->st_blksize = 512; /* whatever */

	if (fn->ops->get_size) {
		f->st_size = fn->ops->get_size(fn);
	}

	f->st_blocks  = (f->st_size + 511) / 512;

	return 0;
}

long sys_stat(int fd, struct stat * st) {
	if (!FD_CHECK(fd)) return -EBADF;
	PTRCHECK(st,sizeof(struct stat),MMU_PTR_WRITE);
	if (!st) return -EFAULT;
	return stat_node(FD_ENTRY(fd), st);
}

long sys_fstatat(int dirfd, const char * filename, struct stat * st, int flag) {
	if (flag & ~AT_SYMLINK_NOFOLLOW) return -EINVAL;
	if (check_user_string(filename)) return -EFAULT;
	PTRCHECK(st,sizeof(struct stat),MMU_PTR_WRITE);
	if (!filename || !st) return -EFAULT;

	int flags = O_PATH;

	if (flag & AT_SYMLINK_NOFOLLOW) flags |= O_NOFOLLOW;

	int error = 0;
	struct fs_file_description * fd = do_dirfd(dirfd);
	struct fs_file_description * out = kopen_at(fd, filename, flags, 0, &error);
	if (!out) return -error;

	long result = stat_node(out->inode, st);
	fs_close_desc(out);
	return result;
}

long sys_symlinkat(const char * target, int dirfd, const char * name) {
	if (check_user_string(target) || check_user_string(name)) return -EFAULT;
	return symlink_fs_at(target, do_dirfd(dirfd), name);
}

long sys_readlinkat(int dirfd, const char * file, char * ptr, long len) {
	if (check_user_string(file)) return -EFAULT;
	PTRCHECK(ptr,len,MMU_PTR_WRITE);
	int error = 0;
	struct fs_file_description * fd = do_dirfd(dirfd);
	struct fs_file_description * out = kopen_at(fd, file, O_PATH | O_NOFOLLOW, 0, &error);
	if (!out) return -error;
	long rv = readlink_fs(out->inode, ptr, len);
	fs_close_desc(out);
	return rv;
}

static mode_t modify_mode(mode_t mode_in) {
	return mode_in & ~(this_core->current_process->process->mask & 0777);
}

long sys_openat(int dirfd, const char *filename, int flags, mode_t mode_in) {
	if (check_user_string(filename)) return -EFAULT;
	mode_t mode = modify_mode(mode_in);
	int error = 0;
	struct fs_file_description * fd = do_dirfd(dirfd);
	struct fs_file_description * out = kopen_at(fd, filename, flags, mode, &error);
	if (!out) return -error;
	uintptr_t clo_flags = 0;

	if (flags & O_CLOEXEC) clo_flags |= FD_PTR_CLOEXEC;
	if (flags & O_CLOFORK) clo_flags |= FD_PTR_CLOFORK;

	return process_append_fd((process_t*)this_core->current_process, out, clo_flags);
}

long sys_close(int fd) {
	if (!FD_CHECK(fd)) return -EBADF;
	return process_close_fd((process_t*)this_core->current_process, fd);
}

long sys_seek(int fd, long offset, long whence) {
	if (!FD_CHECK(fd)) return -EBADF;
	if ((FD_ENTRY(fd)->type == INO_FIFO) || (FD_ENTRY(fd)->type == INO_CHR) || (FD_ENTRY(fd)->type == INO_SOCK)) return -ESPIPE;

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

long sys_ioctl(int fd, unsigned long request, void * argp) {
	if (!FD_CHECK(fd)) return -EBADF;
	if (!(FD_MODE(fd) & PROC_FD_MODE__RW)) return -EBADF;
	PTR_VALIDATE(argp);

	return ioctl_fs(FD_ENTRY(fd), request, argp);
}

long sys_readdir(int fd, long index, struct dirent * entry) {
	if (!FD_CHECK(fd)) return -EBADF;
	if (!(FD_MODE(fd) & PROC_FD_MODE__RW)) return -EBADF;
	PTRCHECK(entry,sizeof(struct dirent),MMU_PTR_WRITE);

	fs_node_t * node = FD_ENTRY(fd);
	if (!(FD_MODE(fd) & PROC_FD_MODE_READ)) return -EBADF;

	return readdir_fs(node, (uint64_t)index, entry);
}

long sys_mkdirat(int dirfd, const char * path, mode_t mode_in) {
	if (check_user_string(path)) return -EFAULT;
	if (!path) return -EFAULT;
	mode_t mode = modify_mode(mode_in);

	int error = 0;
	struct fs_file_description * fd = do_dirfd(dirfd);
	struct fs_file_description * out = kopen_at(fd, path, O_DIRECTORY | O_CREAT | O_EXCL, mode, &error);
	if (!out) return -error;

	fs_close_desc(out);
	return 0;
}

long sys_faccessat(int dirfd, const char *path, int amode, int flag) {
	if (check_user_string(path)) return -EFAULT;
	if (amode < 0 || amode > 7) return -EINVAL;
	if (flag & ~(AT_EACCESS | AT_SYMLINK_NOFOLLOW)) return -EINVAL;

	int flags = O_PATH;
	if (flag & AT_SYMLINK_NOFOLLOW) flags |= O_NOFOLLOW;
	int error = 0;
	struct fs_file_description * fd = do_dirfd(dirfd);
	struct fs_file_description * out = kopen_at(fd, path, flags, 0, &error);

	if (!out) return -error;

	if (!(flag & AT_EACCESS)) amode |= 010;

	int ret = amode ? (!has_permission(out->inode, amode) ? -EACCES : 0) : 0;
	fs_close_desc(out);
	return ret;
}

static long chmod_node(fs_node_t * fn, mode_t mode) {
	if (this_core->current_process->user != 0 && this_core->current_process->user != fn->uid) return -EACCES;
	return chmod_fs(fn, mode);
}

long sys_fchmodat(int dirfd, const char * file, mode_t mode, int flag) {
	if (check_user_string(file)) return -EFAULT;
	if (flag & ~(AT_SYMLINK_NOFOLLOW)) return -EINVAL;
	int flags = O_PATH;
	if (flag & AT_SYMLINK_NOFOLLOW) flags |= O_NOFOLLOW;
	int error = 0;
	struct fs_file_description * fd = do_dirfd(dirfd);
	struct fs_file_description * out = kopen_at(fd, file, flags, 0, &error);
	if (!out) return -error;
	long ret = chmod_node(out->inode, mode);
	fs_close_desc(out);
	return ret;
}

long sys_fchmod(int fd, long mode) {
	if (!FD_CHECK(fd)) return -EBADF;
	if (!(FD_MODE(fd) & PROC_FD_MODE__RW)) return -EBADF;
	return chmod_node(FD_ENTRY(fd), mode);
}

long sys_renameat(int srcfd, const char * src, int destfd, const char * dest) {
	if (check_user_string(src) || check_user_string(dest)) return -EFAULT;

	return rename_file_fs_at(do_dirfd(srcfd), src, do_dirfd(destfd), dest);
}

static int current_group_matches(gid_t gid) {
	if (gid == this_core->current_process->user_group) return 1;
	for (int i = 0; i < this_core->current_process->supplementary_group_count; ++i) {
		if (gid == this_core->current_process->supplementary_group_list[i]) return 1;
	}
	return 0;
}

static long chown_node(fs_node_t * fn, uid_t uid, gid_t gid) {
	/* Only a privileged user can change the owner of a file. */
	if (this_core->current_process->user != USER_ROOT_UID && uid != -1 && uid != fn->uid) return -EPERM;

	if (this_core->current_process->user != USER_ROOT_UID && gid != -1) {
		/* The owner of a file... */
		if (this_core->current_process->user != fn->uid) return -EPERM;
		/* May change the group of the file to one that the owner is a member of... */
		if (!current_group_matches(gid)) return -EPERM;
	}

	if ((fn->mask & (S_IXUSR | S_IXGRP | S_IXOTH)) && (fn->mask & (S_ISUID | S_ISGID)) &&
		this_core->current_process->user != USER_ROOT_UID) {
		/* If any of the execute bits is set, and "the user does not have appropriate priveleges",
		 * clear the set-user-ID and set-group-ID bits; do this before we actually change
		 * ownership, to ensure there isn't a point where the new ownership is set but
		 * the bits haven't been cleared. */
		chmod_fs(fn, fn->mask & ~(S_ISUID | S_ISGID));
	}

	return chown_fs(fn, uid, gid);
}

long sys_fchownat(int dirfd, const char * file, uid_t uid, gid_t gid, int flag) {
	if (check_user_string(file)) return -EFAULT;
	if (flag & ~(AT_SYMLINK_NOFOLLOW)) return -EINVAL;
	int flags = O_PATH;
	if (flag & AT_SYMLINK_NOFOLLOW) flags |= O_NOFOLLOW;
	int error = 0;
	struct fs_file_description * fd = do_dirfd(dirfd);
	struct fs_file_description * out = kopen_at(fd, file, flags, 0, &error);
	if (!out) return -error;
	long ret = chown_node(out->inode, uid, gid);
	fs_close_desc(out);
	return ret;
}

long sys_fchown(int fd, uid_t uid, gid_t gid) {
	if (!FD_CHECK(fd)) return -EBADF;
	if (!(FD_MODE(fd) & PROC_FD_MODE__RW)) return -EBADF;
	return chown_node(FD_ENTRY(fd), uid, gid);
}

long sys_truncate(char * file, off_t size) {
	if (check_user_string(file)) return -EFAULT;
	if (size < 0) return -EINVAL;
	int error = 0;
	struct fs_file_description * fd = do_dirfd(AT_FDCWD);
	struct fs_file_description * out = kopen_at(fd, file, O_WRONLY, 0, &error);
	if (!out) return -error;
	long ret = truncate_fs(out->inode, size);
	fs_close_desc(out);
	return ret;
}

long sys_ftruncate(int fd, off_t size) {
	if (!FD_CHECK(fd)) return -EBADF;
	if (!(FD_MODE(fd) & PROC_FD_MODE_WRITE)) return -EBADF;
	if (size < 0) return -EINVAL;
	return truncate_fs(FD_ENTRY(fd), size);
}

static long utimens_node(fs_node_t * node, const struct timespec * access, const struct timespec * modify) {
	struct timespec _access = {0};
	struct timespec _modify = {0};

	if (access) {
		PTRCHECK(access, sizeof(struct timespec), 0);
		memcpy(&_access, access, sizeof(struct timespec));
	}

	if (modify) {
		PTRCHECK(modify, sizeof(struct timespec), 0);
		memcpy(&_modify, modify, sizeof(struct timespec));
	}

	return utimens_fs(node, _access, _modify);
}

long sys_utimensat(int dirfd, const char * file, const struct timespec * access, const struct timespec * modify, int flag) {
	if (check_user_string(file)) return -EFAULT;
	if (flag & ~(AT_SYMLINK_NOFOLLOW)) return -EINVAL;

	int flags = O_PATH;
	if (flag & AT_SYMLINK_NOFOLLOW) flags |= O_NOFOLLOW;

	int error = 0;
	struct fs_file_description * fd = do_dirfd(dirfd);
	struct fs_file_description * out = kopen_at(fd, file, flags, 0, &error);
	if (!out) return -error;
	long ret = utimens_node(out->inode, access, modify);
	fs_close_desc(out);
	return ret;
}

long sys_futimens(int fd, const struct timespec * access, const struct timespec * modify) {
	if (!FD_CHECK(fd)) return -EBADF;
	if (!(FD_MODE(fd) & PROC_FD_MODE__RW)) return -EBADF;
	return utimens_node(FD_ENTRY(fd), access, modify);
}

long sys_gettimeofday(struct timeval * tv, void * tz) {
	PTRCHECK(tv,sizeof(struct timeval),MMU_PTR_WRITE);
	PTR_VALIDATE(tz);
	return gettimeofday(tv, tz);
}

long sys_settimeofday(struct timeval * tv, void * tz) {
	extern int settimeofday(struct timeval * t, void *z);
	if (this_core->current_process->user != USER_ROOT_UID) return -EPERM;
	PTRCHECK(tv,sizeof(struct timeval),0);
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
		this_core->current_process->saved_user = new_uid;
		return 0;
	} else if (new_uid == this_core->current_process->real_user || this_core->current_process->saved_user) {
		this_core->current_process->user = new_uid;
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
		this_core->current_process->saved_user_group = new_gid;
		return 0;
	} else if (new_gid == this_core->current_process->real_user_group || this_core->current_process->saved_user_group) {
		this_core->current_process->user_group = new_gid;
		return 0;
	}
	return -EPERM;
}

static int valid_uid(uid_t id) {
	if (this_core->current_process->user == USER_ROOT_UID) return 1;
	return (id == this_core->current_process->real_user ||
	        id == this_core->current_process->user ||
	        id == this_core->current_process->saved_user);
}

long sys_setresuid(uid_t ruid, uid_t euid, uid_t suid) {
	if (ruid != -1 && !valid_uid(ruid)) return -EPERM;
	if (euid != -1 && !valid_uid(euid)) return -EPERM;
	if (suid != -1 && !valid_uid(suid)) return -EPERM;

	if (ruid != -1) this_core->current_process->real_user = ruid;
	if (euid != -1) this_core->current_process->user = euid;
	if (suid != -1) this_core->current_process->saved_user = suid;

	return 0;
}

long sys_setreuid(uid_t ruid, uid_t euid) {
	if (ruid != -1 && !valid_uid(ruid)) return -EPERM;
	if (euid != -1 && !valid_uid(euid)) return -EPERM;

	if (ruid != -1) this_core->current_process->real_user = ruid;
	if (euid != -1) this_core->current_process->user = euid;

	if (ruid != -1 || this_core->current_process->user != this_core->current_process->real_user) {
		this_core->current_process->saved_user = this_core->current_process->user;
	}

	return 0;
}

long sys_getresuid(uid_t *ruid, uid_t *euid, uid_t *suid) {
	if (ruid) {
		PTRCHECK(ruid, sizeof(uid_t), MMU_PTR_WRITE);
		*ruid = this_core->current_process->real_user;
	}

	if (euid) {
		PTRCHECK(euid, sizeof(uid_t), MMU_PTR_WRITE);
		*euid = this_core->current_process->user;
	}

	if (suid) {
		PTRCHECK(suid, sizeof(uid_t), MMU_PTR_WRITE);
		*suid = this_core->current_process->saved_user;
	}

	return 0;
}

long sys_getresgid(gid_t *rgid, gid_t *egid, gid_t *sgid) {
	if (rgid) {
		PTRCHECK(rgid, sizeof(gid_t), MMU_PTR_WRITE);
		*rgid = this_core->current_process->real_user;
	}

	if (egid) {
		PTRCHECK(egid, sizeof(gid_t), MMU_PTR_WRITE);
		*egid = this_core->current_process->user;
	}

	if (sgid) {
		PTRCHECK(sgid, sizeof(gid_t), MMU_PTR_WRITE);
		*sgid = this_core->current_process->saved_user;
	}

	return 0;
}

static int valid_gid(uid_t id) {
	if (this_core->current_process->user == USER_ROOT_UID) return 1;
	return (id == this_core->current_process->real_user_group ||
	        id == this_core->current_process->user_group ||
	        id == this_core->current_process->saved_user_group);
}


long sys_setresgid(gid_t rgid, gid_t egid, gid_t sgid) {
	if (rgid != -1 && !valid_gid(rgid)) return -EPERM;
	if (egid != -1 && !valid_gid(egid)) return -EPERM;
	if (sgid != -1 && !valid_gid(sgid)) return -EPERM;

	if (rgid != -1) this_core->current_process->real_user_group = rgid;
	if (egid != -1) this_core->current_process->user_group = egid;
	if (sgid != -1) this_core->current_process->saved_user_group = sgid;

	return 0;
}

long sys_setregid(gid_t rgid, gid_t egid) {
	if (rgid != -1 && !valid_gid(rgid)) return -EPERM;
	if (egid != -1 && !valid_gid(egid)) return -EPERM;

	if (rgid != -1) this_core->current_process->real_user_group = rgid;
	if (egid != -1) this_core->current_process->user_group = egid;

	if (rgid != -1 || this_core->current_process->user_group != this_core->current_process->real_user_group) {
		this_core->current_process->saved_user_group = this_core->current_process->user_group;
	}

	return 0;
}

long sys_getgroups(int size, gid_t list[]) {
	if (size == 0) {
		return this_core->current_process->supplementary_group_count;
	} else if (size < this_core->current_process->supplementary_group_count) {
		return -EINVAL;
	} else {
		PTRCHECK(list,sizeof(gid_t)*size,MMU_PTR_WRITE);
		for (int i = 0; i < this_core->current_process->supplementary_group_count; ++i) {
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

	PTRCHECK(list,sizeof(gid_t)*size,0); /* We already checked for 0 size */
	for (int i = 0; i < size; ++i) {
		this_core->current_process->supplementary_group_list[i] = list[i];
	}

	return 0;
}


long sys_getpid(void) {
	/* The user actually wants the pid of the originating thread (which can be us). */
	return this_core->current_process->process->id;
}

long sys_gettid(void) {
	return (long)this_core->current_process->id;
}

long sys_setsid(void) {
	if (this_core->current_process->job == this_core->current_process->tgid) {
		return -EPERM;
	}
	this_core->current_process->session = this_core->current_process->tgid;
	this_core->current_process->job = this_core->current_process->tgid;
	return this_core->current_process->session;
}

long sys_getsid(pid_t pid) {
	process_t * proc = pid == 0 ? (process_t*)this_core->current_process : process_from_pid(pid);
	if (!proc) return -ESRCH;
	return proc->session;
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
	if (proc->session != this_core->current_process->session || proc->session == proc->tgid) {
		return -EPERM;
	}

	if (pgid == 0) {
		proc->job = proc->tgid;
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

long sys_getppid(void) {
	process_t * proc = (process_t*)this_core->current_process->process;
	process_t * parent = process_get_parent(proc);
	if (!parent) return 0;
	return parent->id;
}

long sys_uname(struct utsname * name) {
	PTRCHECK(name, sizeof(struct utsname), MMU_PTR_WRITE);
	char version_number[256];
	snprintf(version_number, 255, KERNEL_VERSION_FORMAT, KERNEL_VERSION_ELEMENTS);
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
	if (check_user_string(newdir)) return -EFAULT;
	int error = 0;
	struct fs_file_description * fd = kopen_at(do_dirfd(AT_FDCWD), newdir, O_DIRECTORY | O_PATH, 0, &error);
	if (!fd) return -error;

	/* Need X permission at least... */
	if (!has_permission(fd->inode, X_OK)) {
		fs_close_desc(fd);
		return -EACCES;
	}

	process_chdir((process_t*)this_core->current_process, fd);

	fs_close_desc(fd);
	return 0;
}

long sys_fchdir(int dirfd) {
	if (!FD_CHECK(dirfd)) return -EBADF;
	if (FD_ENTRY(dirfd)->type != INO_DIR) return -ENOTDIR;
	if (!has_permission(FD_ENTRY(dirfd), X_OK)) return -EACCES;
	process_chdir((process_t*)this_core->current_process, FD_FILE(dirfd));
	return 0;
}

long sys_getcwd(char * buf, size_t size) {
	PTRCHECK(buf,size,MMU_PTR_WRITE); /* Never allow this to be NULL in the syscall layer */
	size_t len = strlen(fs_current_wd()) + 1;
	if (size < len) return -ERANGE;
	memcpy(buf, fs_current_wd(), len);
	return len;
}

long sys_dup2(int old, int new) {
	return process_move_fd((process_t *)this_core->current_process, old, new, 0, 0);
}

long sys_dup3(int old, int new, int flag) {
	if (new < 0) return -EBADF; /* only dup2 syscall interface accepts -1 */
	int flags = 0;
	if (flag & ~(O_CLOEXEC | O_CLOFORK)) return -EINVAL;
	if (flag & O_CLOEXEC) flags |= PROC_FD_MODE_CLOEXEC;
	if (flag & O_CLOFORK) flags |= PROC_FD_MODE_CLOFORK;
	return process_move_fd((process_t *)this_core->current_process, old, new, 1, flags);
}

long sys_fcntl(int fd, int cmd, long arg) {
	if (!FD_CHECK(fd)) return -EBADF;

	switch (cmd) {
		case F_GETFD: {
			int flags = 0;
			if (FD_CLO_MODE(fd) & FD_PTR_CLOEXEC) flags |= FD_CLOEXEC;
			if (FD_CLO_MODE(fd) & FD_PTR_CLOFORK) flags |= FD_CLOFORK;
			return flags;
		}
		case F_SETFD: {
			int new_mode = 0;
			if (arg & FD_CLOEXEC) new_mode |= FD_PTR_CLOEXEC;
			if (arg & FD_CLOFORK) new_mode |= FD_PTR_CLOFORK;
			fd_set_mode_flags(fd, new_mode);
			return 0;
		}
		case F_GETFL: {
			return (long)fs_convert_descriptor_flags((uintptr_t)FD_FILE(fd));
		}
		case F_SETFL: {
			if (arg & O_APPEND) FD_MODE(fd) |= PROC_FD_MODE_APPEND;
			else FD_MODE(fd) &= ~(PROC_FD_MODE_APPEND);
			return 0;
		}
		case F_DUPFD_CLOEXEC:
		case F_DUPFD_CLOFORK:
		case F_DUPFD: {
			if (arg < 0 || arg > 256) return -EINVAL; /* We expect a value of, like, 10 from dash. */
			int flags = 0;
			if (cmd == F_DUPFD_CLOEXEC) flags = PROC_FD_MODE_CLOEXEC;
			if (cmd == F_DUPFD_CLOFORK) flags = PROC_FD_MODE_CLOFORK;
			return process_fd_dup_least((process_t*)this_core->current_process, fd, arg, flags);
		}
		case F_GETLK:
		case F_SETLK:
		case F_SETLKW:
			/* No lock support */
			return -EINVAL;
	}

	return -EINVAL;
}

long sys_sethostname(char * new_hostname, size_t len) {
	if (this_core->current_process->user == USER_ROOT_UID) {
		PTRCHECK(new_hostname, len, 0); /* Don't accept NULL as an option, regardless */
		/* new_hostname does not need to be nul-terminated with 'len';
		 * account for adding a trailing nul byte */
		if (len > 255) return -ENAMETOOLONG;
		memcpy(hostname, new_hostname, len);
		hostname_len = len + 1; /* hostname_len includes the nul */
		hostname[len] = '\0';
		return 0;
	} else {
		return -EPERM;
	}
}

long sys_gethostname(char * buffer, size_t len) {
	PTRCHECK(buffer, len, MMU_PTR_WRITE); /* Don't allow NULL in this syscall interface, regardless */
	if (len < hostname_len) return -ENAMETOOLONG;
	/* hostname is nul-terminated; hostname_len includes the nul */
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

long sys_umask(mode_t mode) {
	mode_t old_mode = this_core->current_process->process->mask;
	this_core->current_process->process->mask = mode & 0777;
	return old_mode;
}

long sys_unlinkat(int dirfd, const char * name, int flag) {
	if (check_user_string(name)) return -EFAULT;
	if (flag & ~(AT_REMOVEDIR)) return -EINVAL;

	return unlink_fs_at(do_dirfd(dirfd), name, flag);
}

long sys_linkat(int fd1, const char *path1, int fd2, const char *path2, int flag) {
	if (check_user_string(path1) || check_user_string(path2)) return -EFAULT;
	struct fs_file_description * fd1_fd = do_dirfd(fd1);
	struct fs_file_description * fd2_fd = do_dirfd(fd2);

	return link_fs_at(fd1_fd, path1, fd2_fd, path2, flag);
}

long sys_execve(const char * filename, char *const argv[], char *const envp[]) {
	if (check_user_string(filename)) return -EFAULT;
	PTR_VALIDATE(argv);
	PTR_VALIDATE(envp);

	if (!filename || !argv) return -EFAULT;

	int argc = 0;
	int envc = 0;
	while (argv[argc]) {
		if (check_user_string(argv[argc])) return -EFAULT;
		++argc;
	}

	if (envp) {
		while (envp[envc]) {
			if (check_user_string(envp[envc])) return -EFAULT;
			++envc;
		}
	}

	process_free_cmdline((process_t*)this_core->current_process);

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
	if (status) PTRCHECK(status,sizeof(int),MMU_PTR_WRITE);
	return waitpid(pid, status, options);
}

long sys_yield(void) {
	switch_task(1);
	return 1;
}

long sys_nanosleep(const struct timespec *rqtp, struct timespec *rmtp) {
	PTRCHECK(rqtp, sizeof(struct timespec), 0);
	if (rmtp) PTRCHECK(rmtp, sizeof(struct timespec), MMU_PTR_WRITE);

	time_t secs = rqtp->tv_sec;
	long   nsecs = rqtp->tv_nsec;
	if (secs < 0 || nsecs < 0 || nsecs >= 1000000000) return -EINVAL;

	long usecs = nsecs / 1000;
	if (nsecs % 1000) usecs += 1;

	unsigned long target_ticks = 0, target_subticks = 0;
	relative_time(secs, usecs, &target_ticks, &target_subticks);

	sleep_until((process_t *)this_core->current_process, target_ticks, target_subticks);
	switch_task(0);

	unsigned long timer_ticks = 0, timer_subticks = 0;
	relative_time(0,0,&timer_ticks,&timer_subticks);

	if (timer_ticks > target_ticks || (timer_ticks == target_ticks && timer_subticks >= target_subticks)) {
		if (rmtp) {
			rmtp->tv_sec = 0;
			rmtp->tv_nsec = 0;
		}
		return 0;
	}

	if (rmtp) {
		if (timer_ticks == target_ticks) {
			rmtp->tv_sec = 0;
			rmtp->tv_nsec = (target_subticks - timer_subticks) * 1000;
		} else if (timer_subticks > target_subticks) {
			rmtp->tv_sec = target_ticks - timer_ticks - 1;
			rmtp->tv_nsec = ((1000000 + target_subticks) - timer_subticks) * 1000;
		} else {
			rmtp->tv_sec = target_ticks - timer_ticks;
			rmtp->tv_nsec = (target_subticks - timer_subticks) * 1000;
		}
	}

	return -EINTR;
}

long sys_pipe2(int pipes[2], int flag) {
	if (flag & ~(O_NONBLOCK | O_CLOEXEC | O_CLOFORK)) return -EINVAL;
	PTRCHECK(pipes, sizeof(int) * 2, MMU_PTR_WRITE);
	fs_node_t * outpipes[2];
	make_unix_pipe(outpipes);

	open_fs(outpipes[0], 0);
	open_fs(outpipes[1], 0);

	int flags = PROC_FD_MODE__RW;
	if (flag & O_CLOEXEC) flags |= PROC_FD_MODE_CLOEXEC;
	if (flag & O_CLOFORK) flags |= PROC_FD_MODE_CLOFORK;

	static uint64_t pipe_count = 0;
	uint64_t pipe_cnt = pipe_count++;

	/* TODO paths on descriptions... */
	pipes[0] = process_new_fd((process_t *)this_core->current_process, outpipes[0], flags, fs_path_printf("pipe:[%zu]", pipe_cnt));
	pipes[1] = process_new_fd((process_t *)this_core->current_process, outpipes[1], flags, fs_path_printf("pipe:[%zu]", pipe_cnt));

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

long sys_sigaltstack(const stack_t * ss, stack_t * oss) {
	stack_t cas = this_core->current_process->altstack;
	cas.ss_flags = 0;
	if (cas.ss_size) {
		uintptr_t current_sp = arch_stack_pointer(this_core->current_process->syscall_registers);
		if (current_sp > (uintptr_t)cas.ss_sp && current_sp - (uintptr_t)cas.ss_sp <= cas.ss_size) {
			cas.ss_flags = SS_ONSTACK;
		}
	} else {
		cas.ss_flags = SS_DISABLE;
	}

	if (oss) {
		PTRCHECK(oss, sizeof(stack_t), MMU_PTR_WRITE);
		memcpy(oss, &cas, sizeof(stack_t));
	}

	if (ss) {

		/* Tried to modify while on the alt stack, which is invalid. */
		if (cas.ss_flags & SS_ONSTACK) return -EPERM;

		PTRCHECK(ss, sizeof(stack_t), 0);
		stack_t tas;
		memcpy(&tas, ss, sizeof(stack_t));

		/* No change, return immediately. */
		if (tas.ss_flags == cas.ss_flags &&
		    tas.ss_sp == cas.ss_sp &&
		    tas.ss_size == cas.ss_size) {
			return 0;
		}

		if (tas.ss_flags & ~(SS_DISABLE)) return -EINVAL;
		if (!(tas.ss_flags & SS_DISABLE) && tas.ss_size < MINSIGSTKSZ) return -ENOMEM;

		spin_lock(this_core->current_process->sig_lock);

		if (tas.ss_flags & SS_DISABLE) {
			this_core->current_process->altstack.ss_sp = 0;
			this_core->current_process->altstack.ss_size = 0;
			this_core->current_process->altstack.ss_flags = SS_DISABLE;
		} else {
			this_core->current_process->altstack.ss_sp = tas.ss_sp;
			this_core->current_process->altstack.ss_size = tas.ss_size;
			this_core->current_process->altstack.ss_flags = 0;
		}

		spin_unlock(this_core->current_process->sig_lock);
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

extern void signal_wait(void);
long sys_sigsuspend(const sigset_t *set) {
	PTRCHECK(set,sizeof(sigset_t),0);

	this_core->current_process->restored_signals = this_core->current_process->blocked_signals;
	this_core->current_process->blocked_signals = *set;

	signal_wait();

	__sync_or_and_fetch(&this_core->current_process->flags, PROC_FLAG_RESTORE_SIGMASK);
	return -ERESTARTSIGSUSPEND;
}

long sys_sigwait(sigset_t * set, siginfo_t *info) {
	PTRCHECK(set,sizeof(sigset_t),0);

	/* Silently ignore attempts to wait on KILL or STOP */
	sigset_t awaited = *set & ~((1 << SIGKILL) | (1 << SIGSTOP));

	/* Don't let processes wait on unblocked signals */
	if (awaited & ~this_core->current_process->blocked_signals) return -EINVAL;

	siginfo_t cause = {0};
	long ret = signal_await(awaited, &cause);

	if (info) {
		PTRCHECK(info,sizeof(siginfo_t),MMU_PTR_WRITE);
		memcpy(info, &cause, sizeof(siginfo_t));
	}

	return ret;
}

long sys_fswait(int c, int fds[]) {
	if (c < 0) return -EFAULT;
	PTRCHECK(fds, sizeof(int) * c, 0);
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
	if (c < 0) return -EFAULT;
	PTRCHECK(fds, sizeof(int) * c, 0);
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
	if (!fds || !out || c < 0) return -EFAULT;
	PTRCHECK(fds, sizeof(int) * c, 0);
	PTRCHECK(out, sizeof(int) * c, MMU_PTR_WRITE);
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

long sys_openpty(int * manager, int * subsidiary, char * name, struct termios * termp, struct winsize * size) {
	/* We require a place to put these when we are done. */
	PTRCHECK(manager,sizeof(int),MMU_PTR_WRITE);
	PTRCHECK(subsidiary,sizeof(int),MMU_PTR_WRITE);
	if (name)  PTRCHECK(name,128,MMU_PTR_WRITE);
	if (termp) PTRCHECK(termp,sizeof(struct termios),0);
	if (size)  PTRCHECK(size,sizeof(struct winsize),0);

	/* Create a new pseudo terminal */
	fs_node_t * fs_manager;
	fs_node_t * fs_subsidiary;

	pty_t * pty = pty_create(size, &fs_manager, &fs_subsidiary);

	char pty_name[256] = "ptm:";
	pty->fill_name(pty, 252, pty_name + 4);

	if (name && mmu_validate_user_pointer(name, strlen(pty_name + 4) + 1, MMU_PTR_WRITE)) {
		memcpy(name, pty_name + 4, strlen(pty_name + 4) + 1);
	}

	if (termp) {
		memcpy(&pty->tios, termp, sizeof(struct termios));
	}

	/* Append the manager and subsidiary to the calling process */
	*manager = process_new_fd((process_t *)this_core->current_process, fs_manager,PROC_FD_MODE__RW|PROC_FD_MODE_CLOEXEC, fs_alloc_path_from(pty_name, "openpty"));
	*subsidiary  = process_new_fd((process_t *)this_core->current_process, fs_subsidiary, PROC_FD_MODE__RW|PROC_FD_MODE_CLOEXEC, fs_alloc_path_from(pty_name+4, "openpty"));

	open_fs(fs_manager, 0);
	open_fs(fs_subsidiary, 0);

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

long sys_sigqueue(pid_t process, int signal, union sigval value) {
	/* Unlike kill, this doesn't need to support groups or whatnot, just a single pid */
	if (process < 1) return -ESRCH;

	siginfo_t cause = {0};
	cause.si_code  = SI_QUEUE;
	cause.si_value = value;
	cause.si_pid   = this_core->current_process->id;
	cause.si_uid   = this_core->current_process->real_user;

	return send_signal_info(process, signal, 0, &cause);
}

long sys_reboot(int unused) {
	(void)unused;
	if (this_core->current_process->user != USER_ROOT_UID) {
		return -EPERM;
	}

	return arch_reboot();
}

long sys_times(struct tms *buf) {
	if (buf) {
		PTRCHECK(buf,sizeof(struct tms),MMU_PTR_WRITE);

		buf->tms_utime  = (this_core->current_process->time_total - this_core->current_process->time_sys) / arch_cpu_mhz();
		buf->tms_stime  = this_core->current_process->time_sys          / arch_cpu_mhz();
		buf->tms_cutime = (this_core->current_process->time_children - this_core->current_process->time_sys_children) / arch_cpu_mhz();
		buf->tms_cstime = this_core->current_process->time_sys_children / arch_cpu_mhz();
	}

	return arch_perf_timer() / arch_cpu_mhz();
}

long sys_getrusage(int who, struct rusage *buf) {
	PTRCHECK(buf,sizeof(struct rusage),MMU_PTR_WRITE);
	if (who != RUSAGE_SELF && who != RUSAGE_CHILDREN) return -EINVAL;

	uint64_t utime, stime;
	if (who == RUSAGE_SELF) {
		utime = (this_core->current_process->time_total - this_core->current_process->time_sys) / arch_cpu_mhz();
		stime = this_core->current_process->time_sys / arch_cpu_mhz();
	} else {
		utime = (this_core->current_process->time_children - this_core->current_process->time_sys_children) / arch_cpu_mhz();
		stime = this_core->current_process->time_sys_children / arch_cpu_mhz();
	}

	buf->ru_utime.tv_sec = utime / 1000000UL;
	buf->ru_utime.tv_usec = utime % 1000000UL;
	buf->ru_stime.tv_sec = stime / 1000000UL;
	buf->ru_stime.tv_usec = stime % 1000000UL;

	return 0;
}

long sys_sbrk(ssize_t size) {
	return mmap_sbrk(size);
}

long sys_mmap(uintptr_t addr, size_t length, int prot, int flags, int fd, off_t offset) {
	/* Exactly one of MAP_SHARED or MAP_PRIVATE must be set. */
	if (!(flags & (MAP_SHARED | MAP_PRIVATE))) return -EINVAL;
	if ((flags & MAP_SHARED) && (flags & MAP_PRIVATE)) return -EINVAL;

	/* Restrict prot and flags to user-accessible values */
	if (prot & ~(PROT_EXEC | PROT_READ | PROT_WRITE)) return -EINVAL;
	if (flags & ~(MAP_SHARED | MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS)) return -EINVAL;

	/* MAP_ANONYMOUS skips file checks */
	if (flags & MAP_ANONYMOUS) return do_mmap(addr, length, prot, flags, NULL, 0);

	if (!FD_CHECK(fd)) return -EBADF;
	if (!(FD_MODE(fd) & PROC_FD_MODE__RW)) return -EBADF;

	/* File must be something we can actually map. */
	if ((FD_ENTRY(fd)->type == INO_FIFO) || (FD_ENTRY(fd)->type == INO_CHR) || (FD_ENTRY(fd)->type == INO_SOCK)) return -ENODEV;

	/* File must be open at least for reading. */
	if (!(FD_MODE(fd) & PROC_FD_MODE_READ)) return -EACCES;

	/* SHARED + WRITE requires file was open for write access */
	if ((flags & MAP_SHARED) && (prot & PROT_WRITE) && !(FD_MODE(fd) & PROC_FD_MODE_WRITE)) return -EACCES;

	/* Store original file writability */
	if (FD_MODE(fd) & PROC_FD_MODE_WRITE) flags |= MAP_FD_WRITABLE;

	return do_mmap(addr, length, prot, flags, FD_FILE(fd), offset);
}

long sys_munmap(uintptr_t addr, size_t length) {
	if (addr & 0xFFF) return -EINVAL;
	if (length == 0) return -EINVAL;
	return mmap_unmap(addr, length);
}

long sys_nproc(void) {
	return processor_count;
}

long sys_set_tls_base(uintptr_t addr) {
	PTR_VALIDATE(addr);

	this_core->current_process->thread.context.tls_base = addr;
	arch_set_tls_base(this_core->current_process->thread.context.tls_base);
	return 0;
}

extern int elf_module(fs_node_t * file, int argc, char ** args);
long sys_insmod(int fd, int argc, char **argv) {
	if (this_core->current_process->user != USER_ROOT_UID) return -EPERM;
	if (argc < 1) return -EINVAL;
	PTRCHECK(argv, sizeof(char*) * argc, 0);
	for (int i = 0; i < argc; ++i) {
		if (check_user_string(argv[i])) return -EFAULT;
	}
	if (!FD_CHECK(fd)) return -EBADF;
	if (!(FD_MODE(fd) & PROC_FD_MODE_READ)) return -EBADF;
	return elf_module(FD_ENTRY(fd), argc, argv);
}

extern long ptrace_handle(long,pid_t,void*,void*);

typedef long (*scall_func)(long,long,long,long,long,long);

static scall_func syscalls[] = {
	/* System Call Table */
	[SYS_EXT]          = (scall_func)(uintptr_t)sys_exit,
	[SYS_GETEUID]      = (scall_func)(uintptr_t)sys_geteuid,
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
	[SYS_GETTID]       = (scall_func)(uintptr_t)sys_gettid,
	[SYS_IOCTL]        = (scall_func)(uintptr_t)sys_ioctl,
	[SYS_UMASK]        = (scall_func)(uintptr_t)sys_umask,
	[SYS_MOUNT]        = (scall_func)(uintptr_t)sys_mount,
	[SYS_SETSID]       = (scall_func)(uintptr_t)sys_setsid,
	[SYS_SETPGID]      = (scall_func)(uintptr_t)sys_setpgid,
	[SYS_GETPGID]      = (scall_func)(uintptr_t)sys_getpgid,
	[SYS_DUP2]         = (scall_func)(uintptr_t)sys_dup2,
	[SYS_EXECVE]       = (scall_func)(uintptr_t)sys_execve,
	[SYS_FORK]         = (scall_func)(uintptr_t)sys_fork,
	[SYS_WAITPID]      = (scall_func)(uintptr_t)sys_waitpid,
	[SYS_YIELD]        = (scall_func)(uintptr_t)sys_yield,
	[SYS_PIPE2]        = (scall_func)(uintptr_t)sys_pipe2,
	[SYS_FSWAIT]       = (scall_func)(uintptr_t)sys_fswait,
	[SYS_FSWAIT2]      = (scall_func)(uintptr_t)sys_fswait_timeout,
	[SYS_FSWAIT3]      = (scall_func)(uintptr_t)sys_fswait_multi,
	[SYS_CLONE]        = (scall_func)(uintptr_t)sys_clone,
	[SYS_OPENPTY]      = (scall_func)(uintptr_t)sys_openpty,
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
	[SYS_SIGSUSPEND]   = (scall_func)(uintptr_t)sys_sigsuspend,
	[SYS_SIGWAIT]      = (scall_func)(uintptr_t)sys_sigwait,
	[SYS_PREAD]        = (scall_func)(uintptr_t)sys_pread,
	[SYS_PWRITE]       = (scall_func)(uintptr_t)sys_pwrite,
	[SYS_FCNTL]        = (scall_func)(uintptr_t)sys_fcntl,
	[SYS_FCHMOD]       = (scall_func)(uintptr_t)sys_fchmod,
	[SYS_FCHOWN]       = (scall_func)(uintptr_t)sys_fchown,
	[SYS_TRUNCATE]     = (scall_func)(uintptr_t)sys_truncate,
	[SYS_FTRUNCATE]    = (scall_func)(uintptr_t)sys_ftruncate,
	[SYS_GETPPID]      = (scall_func)(uintptr_t)sys_getppid,
	[SYS_GETRUSAGE]    = (scall_func)(uintptr_t)sys_getrusage,
	[SYS_SIGQUEUE]     = (scall_func)(uintptr_t)sys_sigqueue,
	[SYS_SETRESUID]    = (scall_func)(uintptr_t)sys_setresuid,
	[SYS_SETREUID]     = (scall_func)(uintptr_t)sys_setreuid,
	[SYS_SETRESGID]    = (scall_func)(uintptr_t)sys_setresgid,
	[SYS_SETREGID]     = (scall_func)(uintptr_t)sys_setregid,
	[SYS_MMAP]         = (scall_func)(uintptr_t)sys_mmap,
	[SYS_MUNMAP]       = (scall_func)(uintptr_t)sys_munmap,
	[SYS_NPROC]        = (scall_func)(uintptr_t)sys_nproc,
	[SYS_SETTLSBASE]   = (scall_func)(uintptr_t)sys_set_tls_base,
	[SYS_INSMOD]       = (scall_func)(uintptr_t)sys_insmod,
	[SYS_GETSID]       = (scall_func)(uintptr_t)sys_getsid,
	[SYS_NANOSLEEP]    = (scall_func)(uintptr_t)sys_nanosleep,
	[SYS_FUTIMENS]     = (scall_func)(uintptr_t)sys_futimens,
	[SYS_SIGALTSTACK]  = (scall_func)(uintptr_t)sys_sigaltstack,
	[SYS_GETRESUID]    = (scall_func)(uintptr_t)sys_getresuid,
	[SYS_GETRESGID]    = (scall_func)(uintptr_t)sys_getresgid,
	[SYS_OPENAT]       = (scall_func)(uintptr_t)sys_openat,
	[SYS_FSTATAT]      = (scall_func)(uintptr_t)sys_fstatat,
	[SYS_FCHDIR]       = (scall_func)(uintptr_t)sys_fchdir,
	[SYS_FACCESSAT]    = (scall_func)(uintptr_t)sys_faccessat,
	[SYS_FCHMODAT]     = (scall_func)(uintptr_t)sys_fchmodat,
	[SYS_FCHOWNAT]     = (scall_func)(uintptr_t)sys_fchownat,
	[SYS_READLINKAT]   = (scall_func)(uintptr_t)sys_readlinkat,
	[SYS_UTIMENSAT]    = (scall_func)(uintptr_t)sys_utimensat,
	[SYS_MKDIRAT]      = (scall_func)(uintptr_t)sys_mkdirat,
	[SYS_SYMLINKAT]    = (scall_func)(uintptr_t)sys_symlinkat,
	[SYS_RENAMEAT]     = (scall_func)(uintptr_t)sys_renameat,
	[SYS_UNLINKAT]     = (scall_func)(uintptr_t)sys_unlinkat,
	[SYS_LINKAT]       = (scall_func)(uintptr_t)sys_linkat,

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

	this_core->current_process->syscall_registers = r;

	if (!mmu_check_syscall_capability(this_core->current_process->process, arch_user_ip(r))) {
		arch_syscall_return(r, -EFAULT);
		send_signal(this_core->current_process->id, SIGSYS, 1);
		return;
	}

	if (this_core->current_process->flags & PROC_FLAG_TRACE_SYSCALLS) {
		ptrace_signal(SIGTRAP, PTRACE_EVENT_SYSCALL_ENTER);
	}

	long result;

	if (arch_syscall_number(r) < 0 || arch_syscall_number(r) >= num_syscalls) {
		result = -EINVAL;
		goto _finish_syscall;
	}

	scall_func func = syscalls[arch_syscall_number(r)];
	result = func(
		arch_syscall_arg0(r), arch_syscall_arg1(r), arch_syscall_arg2(r),
		arch_syscall_arg3(r), arch_syscall_arg4(r), arch_syscall_arg5(r));

	if (result == -ERESTARTSYS || result == -ERESTARTSIGSUSPEND) {
		this_core->current_process->interrupted_system_call = arch_syscall_number(r);
	}

_finish_syscall:
	arch_syscall_return(r, result);

	if (this_core->current_process->flags & PROC_FLAG_TRACE_SYSCALLS) {
		ptrace_signal(SIGTRAP, PTRACE_EVENT_SYSCALL_EXIT);
	}
}
