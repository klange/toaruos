/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2011-2018 K. Lange
 * Copyright (C) 2012 Markus Schober
 *
 * Syscall Tables
 *
 */
#include <kernel/system.h>
#include <kernel/process.h>
#include <kernel/logging.h>
#include <kernel/fs.h>
#include <kernel/pipe.h>
#include <kernel/version.h>
#include <kernel/shm.h>
#include <kernel/printf.h>
#include <kernel/module.h>
#include <kernel/args.h>

#include <sys/utsname.h>
#include <syscall_nums.h>

static char   hostname[256];
static size_t hostname_len = 0;

#define FD_INRANGE(FD) \
	((FD) < (int)current_process->fds->length && (FD) >= 0)
#define FD_ENTRY(FD) \
	(current_process->fds->entries[(FD)])
#define FD_CHECK(FD) \
	(FD_INRANGE(FD) && FD_ENTRY(FD))
#define FD_OFFSET(FD) \
	(current_process->fds->offsets[(FD)])
#define FD_MODE(FD) \
	(current_process->fds->modes[(FD)])

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
	task_exit((retval & 0xFF) << 8);
	for (;;) ;
}

static int sys_read(int fd, char * ptr, int len) {
	if (FD_CHECK(fd)) {
		PTR_VALIDATE(ptr);

		fs_node_t * node = FD_ENTRY(fd);
		if (!(FD_MODE(fd) & 01)) {
			debug_print(WARNING, "access denied (read, fd=%d, mode=%d, %s, %s)", fd, FD_MODE(fd), node->name, current_process->name);
			return -EACCES;
		}
		uint32_t out = read_fs(node, FD_OFFSET(fd), len, (uint8_t *)ptr);
		FD_OFFSET(fd) += out;
		return (int)out;
	}
	return -EBADF;
}

static int sys_ioctl(int fd, int request, void * argp) {
	if (FD_CHECK(fd)) {
		PTR_VALIDATE(argp);
		return ioctl_fs(FD_ENTRY(fd), request, argp);
	}
	return -EBADF;
}

static int sys_readdir(int fd, int index, struct dirent * entry) {
	if (FD_CHECK(fd)) {
		PTR_VALIDATE(entry);
		struct dirent * kentry = readdir_fs(FD_ENTRY(fd), (uint32_t)index);
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

static int sys_write(int fd, char * ptr, int len) {
	if (FD_CHECK(fd)) {
		PTR_VALIDATE(ptr);
		fs_node_t * node = FD_ENTRY(fd);
		if (!(FD_MODE(fd) & 02)) {
			debug_print(WARNING, "access denied (write, fd=%d)", fd);
			return -EACCES;
		}
		uint32_t out = write_fs(node, FD_OFFSET(fd), len, (uint8_t *)ptr);
		FD_OFFSET(fd) += out;
		return out;
	}
	return -EBADF;
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

	int access_bits = 0;

	if (node && (flags & O_CREAT) && (flags & O_EXCL)) {
		close_fs(node);
		return -EEXIST;
	}

	if (!(flags & O_WRONLY) || (flags & O_RDWR)) {
		if (node && !has_permission(node, 04)) {
			debug_print(WARNING, "access denied (read, sys_open, file=%s)", file);
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
		debug_print(NOTICE, "- file does not exist and create was requested.");
		/* Um, make one */
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
	int fd = process_append_fd((process_t *)current_process, node);
	FD_MODE(fd) = access_bits;
	if (flags & O_APPEND) {
		FD_OFFSET(fd) = node->length;
	} else {
		FD_OFFSET(fd) = 0;
	}
	debug_print(INFO, "[open] pid=%d %s -> %d", getpid(), file, fd);
	return fd;
}

static int sys_access(const char * file, int flags) {
	PTR_VALIDATE(file);
	debug_print(INFO, "access(%s, 0x%x) from pid=%d", file, flags, getpid());
	fs_node_t * node = kopen((char *)file, 0);
	if (!node) return -ENOENT;
	close_fs(node);
	return 0;
}

static int sys_close(int fd) {
	if (FD_CHECK(fd)) {
		close_fs(FD_ENTRY(fd));
		FD_ENTRY(fd) = NULL;
		return 0;
	}
	return -EBADF;
}

static int sys_sbrk(int size) {
	process_t * proc = (process_t *)current_process;
	if (proc->group != 0) {
		proc = process_from_pid(proc->group);
	}
	spin_lock(proc->image.lock);
	uintptr_t ret = proc->image.heap;
	uintptr_t i_ret = ret;
	ret = (ret + 0xfff) & ~0xfff; /* Rounds ret to 0x1000 in O(1) */
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

	if (args_present("traceexec")) {
		debug_print(WARNING, "%d = exec(%s", current_process->id, filename);
		for (char * const * arg = argv; *arg; ++arg) {
			debug_print(WARNING, "          %s", *arg);
		}
		debug_print(WARNING, "         )");
	}

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

	current_process->cmdline = argv_;

	debug_print(INFO,"Executing...");
	/* Discard envp */
	return exec((char *)filename, argc, (char **)argv_, (char **)envp_, 0);
}

static int sys_seek(int fd, int offset, int whence) {
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

static int stat_node(fs_node_t * fn, uintptr_t st) {
	struct stat * f = (struct stat *)st;

	PTR_VALIDATE(f);

	if (!fn) {
		memset(f, 0x00, sizeof(struct stat));
		debug_print(INFO, "stat: This file doesn't exist");
		return -ENOENT;
	}
	f->st_dev   = (uint16_t)(((uint32_t)fn->device & 0xFFFF0) >> 8);
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
		/* Can group members change bits? I think it's only owners. */
		if (current_process->user != 0 && current_process->user != fn->uid) {
			close_fs(fn);
			return -EACCES;
		}
		result = chmod_fs(fn, mode);
		close_fs(fn);
		return result;
	} else {
		return -ENOENT;
	}
}

static int sys_chown(char * file, int uid, int gid) {
	int result;
	PTR_VALIDATE(file);
	fs_node_t * fn = kopen(file, 0);
	if (fn) {
		/* TODO: Owners can change groups... */
		if (current_process->user != 0) {
			close_fs(fn);
			return -EACCES;
		}
		result = chown_fs(fn, uid, gid);
		close_fs(fn);
		return result;
	} else {
		return -ENOENT;
	}
}


static int sys_stat(int fd, uintptr_t st) {
	PTR_VALIDATE(st);
	if (FD_CHECK(fd)) {
		return stat_node(FD_ENTRY(fd), st);
	}
	return -EBADF;
}

static int sys_mkpipe(void) {
	fs_node_t * node = make_pipe(4096 * 2);
	open_fs(node, 0);
	int fd = process_append_fd((process_t *)current_process, node);
	FD_MODE(fd) = 03; /* read write */
	return fd;
}

static int sys_dup2(int old, int new) {
	return process_move_fd((process_t *)current_process, old, new);
}

static int sys_getuid(void) {
	return current_process->real_user;
}

static int sys_geteuid(void) {
	return current_process->user;
}

static int sys_setuid(user_t new_uid) {
	if (current_process->user == USER_ROOT_UID) {
		current_process->user = new_uid;
		current_process->real_user = new_uid;
		return 0;
	}
	return -EPERM;
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
		return -EINVAL;
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

extern void idt_load(uint32_t *);

static int sys_reboot(void) {
	debug_print(NOTICE, "[kernel] Reboot requested from process %d by user #%d", current_process->id, current_process->user);
	if (current_process->user != USER_ROOT_UID) {
		return -EPERM;
	} else {
		debug_print(ERROR, "[kernel] Good bye!");
		/* Goodbye, cruel world */
		IRQ_OFF;
		uintptr_t phys;
		uint32_t * virt = (void*)kvmalloc_p(0x1000, &phys);
		virt[0] = 0;
		virt[1] = 0;
		virt[2] = 0;
		idt_load(virt);
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
			return -ENOTDIR;
		}
		if (!has_permission(chd, 01)) {
			close_fs(chd);
			return -EACCES;
		}
		close_fs(chd);
		free(current_process->wd_name);
		current_process->wd_name = malloc(strlen(path) + 1);
		memcpy(current_process->wd_name, path, strlen(path) + 1);
		return 0;
	} else {
		return -ENOENT;
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
			return -ENAMETOOLONG;
		}
		hostname_len = len;
		memcpy(hostname, new_hostname, hostname_len);
		return 0;
	} else {
		return -EPERM;
	}
}

static int sys_gethostname(char * buffer) {
	PTR_VALIDATE(buffer);
	memcpy(buffer, hostname, hostname_len);
	return hostname_len;
}

extern int mkdir_fs(char *name, uint16_t permission);

static int sys_mkdir(char * path, uint32_t mode) {
	return mkdir_fs(path, mode);
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
				return 0;
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
				return 0;
			case 6:
				debug_print(WARNING, "writing contents of file %s to sdb", args[0]);
				{
					PTR_VALIDATE(args);
					PTR_VALIDATE(args[0]);
					fs_node_t * file = kopen((char *)args[0], 0);
					if (!file) {
						return -EINVAL;
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
					return -EINVAL;
				}
			case 8:
				debug_print(NOTICE, "Loading module %s.", args[0]);
				{
					/* Check file existence */
					fs_node_t * file = kopen(args[0], 0);
					if (!file) {
						return 1;
					}
					close_fs(file);

					module_data_t * mod_info = module_load(args[0]);
					if (!mod_info) {
						return 2;
					}
					return 0;
				}
		}
	}
	switch (fn) {
		/* The following functions are here to support the loader and are probably bad. */
		case 9:
			{
				process_t * proc = (process_t *)current_process;
				if (proc->group != 0) {
					proc = process_from_pid(proc->group);
				}
				spin_lock(proc->image.lock);
				/* Set new heap start */
				uintptr_t address = (uintptr_t)args[0];
				/* TODO: These virtual address bounds should be in a header somewhere */
				if (address < 0x20000000) {
					spin_unlock(proc->image.lock);
					return -EINVAL;
				}
				proc->image.heap = (uintptr_t)address;
				proc->image.heap_actual = proc->image.heap & 0xFFFFF000;
				assert(proc->image.heap_actual % 0x1000 == 0);
				alloc_frame(get_page(proc->image.heap_actual, 1, current_directory), 0, 1);
				invalidate_tables_at(proc->image.heap_actual);
				while (proc->image.heap > proc->image.heap_actual) {
					proc->image.heap_actual += 0x1000;
					alloc_frame(get_page(proc->image.heap_actual, 1, current_directory), 0, 1);
					invalidate_tables_at(proc->image.heap_actual);
				}
				spin_unlock(proc->image.lock);
				return 0;
			}
		case 10:
			{
				/* Load pages to fit region. */
				uintptr_t address = (uintptr_t)args[0];
				/* TODO: These virtual address bounds should be in a header somewhere */
				if (address < 0x20000000) return -EINVAL;
				/* TODO: Upper bounds */
				size_t size = (size_t)args[1];
				/* TODO: Other arguments for read/write? */

				if (address & 0xFFF) {
					size += address & 0xFFF;
					address &= 0xFFFFF000;
				}

				process_t * proc = (process_t *)current_process;
				if (proc->group != 0) {
					proc = process_from_pid(proc->group);
				}

				spin_lock(proc->image.lock);
				for (size_t x = 0; x < size; x += 0x1000) {
					alloc_frame(get_page(address + x, 1, current_directory), 0, 1);
					invalidate_tables_at(address + x);
				}
				spin_unlock(proc->image.lock);

				return 0;
			}

		case 11:
			{
				/* Set command line (meant for threads to set descriptions) */
				int count = 0;
				char **arg = args;
				PTR_VALIDATE(args);
				while (*arg) {
					PTR_VALIDATE(*args);
					count++;
					arg++;
				}
				/*
				 * XXX We have a pretty obvious leak in storing command lines, since
				 *     we never free them! Unfortunately, at the moment, they are
				 *     shared between different processes, so until that gets fixed
				 *     we're going to be just as bad as the rest of the codebase and
				 *     just not free the previous value.
				 */
				current_process->cmdline = malloc(sizeof(char*)*(count+1));
				int i = 0;
				while (i < count) {
					current_process->cmdline[i] = strdup(args[i]);
					i++;
				}
				current_process->cmdline[i] = NULL;
				return 0;
			}

		case 12:
			/*
			 * Print a debug message to the kernel console
			 * XXX: This probably should be a thing normal users can do.
			 */
			PTR_VALIDATE(args);
			debug_print(WARNING, "0x%x 0x%x 0x%x 0x%x", args[0], args[1], args[2], args[3]);
			_debug_print(args[0], (uintptr_t)args[1], (uint32_t)args[2], args[3] ? args[3] : "(null)");
			return 0;

		case 13:
			/*
			 * Set VGA text-mode cursor location
			 * (Not actually used to place a cursor, we use this to move the cursor off screen)
			 */
			PTR_VALIDATE(args);
			outportb(0x3D4, 14);
			outportb(0x3D5, (unsigned int)args[0]);
			outportb(0x3D4, 15);
			outportb(0x3D5, (unsigned int)args[1]);

			return 0;

		default:
			debug_print(ERROR, "Bad system function %d", fn);
			break;
	}
	return -EINVAL; /* Bad system function or access failure */
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
	if (!new_stack || !PTR_INRANGE(new_stack)) return -EINVAL;
	if (!thread_func || !PTR_INRANGE(thread_func)) return -EINVAL;
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
	if (process < -1) {
		return group_send_signal(-process, signal, 0);
	} else if (process == 0) {
		return group_send_signal(current_process->job, signal, 0);
	} else {
		return send_signal(process, signal, 0);
	}
}

static int sys_gettimeofday(struct timeval * tv, void * tz) {
	PTR_VALIDATE(tv);
	PTR_VALIDATE(tz);

	return gettimeofday(tv, tz);
}

static int sys_openpty(int * master, int * slave, char * name, void * _ign0, void * size) {
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
	*master = process_append_fd((process_t *)current_process, fs_master);
	*slave  = process_append_fd((process_t *)current_process, fs_slave);

	FD_MODE(*master) = 03;
	FD_MODE(*slave) = 03;

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
	FD_MODE(pipes[0]) = 03;
	FD_MODE(pipes[1]) = 03;
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

static int sys_fswait(int c, int fds[]) {
	PTR_VALIDATE(fds);
	for (int i = 0; i < c; ++i) {
		if (!FD_CHECK(fds[i])) return -EBADF;
	}
	fs_node_t ** nodes = malloc(sizeof(fs_node_t *)*(c+1));
	for (int i = 0; i < c; ++i) {
		nodes[i] = FD_ENTRY(fds[i]);
	}
	nodes[c] = NULL;

	int result = process_wait_nodes((process_t *)current_process, nodes, -1);
	free(nodes);
	return result;
}

static int sys_fswait_timeout(int c, int fds[], int timeout) {
	PTR_VALIDATE(fds);
	for (int i = 0; i < c; ++i) {
		if (!FD_CHECK(fds[i])) return -EBADF;
	}
	fs_node_t ** nodes = malloc(sizeof(fs_node_t *)*(c+1));
	for (int i = 0; i < c; ++i) {
		nodes[i] = FD_ENTRY(fds[i]);
	}
	nodes[c] = NULL;

	int result = process_wait_nodes((process_t *)current_process, nodes, timeout);
	free(nodes);
	return result;
}

static int sys_fswait_multi(int c, int fds[], int timeout, int out[]) {
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
	if (result != -1) out[result] = 1;
	if (result == -1) {
		debug_print(ERROR,"negative result from fswait3");
	}
	return result;
}

static int sys_setsid(void) {
	if (current_process->job == current_process->group) {
		return -EPERM;
	}
	current_process->session = current_process->group;
	current_process->job = current_process->group;
	return current_process->session;
}

static int sys_setpgid(pid_t pid, pid_t pgid) {
	if (pgid < 0) {
		return -EINVAL;
	}
	process_t * proc;
	if (pid == 0) {
		proc = (process_t*)current_process;
	} else {
		proc = process_from_pid(pid);
	}
	if (!proc) {
		debug_print(WARNING, "not found");
		return -ESRCH;
	}
	if (proc->session != current_process->session) {
		debug_print(WARNING, "child is in different sesion");
		return -EPERM;
	}
	if (proc->session == proc->group) {
		debug_print(WARNING, "process is session leader");
		return -EPERM;
	}

	if (pgid == 0) {
		proc->job = proc->group;
	} else {
		process_t * pgroup = process_from_pid(pgid);

		if (!pgroup) {
			debug_print(WARNING, "bad session id");
			return -EPERM;
		}

		if (pgroup->session != proc->session) {
			debug_print(WARNING, "tried to move to different session");
			return -EPERM;
		}

		proc->job = pgid;
	}
	return 0;
}

static int sys_getpgid(pid_t pid) {
	process_t * proc;
	if (pid == 0) {
		proc = (process_t*)current_process;
	} else {
		proc = process_from_pid(pid);
	}

	if (!proc) {
		return -ESRCH;
	}

	return proc->job;
}

/*
 * System Call Internals
 */
static int (*syscalls[])() = {
	/* System Call Table */
	[SYS_EXT]          = sys_exit,
	[SYS_GETEUID]      = sys_geteuid,
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
	[SYS_FSWAIT]       = sys_fswait,
	[SYS_FSWAIT2]      = sys_fswait_timeout,
	[SYS_FSWAIT3]      = sys_fswait_multi,
	[SYS_CHOWN]        = sys_chown,
	[SYS_SETSID]       = sys_setsid,
	[SYS_SETPGID]      = sys_setpgid,
	[SYS_GETPGID]      = sys_getpgid,
};

uint32_t num_syscalls = sizeof(syscalls) / sizeof(*syscalls);

typedef uint32_t (*scall_func)(unsigned int, ...);

pid_t trace_pid = 0;

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

	if (trace_pid && current_process->id == trace_pid) {
		debug_print(WARNING, "[syscall trace] %d (0x%x) 0x%x 0x%x 0x%x 0x%x 0x%x", r->eax, location, r->ebx, r->ecx, r->edx, r->esi, r->edi);
	}

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

