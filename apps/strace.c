/**
 * @brief Process system call tracer.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021-2026 K. Lange
 */
#include <stdbool.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <dirent.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/signal.h>
#include <sys/signal_defs.h>
#include <sys/utsname.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/uregs.h>
#include <sys/resource.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <arpa/inet.h>
#include <netinet/in.h>

static FILE * logfile;
static bool log_hidden = true;
static bool follow_forks = false;
static pid_t unfinished_child = 0;

struct Pid {
	struct Pid *next;
	struct Pid *prev;
	pid_t pid;
	int previous_syscall;
	bool unfinished;
};

static struct Pid *children = NULL;
static size_t children_count = 0;

static struct Pid * find_pid(pid_t p) {
	struct Pid * c = children;
	while (c) {
		if (c->pid == p) return c;
		c = c->next;
	}
	return NULL;
}

static int forget_child(struct Pid * child) {
	if (child->prev) child->prev->next = child->next;
	if (child->next) child->next->prev = child->prev;
	if (child == children) children = child->next;
	children_count--;

	ptrace(PTRACE_DETACH, child->pid, NULL, NULL);
	free(child);

	return children_count == 0;
}

static void interrupt_log(pid_t pid) {
	struct Pid * c;
	if (unfinished_child && unfinished_child != pid) {
		fprintf(logfile, "/* unfinished */\n");
		if ((c = find_pid(unfinished_child))) c->unfinished = true;
	}
	unfinished_child = 0;
}

static void report_pid(struct Pid *child) {
	interrupt_log(child->pid);
	if (children_count > 1) {
		fprintf(logfile, "[pid %d] ", child->pid);
	}
}


/* System call names */
const char * syscall_names[] = {
	[SYS_EXT]          = "exit",
	[SYS_GETEUID]      = "geteuid",
	[SYS_OPEN]         = "open",
	[SYS_READ]         = "read",
	[SYS_WRITE]        = "write",
	[SYS_CLOSE]        = "close",
	[SYS_GETTIMEOFDAY] = "gettimeofday",
	[SYS_GETPID]       = "getpid",
	[SYS_SBRK]         = "sbrk",
	[SYS_UNAME]        = "uname",
	[SYS_SEEK]         = "seek",
	[SYS_STAT]         = "stat",
	[SYS_GETUID]       = "getuid",
	[SYS_SETUID]       = "setuid",
	[SYS_READDIR]      = "readdir",
	[SYS_CHDIR]        = "chdir",
	[SYS_GETCWD]       = "getcwd",
	[SYS_SETHOSTNAME]  = "sethostname",
	[SYS_GETHOSTNAME]  = "gethostname",
	[SYS_MKDIR]        = "mkdir",
	[SYS_GETTID]       = "gettid",
	[SYS_IOCTL]        = "ioctl",
	[SYS_ACCESS]       = "access",
	[SYS_EACCESS]      = "eaccess",
	[SYS_STATF]        = "statf",
	[SYS_CHMOD]        = "chmod",
	[SYS_UMASK]        = "umask",
	[SYS_UNLINK]       = "unlink",
	[SYS_MOUNT]        = "mount",
	[SYS_SYMLINK]      = "symlink",
	[SYS_READLINK]     = "readlink",
	[SYS_LSTAT]        = "lstat",
	[SYS_CHOWN]        = "chown",
	[SYS_SETSID]       = "setsid",
	[SYS_SETPGID]      = "setpgid",
	[SYS_GETPGID]      = "getpgid",
	[SYS_DUP2]         = "dup2",
	[SYS_DUP3]         = "dup3",
	[SYS_EXECVE]       = "execve",
	[SYS_FORK]         = "fork",
	[SYS_WAITPID]      = "waitpid",
	[SYS_YIELD]        = "yield",
	[SYS_SLEEPABS]     = "sleepabs",
	[SYS_SLEEP]        = "sleep",
	[SYS_PIPE]         = "pipe",
	[SYS_FSWAIT]       = "fswait",
	[SYS_FSWAIT2]      = "fswait_timeout",
	[SYS_FSWAIT3]      = "fswait_multi",
	[SYS_CLONE]        = "clone",
	[SYS_OPENPTY]      = "openpty",
	[SYS_SHM_OBTAIN]   = "shm_obtain",
	[SYS_SHM_RELEASE]  = "shm_release",
	[SYS_SIGNAL]       = "signal",
	[SYS_KILL]         = "kill",
	[SYS_REBOOT]       = "reboot",
	[SYS_GETGID]       = "getgid",
	[SYS_GETEGID]      = "getegid",
	[SYS_SETGID]       = "setgid",
	[SYS_GETGROUPS]    = "getgroups",
	[SYS_SETGROUPS]    = "setgroups",
	[SYS_TIMES]        = "times",
	[SYS_PTRACE]       = "ptrace",
	[SYS_SOCKET]       = "socket",
	[SYS_SETSOCKOPT]   = "setsockopt",
	[SYS_BIND]         = "bind",
	[SYS_ACCEPT]       = "accept",
	[SYS_LISTEN]       = "listen",
	[SYS_CONNECT]      = "connect",
	[SYS_GETSOCKOPT]   = "getsockopt",
	[SYS_RECV]         = "recv",
	[SYS_SEND]         = "send",
	[SYS_SHUTDOWN]     = "shutdown",
	[SYS_SIGACTION]    = "sigaction",
	[SYS_SIGPENDING]   = "sigpending",
	[SYS_SIGPROCMASK]  = "sigprocmask",
	[SYS_SIGSUSPEND]   = "sigsuspend",
	[SYS_SIGWAIT]      = "sigwait",
	[SYS_PREAD]        = "pread",
	[SYS_PWRITE]       = "pwrite",
	[SYS_RENAME]       = "rename",
	[SYS_FCNTL]        = "fcntl",
	[SYS_FCHMOD]       = "fchmod",
	[SYS_FCHOWN]       = "fchown",
	[SYS_TRUNCATE]     = "truncate",
	[SYS_FTRUNCATE]    = "ftruncate",
	[SYS_SETTIMEOFDAY] = "settimeofday",
	[SYS_GETSOCKNAME]  = "getsockname",
	[SYS_GETPEERNAME]  = "getpeername",
	[SYS_GETPPID]      = "getppid",
	[SYS_LCHOWN]       = "lchown",
	[SYS_GETRUSAGE]    = "getrusage",
	[SYS_PIPE2]        = "pipe2",
	[SYS_SIGQUEUE]     = "sigqueue",
	[SYS_SETRESUID]    = "setresuid",
	[SYS_SETREUID]     = "setreuid",
	[SYS_SETRESGID]    = "setresgid",
	[SYS_SETREGID]     = "setregid",
	[SYS_MMAP]         = "mmap",
	[SYS_MUNMAP]       = "munmap",
	[SYS_NPROC]        = "nproc",
	[SYS_SETTLSBASE]   = "set_tls_base",
	[SYS_INSMOD]       = "insmod",
};

char syscall_mask[] = {
	[SYS_EXT]          = 1,
	[SYS_GETEUID]      = 1,
	[SYS_OPEN]         = 1,
	[SYS_READ]         = 1,
	[SYS_WRITE]        = 1,
	[SYS_CLOSE]        = 1,
	[SYS_GETTIMEOFDAY] = 1,
	[SYS_GETPID]       = 1,
	[SYS_SBRK]         = 1,
	[SYS_UNAME]        = 1,
	[SYS_SEEK]         = 1,
	[SYS_STAT]         = 1,
	[SYS_GETUID]       = 1,
	[SYS_SETUID]       = 1,
	[SYS_READDIR]      = 1,
	[SYS_CHDIR]        = 1,
	[SYS_GETCWD]       = 1,
	[SYS_SETHOSTNAME]  = 1,
	[SYS_GETHOSTNAME]  = 1,
	[SYS_MKDIR]        = 1,
	[SYS_GETTID]       = 1,
	[SYS_IOCTL]        = 1,
	[SYS_ACCESS]       = 1,
	[SYS_EACCESS]      = 1,
	[SYS_STATF]        = 1,
	[SYS_CHMOD]        = 1,
	[SYS_UMASK]        = 1,
	[SYS_UNLINK]       = 1,
	[SYS_MOUNT]        = 1,
	[SYS_SYMLINK]      = 1,
	[SYS_READLINK]     = 1,
	[SYS_LSTAT]        = 1,
	[SYS_CHOWN]        = 1,
	[SYS_SETSID]       = 1,
	[SYS_SETPGID]      = 1,
	[SYS_GETPGID]      = 1,
	[SYS_DUP2]         = 1,
	[SYS_DUP3]         = 1,
	[SYS_EXECVE]       = 1,
	[SYS_FORK]         = 1,
	[SYS_WAITPID]      = 1,
	[SYS_YIELD]        = 1,
	[SYS_SLEEPABS]     = 1,
	[SYS_SLEEP]        = 1,
	[SYS_PIPE]         = 1,
	[SYS_FSWAIT]       = 1,
	[SYS_FSWAIT2]      = 1,
	[SYS_FSWAIT3]      = 1,
	[SYS_CLONE]        = 1,
	[SYS_OPENPTY]      = 1,
	[SYS_SHM_OBTAIN]   = 1,
	[SYS_SHM_RELEASE]  = 1,
	[SYS_SIGNAL]       = 1,
	[SYS_KILL]         = 1,
	[SYS_REBOOT]       = 1,
	[SYS_GETGID]       = 1,
	[SYS_GETEGID]      = 1,
	[SYS_SETGID]       = 1,
	[SYS_GETGROUPS]    = 1,
	[SYS_SETGROUPS]    = 1,
	[SYS_TIMES]        = 1,
	[SYS_PTRACE]       = 1,
	[SYS_SOCKET]       = 1,
	[SYS_SETSOCKOPT]   = 1,
	[SYS_BIND]         = 1,
	[SYS_ACCEPT]       = 1,
	[SYS_LISTEN]       = 1,
	[SYS_CONNECT]      = 1,
	[SYS_GETSOCKOPT]   = 1,
	[SYS_RECV]         = 1,
	[SYS_SEND]         = 1,
	[SYS_SHUTDOWN]     = 1,
	[SYS_PREAD]        = 1,
	[SYS_PWRITE]       = 1,
	[SYS_SIGACTION]    = 1,
	[SYS_SIGPENDING]   = 1,
	[SYS_SIGPROCMASK]  = 1,
	[SYS_SIGSUSPEND]   = 1,
	[SYS_SIGWAIT]      = 1,
	[SYS_RENAME]       = 1,
	[SYS_FCNTL]        = 1,
	[SYS_FCHMOD]       = 1,
	[SYS_FCHOWN]       = 1,
	[SYS_TRUNCATE]     = 1,
	[SYS_FTRUNCATE]    = 1,
	[SYS_SETTIMEOFDAY] = 1,
	[SYS_GETSOCKNAME]  = 1,
	[SYS_GETPEERNAME]  = 1,
	[SYS_GETPPID]      = 1,
	[SYS_LCHOWN]       = 1,
	[SYS_GETRUSAGE]    = 1,
	[SYS_PIPE2]        = 1,
	[SYS_SIGQUEUE]     = 1,
	[SYS_SETRESUID]    = 1,
	[SYS_SETREUID]     = 1,
	[SYS_SETRESGID]    = 1,
	[SYS_SETREGID]     = 1,
	[SYS_MMAP]         = 1,
	[SYS_MUNMAP]       = 1,
	[SYS_NPROC]        = 1,
	[SYS_SETTLSBASE]   = 1,
	[SYS_INSMOD]       = 1,
};

static const int syscall_set_net[] = {
	SYS_SOCKET, SYS_SETSOCKOPT, SYS_BIND, SYS_ACCEPT, SYS_LISTEN,
	SYS_CONNECT, SYS_GETSOCKOPT, SYS_RECV, SYS_SEND, SYS_SHUTDOWN,
	SYS_GETPEERNAME, SYS_GETSOCKNAME, -1
};

static const int syscall_set_file[] = {
	SYS_OPEN, SYS_STATF, SYS_LSTAT, SYS_ACCESS, SYS_EXECVE,
	SYS_GETCWD, SYS_CHDIR, SYS_MKDIR, SYS_SYMLINK, SYS_UNLINK,
	SYS_CHMOD, SYS_CHOWN, SYS_MOUNT, SYS_READLINK, SYS_RENAME,
	SYS_TRUNCATE, SYS_EACCESS, SYS_LCHOWN, -1
};

static const int syscall_set_desc[] = {
	SYS_OPEN, SYS_READ, SYS_WRITE, SYS_CLOSE, SYS_STAT, SYS_FSWAIT,
	SYS_FSWAIT2, SYS_FSWAIT3, SYS_SEEK, SYS_IOCTL, SYS_PIPE, SYS_PIPE2,
	SYS_DUP2, SYS_READDIR, SYS_OPENPTY, SYS_PREAD, SYS_PWRITE, SYS_FCNTL,
	SYS_FCHMOD, SYS_FCHOWN, SYS_FTRUNCATE, SYS_DUP3, SYS_INSMOD, -1
};

static const int syscall_set_memory[] = {
	SYS_SBRK, SYS_SHM_OBTAIN, SYS_SHM_RELEASE, SYS_MMAP, SYS_MUNMAP, -1
};

static const int syscall_set_ipc[] = {
	SYS_SHM_OBTAIN, SYS_SHM_RELEASE, -1
};

static const int syscall_set_signal[] = {
	SYS_SIGNAL, SYS_KILL, SYS_SIGACTION, SYS_SIGPENDING, SYS_SIGPROCMASK,
	SYS_SIGSUSPEND, SYS_SIGWAIT, SYS_SIGQUEUE, -1
};

static const int syscall_set_process[] = {
	SYS_EXT, SYS_EXECVE, SYS_FORK, SYS_CLONE, SYS_WAITPID, SYS_KILL,
	SYS_SIGQUEUE, -1
};

static const int syscall_set_creds[] = {
	SYS_GETUID, SYS_GETGID, SYS_GETGROUPS, SYS_GETEGID, SYS_GETEUID,
	SYS_SETUID, SYS_SETGID, SYS_SETGROUPS, SYS_SETRESUID, SYS_SETREUID,
	SYS_SETRESGID, SYS_SETREGID, -1
};

static const int syscall_set_stat[] = {
	SYS_STAT, SYS_STATF, SYS_LSTAT, -1
};

static const int syscall_set_clock[] = {
	SYS_GETTIMEOFDAY, SYS_SETTIMEOFDAY, -1
};

struct SyscallSet {
	const char * name;
	const int * syscalls;
};

static const struct SyscallSet syscall_sets[] = {
	{"net",     syscall_set_net},
	{"network", syscall_set_net}, /* Alias */
	{"file",    syscall_set_file},
	{"desc",    syscall_set_desc},
	{"memory",  syscall_set_memory},
	{"ipc",     syscall_set_ipc},
	{"signal",  syscall_set_signal},
	{"process", syscall_set_process},
	{"creds",   syscall_set_creds},
	{"stat",    syscall_set_stat},
	{"clock",   syscall_set_clock},
	{NULL, NULL}
};

#define M(e) [e] = #e
const char * errno_names[] = {
	M(EPERM),
	M(ENOENT),
	M(ESRCH),
	M(EINTR),
	M(EIO),
	M(ENXIO),
	M(E2BIG),
	M(ENOEXEC),
	M(EBADF),
	M(ECHILD),
	M(EAGAIN),
	M(ENOMEM),
	M(EACCES),
	M(EFAULT),
	M(ENOTBLK),
	M(EBUSY),
	M(EEXIST),
	M(EXDEV),
	M(ENODEV),
	M(ENOTDIR),
	M(EISDIR),
	M(EINVAL),
	M(ENFILE),
	M(EMFILE),
	M(ENOTTY),
	M(ETXTBSY),
	M(EFBIG),
	M(ENOSPC),
	M(ESPIPE),
	M(EROFS),
	M(EMLINK),
	M(EPIPE),
	M(EDOM),
	M(ERANGE),
	M(ENOMSG),
	M(EIDRM),
	M(ECHRNG),
	M(EL2NSYNC),
	M(EL3HLT),
	M(EL3RST),
	M(ELNRNG),
	M(EUNATCH),
	M(ENOCSI),
	M(EL2HLT),
	M(EDEADLK),
	M(ENOLCK),
	M(EBADE),
	M(EBADR),
	M(EXFULL),
	M(ENOANO),
	M(EBADRQC),
	M(EBADSLT),
	M(EDEADLOCK),
	M(EBFONT),
	M(ENOSTR),
	M(ENODATA),
	M(ETIME),
	M(ENOSR),
	M(ENONET),
	M(ENOPKG),
	M(EREMOTE),
	M(ENOLINK),
	M(EADV),
	M(ESRMNT),
	M(ECOMM),
	M(EPROTO),
	M(EMULTIHOP),
	M(ELBIN),
	M(EDOTDOT),
	M(EBADMSG),
	M(EFTYPE),
	M(ENOTUNIQ),
	M(EBADFD),
	M(EREMCHG),
	M(ELIBACC),
	M(ELIBBAD),
	M(ELIBSCN),
	M(ELIBMAX),
	M(ELIBEXEC),
	M(ENOSYS),
	M(ENOTEMPTY),
	M(ENAMETOOLONG),
	M(ELOOP),
	M(EOPNOTSUPP),
	M(EPFNOSUPPORT),
	M(ECONNRESET),
	M(ENOBUFS),
	M(EAFNOSUPPORT),
	M(EPROTOTYPE),
	M(ENOTSOCK),
	M(ENOPROTOOPT),
	M(ESHUTDOWN),
	M(ECONNREFUSED),
	M(EADDRINUSE),
	M(ECONNABORTED),
	M(ENETUNREACH),
	M(ENETDOWN),
	M(ETIMEDOUT),
	M(EHOSTDOWN),
	M(EHOSTUNREACH),
	M(EINPROGRESS),
	M(EALREADY),
	M(EDESTADDRREQ),
	M(EMSGSIZE),
	M(EPROTONOSUPPORT),
	M(ESOCKTNOSUPPORT),
	M(EADDRNOTAVAIL),
	M(EISCONN),
	M(ENOTCONN),
	M(ENOTSUP),
	M(EOVERFLOW),
	M(ECANCELED),
	M(ENOTRECOVERABLE),
	M(EOWNERDEAD),
	M(ESTRPIPE),
	M(ERESTARTSYS),
	M(ERESTARTSIGSUSPEND),
};

const char * fcntl_cmd_names[] = {
	M(F_GETFD),
	M(F_SETFD),
	M(F_GETFL),
	M(F_SETFL),
	M(F_DUPFD),
	M(F_GETLK),
	M(F_SETLK),
	M(F_SETLKW),
	M(F_DUPFD_CLOEXEC),
	M(F_DUPFD_CLOFORK),
};

static void open_flags(int flags) {
	if (!flags) {
		fprintf(logfile, "O_RDONLY");
		return;
	}

#define H(flg) do { if (flags & flg) { fprintf(logfile, #flg); flags &= (~flg); if (flags) fprintf(logfile, "|"); } } while (0)

	H(O_WRONLY);
	H(O_RDWR);
	H(O_APPEND);
	H(O_CREAT);
	H(O_TRUNC);
	H(O_EXCL);
	H(O_NOFOLLOW);
	H(O_PATH);
	H(O_NONBLOCK);
	H(O_DIRECTORY);
	H(O_CLOEXEC);
	H(O_CLOFORK);

	if (flags) {
		fprintf(logfile, "(%#x)", flags);
	}
}

static void one_char(uint8_t buf) {
	if (buf == '\\') fprintf(logfile, "\\\\");
	else if (buf == '"') fprintf(logfile, "\\\"");
	else if (buf >= ' ' && buf <= '~') fprintf(logfile, "%c", buf);
	else if (buf == '\r') fprintf(logfile, "\\r");
	else if (buf == '\n') fprintf(logfile, "\\n");
	else fprintf(logfile, "\\%o", buf);
}

static void string_arg(pid_t pid, uintptr_t ptr) {
	if (ptr == 0) {
		fprintf(logfile, "NULL");
		return;
	}

	fprintf(logfile, "\"");

	size_t size = 0;
	uint8_t buf = 0;

	do {
		long result = ptrace(PTRACE_PEEKDATA, pid, (void*)ptr, &buf);
		if (result != 0) break;
		if (!buf) {
			fprintf(logfile, "\"");
			return;
		}
		one_char(buf);
		ptr++;
		size++;
		if (size > 30) break;
	} while (buf);

	fprintf(logfile, "\"...");
}

#define C(arg) case arg: fprintf(logfile, #arg); break
#define COMMA fprintf(logfile, ", ");

static void pointer_arg(uintptr_t ptr) {
	if (ptr == 0) fprintf(logfile, "NULL");
	else fprintf(logfile, "%#zx", ptr);
}

static void uint_arg(size_t val) {
	fprintf(logfile, "%zu", val);
}

static void int_arg(size_t val) {
	fprintf(logfile, "%zd", val);
}

static void mode_arg(mode_t val) {
	fprintf(logfile, "%#03o", val);
}

static void fd_arg(pid_t pid, int val) {
	/* TODO: Look up file in user data? */
	fprintf(logfile, "%d", val);
}

static void sock_dom_arg(int domain) {
	switch (domain) {
		C(AF_UNSPEC);
		C(AF_RAW);
		C(AF_INET);
		default: fprintf(logfile, "%d", domain); break;
	}
}

static void sock_typ_arg(int type) {
	switch (type) {
		C(SOCK_DGRAM);
		C(SOCK_STREAM);
		C(SOCK_RAW);
		default: fprintf(logfile, "%d", type); break;
	}
}

static void sock_pro_arg(int domain, int type, int proto) {
	switch (domain) {
		case AF_INET:
			switch (proto) {
				C(IPPROTO_IP);
				C(IPPROTO_ICMP);
				C(IPPROTO_TCP);
				C(IPPROTO_UDP);
				default:
					fprintf(logfile, "%d", proto);
					break;
			}
			return;
	}

	/* Fallback case */
	fprintf(logfile, "%d", proto);
}

static void sock_lvl_arg(int level) {
	switch (level) {
		C(SOL_SOCKET);
		default: fprintf(logfile, "%d", level); break;
	}
}

static void sock_opt_arg(int opt) {
	switch (opt) {
		C(SO_KEEPALIVE);
		C(SO_REUSEADDR);
		C(SO_BINDTODEVICE);
		C(SO_RCVTIMEO);
		default: fprintf(logfile, "%d", opt); break;
	}
}

static int data_read_bytes(pid_t pid, uintptr_t addr, char * buf, size_t size) {
	for (unsigned int i = 0; i < size; ++i) {
		if (ptrace(PTRACE_PEEKDATA, pid, (void*)addr++, &buf[i])) {
			return 1;
		}
	}
	return 0;
}

static int data_read_int(pid_t pid, uintptr_t addr) {
	int x;
	data_read_bytes(pid, addr, (char*)&x, sizeof(int));
	return x;
}

static uintptr_t data_read_ptr(pid_t pid, uintptr_t addr) {
	uintptr_t x;
	data_read_bytes(pid, addr, (char*)&x, sizeof(uintptr_t));
	return x;
}

static void sockaddr_arg(pid_t pid, uintptr_t addr, size_t size) {
	if (addr == 0) {
		fprintf(logfile, "null");
		return;
	}

	struct sockaddr_storage sa = {0};
	data_read_bytes(pid, addr, (char*)&sa, size < sizeof(struct sockaddr_storage) ? size : sizeof(struct sockaddr_storage));

	fprintf(logfile, "{sa_family=");
	sock_dom_arg(sa.ss_family);

	if (sa.ss_family == AF_INET && size >= sizeof(struct sockaddr_in)) {
		struct sockaddr_in addr;
		memcpy(&addr, &sa, sizeof(struct sockaddr_in));
		fprintf(logfile, ", sin_port=htons(%d), sin_addr=inet_addr(\"%s\")", ntohs(addr.sin_port), inet_ntoa(addr.sin_addr));
	}

	fprintf(logfile, "}");
}

static void sockaddrp_arg(pid_t pid, uintptr_t addr, uintptr_t size_p) {
	size_t size = 0;
	data_read_bytes(pid, size_p, (char*)&size, sizeof(size_t));
	sockaddr_arg(pid,addr,size);
}


static void fds_arg(pid_t pid, size_t ecount, uintptr_t array) {
	fprintf(logfile, "[");
	for (size_t count = 0; count < 10 && count < ecount; ++count) {
		int x = data_read_int(pid, array);
		fprintf(logfile, "%d", x);
		if (count + 1 < ecount) fprintf(logfile, ",");
		array += sizeof(int);
	}
	fprintf(logfile, "]");
}

static void string_array_arg(pid_t pid, uintptr_t array) {
	fprintf(logfile, "[");
	uintptr_t val = data_read_ptr(pid, array);
	for (size_t count = 0; count < 10; ++count) {
		string_arg(pid, val);
		array += sizeof(uintptr_t);
		val = data_read_ptr(pid, array);
		if (val) { COMMA; }
		else break;
	}
	fprintf(logfile, "]");
}

static void envp_arg(pid_t pid, uintptr_t array) {
	pointer_arg(array);

	/* try to count them */
	size_t count = 0;
	while (data_read_ptr(pid, array)) {
		count += 1;
		array += sizeof(uintptr_t);
	}

	if (count) {
		fprintf(logfile, " /* %zu var%s */", count, "s" + (count==1));
	}
}

static void buffer_arg(pid_t pid, uintptr_t buffer, ssize_t count) {
	if (count < 0) {
		fprintf(logfile, "...");
	} else if (buffer == 0) {
		fprintf(logfile, "NULL");
	} else {
		ssize_t x = 0;
		uint8_t buf = 0;
		fprintf(logfile, "\"");
		while (x < count && x < 30) {
			long result = ptrace(PTRACE_PEEKDATA, pid, (void*)buffer, &buf);
			if (result != 0) break;
			one_char(buf);
			buffer++;
			x++;
		}
		fprintf(logfile, "\"");
		if (x < count) fprintf(logfile, "...");
	}
}

static void msghdr_arg(pid_t pid, uintptr_t msghdr) {
	struct msghdr data = {0};
	if (data_read_bytes(pid, msghdr, (char*)&data, sizeof(struct msghdr))) {
		fprintf(logfile, "(?)");
	} else {
		fprintf(logfile, "{msg_name=%#zx,msg_iovlen=%zu,msg_iov[0]=", (uintptr_t)data.msg_name, data.msg_iovlen);
		if (data.msg_iovlen > 0) {
			struct iovec iodata = {0};
			if (data_read_bytes(pid, (uintptr_t)data.msg_iov, (char*)&iodata, sizeof(struct iovec))) {
				fprintf(logfile,"?");
			} else {
				fprintf(logfile,"{iov_base=%#zx,iov_len=%zu}", (uintptr_t)iodata.iov_base, iodata.iov_len);
			}
		}
		fprintf(logfile,"}");
	}
}

static void fcntl_cmd_arg(long cmd) {
	const char * name = (cmd >= 0 && (size_t)cmd < (sizeof(fcntl_cmd_names) / sizeof(*fcntl_cmd_names))) ? fcntl_cmd_names[cmd] : NULL;
	if (name) {
		fprintf(logfile, "%s", name);
	} else {
		fprintf(logfile, "%ld", cmd);
	}
}

static void print_error(int err) {
	const char * name = (err >= 0 && (size_t)err < (sizeof(errno_names) / sizeof(*errno_names))) ? errno_names[err] : NULL;
	if (name) {
		fprintf(logfile, " %s (%s)", name, strerror(err));
	} else {
		fprintf(logfile, " %d (%s)", err, strerror(err));
	}
}

static void maybe_errno(struct URegs * r) {
	fprintf(logfile, ") = %ld", uregs_syscall_result(r));
	if ((intptr_t)uregs_syscall_result(r) < 0) print_error(-uregs_syscall_result(r));
	fprintf(logfile, "\n");
}

static void struct_utsname_arg(pid_t pid, uintptr_t ptr) {
	if (!ptr) {
		fprintf(logfile, "NULL");
		return;
	}

	fprintf(logfile, "{");
	fprintf(logfile, "sysname=");
	string_arg(pid, ptr + offsetof(struct utsname, sysname));
	COMMA;
	fprintf(logfile, "nodename=");
	string_arg(pid, ptr + offsetof(struct utsname, nodename));
	COMMA;
	fprintf(logfile, "...}");
}

static void struct_timeval_arg(pid_t pid, uintptr_t ptr) {
	if (!ptr) {
		fprintf(logfile, "NULL");
		return;
	}

	fprintf(logfile, "{");
	fprintf(logfile, "tv_sec=");
	int_arg(data_read_ptr(pid, ptr + offsetof(struct timeval, tv_sec)));
	COMMA;
	fprintf(logfile, "tv_usec=");
	int_arg(data_read_ptr(pid, ptr + offsetof(struct timeval, tv_usec)));
	fprintf(logfile, "}");
}

static void struct_rusage_arg(pid_t pid, uintptr_t ptr) {
	if (!ptr) {
		fprintf(logfile, "NULL");
		return;
	}

	fprintf(logfile, "{ru_utime=");
	struct_timeval_arg(pid, ptr + offsetof(struct rusage, ru_utime));
	COMMA;
	fprintf(logfile, "ru_stime=");
	struct_timeval_arg(pid, ptr + offsetof(struct rusage, ru_stime));
	fprintf(logfile, "}");
}

static void struct_stat_arg(pid_t pid, uintptr_t ptr) {
	if (!ptr) {
		fprintf(logfile, "NULL");
		return;
	}

	fprintf(logfile, "{");

	fprintf(logfile, "st_mode=");
	mode_t mode = data_read_int(pid, ptr + offsetof(struct stat, st_mode));
	if (mode & S_IFMT) {
		switch (mode & S_IFMT) {
			C(S_IFDIR);
			C(S_IFCHR);
			C(S_IFBLK);
			C(S_IFREG);
			C(S_IFLNK);
			C(S_IFSOCK);
			C(S_IFIFO);
			default: fprintf(logfile,"%0o",mode & S_IFMT);
		}
		fprintf(logfile, "|");
	}

	int flags;
	if ((flags = (mode & (S_ISUID|S_ISGID|S_ISVTX)))) {
		H(S_ISUID);
		H(S_ISGID);
		H(S_ISVTX);
		if (mode & 0777) fprintf(logfile,"|");
	}
	mode_arg(mode & 0777);
	COMMA;

	fprintf(logfile, "st_size=");
	uint_arg(data_read_ptr(pid, ptr + offsetof(struct stat, st_size)));
	COMMA;

	fprintf(logfile, "...}");
}

static void struct_dirent_arg(pid_t pid, uintptr_t ptr) {
	if (!ptr) {
		fprintf(logfile, "NULL");
		return;
	}

	fprintf(logfile, "{");

	fprintf(logfile, "d_ino=");
	uint_arg(data_read_ptr(pid, ptr + offsetof(struct dirent, d_ino)));
	COMMA;

	fprintf(logfile, "d_name=");
	string_arg(pid, ptr + offsetof(struct dirent, d_name));

	fprintf(logfile, "}");
}

static void signal_arg(int signum) {
	char signame[SIG2STR_MAX] = {0};

	if (signum != 0 && !sig2str(signum, signame)) {
		fprintf(logfile, "SIG%s", signame);
	} else {
		fprintf(logfile, "%d", signum);
	}
}

static void sigset_arg(uint64_t sigset) {
	/* handle special cases */
	if (sigset == 0xFFFFffffFFFFffff) {
		fprintf(logfile, "sigfillset()");
		return;
	} else if (sigset == 0) {
		fprintf(logfile, "sigemptyset()");
		return;
	}

	uint64_t x = (1ULL << NUMSIGNALS) - 2; /* 0 is also unused */
	sigset &= x;

	fprintf(logfile, "{");
	for (int i = 1; i < NUMSIGNALS; ++i) {
		uint64_t s = 1ULL << i;
		if (sigset & s) {
			signal_arg(i);
			sigset &= ~s;
			if (sigset) {
				fprintf(logfile, "|");
			}
		}
	}
	fprintf(logfile, "}");
}

static void sigset_ptr_arg(pid_t pid, uintptr_t ptr) {
	if (!ptr) {
		fprintf(logfile, "NULL");
		return;
	}

	uint64_t sigset = data_read_ptr(pid, ptr);

	sigset_arg(sigset);
}

static void sigaction_ptr_arg(pid_t pid, uintptr_t ptr) {
	if (!ptr) {
		fprintf(logfile, "NULL");
		return;
	}

	struct sigaction sa = {0};
	data_read_bytes(pid, ptr, (char*)&sa, sizeof(struct sigaction));

	fprintf(logfile, "{sa_flags=");

	int flags = sa.sa_flags;
	if (!flags) fprintf(logfile,"0");
	else {
		H(SA_NOCLDSTOP);
		H(SA_SIGINFO);
		H(SA_NODEFER);
		H(SA_RESETHAND);
		H(SA_RESTART);
		if (flags) fprintf(logfile,"%#x",flags);
	}

	fprintf(logfile,",sa_mask=");
	sigset_arg(sa.sa_mask);
	fprintf(logfile,",sa_handler=");

	if (sa.sa_handler == SIG_DFL) fprintf(logfile,"SIG_DFL");
	else if (sa.sa_handler == SIG_IGN) fprintf(logfile,"SIG_IGN");
	else if (sa.sa_handler == SIG_ERR) fprintf(logfile,"SIG_ERR");
	else pointer_arg((uintptr_t)sa.sa_handler);

	fprintf(logfile,"}");
}

static void signal_ptr_arg(pid_t pid, uintptr_t ptr) {
	int i = data_read_int(pid, ptr);
	fprintf(logfile, "{");
	signal_arg(i);
	fprintf(logfile, "}");
}

static void mmap_prot_arg(int flags) {
	if (!flags) fprintf(logfile,"PROT_NONE");
	else {
		H(PROT_READ);
		H(PROT_WRITE);
		H(PROT_EXEC);
		if (flags) fprintf(logfile,"%#x",flags);
	}
}

static void mmap_flags_arg(int flags) {
	if (!flags) fprintf(logfile,"0");
	else {
		H(MAP_SHARED);
		H(MAP_PRIVATE);
		H(MAP_FIXED);
		H(MAP_ANONYMOUS);
		if (flags) fprintf(logfile,"%#x",flags);
	}
}

static void access_mode_arg(int flags) {
	if (!flags) fprintf(logfile,"F_OK");
	else {
		H(X_OK);
		H(W_OK);
		H(R_OK);
		if (flags) fprintf(logfile,"%#x",flags);
	}
}

static void wait_status_ptr_arg(pid_t pid, uintptr_t ptr) {
	if (!ptr) {
		pointer_arg(ptr);
		return;
	}

	int status = data_read_int(pid, ptr);
	fprintf(logfile, "{");

	if (WIFEXITED(status)) {
		fprintf(logfile, "WIFEXITED, WEXITSTATUS=%d", WEXITSTATUS(status));
	} else if (WIFSIGNALED(status)) {
		fprintf(logfile, "WIFSIGNALED, WTERMSIG=");
		signal_arg(WTERMSIG(status));
	} else if (WIFSTOPPED(status)) {
		fprintf(logfile, "WIFSTOPPED, WSTOPSIG=");
		signal_arg(WSTOPSIG(status));
	}

	fprintf(logfile, "}");
}

static void wait_options_arg(int flags) {
	if (!flags) fprintf(logfile, "0");
	else {
		H(WNOHANG);
		H(WUNTRACED);
		H(WSTOPPED);
		H(WNOKERN);
		if (flags) fprintf(logfile, "%#x", flags);
	}
}

static void handle_syscall(struct Pid * child, pid_t pid, struct URegs * r) {
	if (uregs_syscall_num(r) >= sizeof(syscall_mask)) return;
	if (!syscall_mask[uregs_syscall_num(r)]) return;
	if (log_hidden) return;

	report_pid(child);

	fprintf(logfile, "%s(", syscall_names[uregs_syscall_num(r)]);
	switch (uregs_syscall_num(r)) {
		case SYS_OPEN:
			string_arg(pid, uregs_syscall_arg1(r)); COMMA;
			open_flags(uregs_syscall_arg2(r));
			if (uregs_syscall_arg2(r) & O_CREAT) {
				COMMA;
				mode_arg(uregs_syscall_arg3(r));
			}
			break;
		case SYS_CHMOD:
			string_arg(pid, uregs_syscall_arg1(r)); COMMA;
			mode_arg(uregs_syscall_arg2(r));
			break;
		case SYS_CHOWN:
		case SYS_LCHOWN:
			string_arg(pid, uregs_syscall_arg1(r)); COMMA;
			int_arg(uregs_syscall_arg2(r)); COMMA;
			int_arg(uregs_syscall_arg3(r));
			break;
		case SYS_TRUNCATE:
			string_arg(pid, uregs_syscall_arg1(r)); COMMA;
			int_arg(uregs_syscall_arg2(r));
			break;
		case SYS_READ:
			fd_arg(pid, uregs_syscall_arg1(r)); COMMA;
			/* Plus two more when done */
			break;
		case SYS_WRITE:
			fd_arg(pid, uregs_syscall_arg1(r)); COMMA;
			buffer_arg(pid, uregs_syscall_arg2(r), uregs_syscall_arg3(r)); COMMA;
			uint_arg(uregs_syscall_arg3(r));
			break;
		case SYS_PREAD:
			fd_arg(pid, uregs_syscall_arg1(r)); COMMA;
			/* Plus three more when done */
			break;
		case SYS_PWRITE:
			fd_arg(pid, uregs_syscall_arg1(r)); COMMA;
			buffer_arg(pid, uregs_syscall_arg2(r), uregs_syscall_arg3(r)); COMMA;
			uint_arg(uregs_syscall_arg3(r)); COMMA;
			int_arg(uregs_syscall_arg4(r));
			break;
		case SYS_CLOSE:
			fd_arg(pid, uregs_syscall_arg1(r));
			break;
		case SYS_SBRK:
			uint_arg(uregs_syscall_arg1(r));
			break;
		case SYS_SEEK:
			fd_arg(pid, uregs_syscall_arg1(r)); COMMA;
			int_arg(uregs_syscall_arg2(r)); COMMA;
			switch (uregs_syscall_arg3(r)) {
				C(SEEK_SET);
				C(SEEK_CUR);
				C(SEEK_END);
				default: int_arg(uregs_syscall_arg3(r)); break;
			}
			break;
		case SYS_STATF:
			string_arg(pid, uregs_syscall_arg1(r)); COMMA;
			/* Plus one more when done */
			break;
		case SYS_STAT:
			fd_arg(pid, uregs_syscall_arg1(r)); COMMA;
			/* Plus one more when done */
			break;
		case SYS_LSTAT:
			string_arg(pid, uregs_syscall_arg1(r)); COMMA;
			/* Plus one more when done */
			break;
		case SYS_READDIR:
			fd_arg(pid, uregs_syscall_arg1(r)); COMMA;
			int_arg(uregs_syscall_arg2(r)); COMMA;
			/* Plus one more when done */
			break;
		case SYS_KILL:
			int_arg(uregs_syscall_arg1(r)); COMMA; /* pid_arg? */
			signal_arg(uregs_syscall_arg2(r));
			break;
		case SYS_SIGQUEUE:
			int_arg(uregs_syscall_arg1(r)); COMMA; /* pid_arg? */
			signal_arg(uregs_syscall_arg2(r)); COMMA;
			uint_arg(uregs_syscall_arg3(r)); /* sigval, which is an untagged union */
			break;
		case SYS_CHDIR:
			string_arg(pid, uregs_syscall_arg1(r));
			break;
		case SYS_GETCWD:
			/* output is first arg */
			break;
		case SYS_CLONE:
			pointer_arg(uregs_syscall_arg1(r)); COMMA;
			pointer_arg(uregs_syscall_arg2(r)); COMMA;
			pointer_arg(uregs_syscall_arg3(r));
			break;
		case SYS_SETHOSTNAME:
			buffer_arg(pid, uregs_syscall_arg1(r), uregs_syscall_arg2(r)); COMMA; /* not a nul-terminated string in sethostname */
			uint_arg(uregs_syscall_arg2(r));
			break;
		case SYS_GETHOSTNAME:
			/* plus one more when done */
			break;
		case SYS_MKDIR:
			string_arg(pid, uregs_syscall_arg1(r)); COMMA;
			mode_arg(uregs_syscall_arg2(r));
			break;
		case SYS_RENAME:
			string_arg(pid, uregs_syscall_arg1(r)); COMMA;
			string_arg(pid, uregs_syscall_arg2(r));
			break;
		case SYS_ACCESS:
		case SYS_EACCESS:
			string_arg(pid, uregs_syscall_arg1(r)); COMMA;
			access_mode_arg(uregs_syscall_arg2(r));
			break;
		case SYS_PTRACE:
			switch (uregs_syscall_arg1(r)) {
				C(PTRACE_ATTACH);
				C(PTRACE_CONT);
				C(PTRACE_DETACH);
				C(PTRACE_TRACEME);
				C(PTRACE_GETREGS);
				C(PTRACE_PEEKDATA);
				default: int_arg(uregs_syscall_arg1(r)); break;
			} COMMA;
			int_arg(uregs_syscall_arg2(r)); COMMA;
			pointer_arg(uregs_syscall_arg3(r)); COMMA;
			pointer_arg(uregs_syscall_arg4(r));
			break;
		case SYS_EXECVE:
			string_arg(pid, uregs_syscall_arg1(r)); COMMA;
			string_array_arg(pid, uregs_syscall_arg2(r)); COMMA;
			envp_arg(pid, uregs_syscall_arg3(r));
			break;
		case SYS_SHM_OBTAIN:
			string_arg(pid, uregs_syscall_arg1(r)); COMMA;
			pointer_arg(uregs_syscall_arg2(r));
			break;
		case SYS_SHM_RELEASE:
			string_arg(pid, uregs_syscall_arg1(r));
			break;
		case SYS_SIGNAL:
			signal_arg(uregs_syscall_arg1(r)); COMMA;
			pointer_arg(uregs_syscall_arg2(r));
			break;
		case SYS_FSWAIT:
			int_arg(uregs_syscall_arg1(r)); COMMA;
			fds_arg(pid, uregs_syscall_arg1(r), uregs_syscall_arg2(r));
			break;
		case SYS_FSWAIT2:
			int_arg(uregs_syscall_arg1(r)); COMMA;
			fds_arg(pid, uregs_syscall_arg1(r), uregs_syscall_arg2(r)); COMMA;
			int_arg(uregs_syscall_arg3(r));
			break;
		case SYS_FSWAIT3:
			int_arg(uregs_syscall_arg1(r)); COMMA;
			fds_arg(pid, uregs_syscall_arg1(r), uregs_syscall_arg2(r)); COMMA;
			int_arg(uregs_syscall_arg3(r)); COMMA;
			pointer_arg(uregs_syscall_arg4(r));
			break;
		case SYS_IOCTL:
			fd_arg(pid, uregs_syscall_arg1(r)); COMMA;
			int_arg(uregs_syscall_arg2(r)); COMMA;
			pointer_arg(uregs_syscall_arg3(r));
			break;
		case SYS_WAITPID:
			int_arg(uregs_syscall_arg1(r)); COMMA;
			/* Plus two more */
			break;
		case SYS_EXT:
			int_arg(uregs_syscall_arg1(r));
			fprintf(logfile, ") = ?\n");
			return;
		case SYS_UNAME:
			/* One output arg */
			break;
		case SYS_SLEEPABS:
			uint_arg(uregs_syscall_arg1(r)); COMMA;
			uint_arg(uregs_syscall_arg2(r));
			break;
		case SYS_SLEEP:
			uint_arg(uregs_syscall_arg1(r)); COMMA;
			uint_arg(uregs_syscall_arg2(r));
			break;
		case SYS_PIPE:
			/* Arg is a pointer */
			break;
		case SYS_PIPE2:
			/* Output-filled pointer */
			break;
		case SYS_OPENPTY:
			/* Output-filled pointers */
			break;
		case SYS_DUP2:
			fd_arg(pid, uregs_syscall_arg1(r)); COMMA;
			fd_arg(pid, uregs_syscall_arg2(r));
			break;
		case SYS_DUP3:
			fd_arg(pid, uregs_syscall_arg1(r)); COMMA;
			fd_arg(pid, uregs_syscall_arg2(r)); COMMA;
			open_flags(uregs_syscall_arg3(r));
			break;
		case SYS_FCNTL:
			fd_arg(pid, uregs_syscall_arg1(r)); COMMA;
			fcntl_cmd_arg(uregs_syscall_arg2(r)); COMMA;
			int_arg(uregs_syscall_arg3(r));
			break;
		case SYS_FCHMOD:
			fd_arg(pid, uregs_syscall_arg1(r)); COMMA;
			mode_arg(uregs_syscall_arg2(r));
			break;
		case SYS_FCHOWN:
			fd_arg(pid, uregs_syscall_arg1(r)); COMMA;
			int_arg(uregs_syscall_arg2(r)); COMMA;
			int_arg(uregs_syscall_arg3(r));
			break;
		case SYS_FTRUNCATE:
			fd_arg(pid, uregs_syscall_arg1(r)); COMMA;
			int_arg(uregs_syscall_arg2(r));
			break;
		case SYS_MOUNT:
			string_arg(pid, uregs_syscall_arg1(r)); COMMA;
			string_arg(pid, uregs_syscall_arg2(r)); COMMA;
			uint_arg(uregs_syscall_arg3(r)); COMMA;
			pointer_arg(uregs_syscall_arg4(r));
			break;
		case SYS_UMASK:
			mode_arg(uregs_syscall_arg1(r));
			break;
		case SYS_UNLINK:
			string_arg(pid, uregs_syscall_arg1(r));
			break;
		case SYS_GETTIMEOFDAY:
			/* two output args */
			break;
		case SYS_SETTIMEOFDAY:
			struct_timeval_arg(pid, uregs_syscall_arg1(r)); COMMA;
			pointer_arg(uregs_syscall_arg2(r));
			break;
		case SYS_SIGACTION:
			signal_arg(uregs_syscall_arg1(r)); COMMA;
			sigaction_ptr_arg(pid, uregs_syscall_arg2(r)); COMMA;
			/* one output arg */
			break;
		case SYS_SIGPENDING:
			/* output only */
			break;
		case SYS_SIGPROCMASK:
			switch (uregs_syscall_arg1(r)) {
				C(SIG_BLOCK);
				C(SIG_UNBLOCK);
				C(SIG_SETMASK);
				default: int_arg(uregs_syscall_arg1(r)); break;
			} COMMA;
			sigset_ptr_arg(pid, uregs_syscall_arg2(r)); COMMA;
			/* one more after with old set */
			break;
		case SYS_SIGSUSPEND:
			sigset_ptr_arg(pid, uregs_syscall_arg1(r));
			break;
		case SYS_SIGWAIT:
			sigset_ptr_arg(pid, uregs_syscall_arg1(r)); COMMA;
			break;
		case SYS_SOCKET:
			sock_dom_arg(uregs_syscall_arg1(r)); COMMA;
			sock_typ_arg(uregs_syscall_arg2(r)); COMMA;
			sock_pro_arg(uregs_syscall_arg1(r), uregs_syscall_arg2(r), uregs_syscall_arg3(r));
			break;
		case SYS_RECV:
		case SYS_SEND:
			fd_arg(pid, uregs_syscall_arg1(r)); COMMA;
			msghdr_arg(pid, uregs_syscall_arg2(r)); COMMA;
			int_arg(uregs_syscall_arg3(r));
			break;
		case SYS_LISTEN:
			fd_arg(pid, uregs_syscall_arg1(r)); COMMA;
			int_arg(uregs_syscall_arg2(r));
			break;
		case SYS_CONNECT:
		case SYS_ACCEPT:
		case SYS_BIND:
			fd_arg(pid, uregs_syscall_arg1(r)); COMMA;
			sockaddr_arg(pid, uregs_syscall_arg2(r), uregs_syscall_arg3(r)); /* Consumes both */
			break;
		case SYS_GETSOCKNAME:
		case SYS_GETPEERNAME:
			fd_arg(pid, uregs_syscall_arg1(r)); COMMA;
			/* two output args */
			break;
		case SYS_SHUTDOWN:
			fd_arg(pid, uregs_syscall_arg1(r)); COMMA;
			int_arg(uregs_syscall_arg2(r)); /* TODO SHUT_... */
			break;
		case SYS_SETSOCKOPT:
			fd_arg(pid, uregs_syscall_arg1(r)); COMMA;
			sock_lvl_arg(uregs_syscall_arg2(r)); COMMA;
			sock_opt_arg(uregs_syscall_arg3(r)); COMMA;
			pointer_arg(uregs_syscall_arg4(r)); COMMA; /* TODO sockaddr in some contexts */
			uint_arg(uregs_syscall_arg5(r));
			break;
		case SYS_GETSOCKOPT:
			fd_arg(pid, uregs_syscall_arg1(r)); COMMA;
			sock_lvl_arg(uregs_syscall_arg2(r)); COMMA;
			sock_opt_arg(uregs_syscall_arg3(r)); COMMA;
			pointer_arg(uregs_syscall_arg4(r)); COMMA; /* TODO sockaddr in some contexts */
			pointer_arg(uregs_syscall_arg5(r)); /* Note - differs from set */
			break;
		case SYS_GETRUSAGE:
			switch (uregs_syscall_arg1(r)) {
				C(RUSAGE_SELF);
				C(RUSAGE_CHILDREN);
				default: int_arg(uregs_syscall_arg1(r)); break;
			} COMMA;
			/* one output arg */
			break;
		case SYS_SETUID:
		case SYS_SETGID:
			int_arg(uregs_syscall_arg1(r));
			break;
		case SYS_SETREUID:
		case SYS_SETREGID:
			int_arg(uregs_syscall_arg1(r)); COMMA;
			int_arg(uregs_syscall_arg2(r));
			break;
		case SYS_SETRESUID:
		case SYS_SETRESGID:
			int_arg(uregs_syscall_arg1(r)); COMMA;
			int_arg(uregs_syscall_arg2(r)); COMMA;
			int_arg(uregs_syscall_arg3(r));
			break;
		case SYS_MMAP:
			pointer_arg(uregs_syscall_arg1(r)); COMMA;
			uint_arg(uregs_syscall_arg2(r)); COMMA;
			mmap_prot_arg(uregs_syscall_arg3(r)); COMMA;
			mmap_flags_arg(uregs_syscall_arg4(r)); COMMA;
			fd_arg(pid, uregs_syscall_arg5(r)); COMMA;
			int_arg(uregs_syscall_arg6(r));
			break;
		case SYS_MUNMAP:
			pointer_arg(uregs_syscall_arg1(r)); COMMA;
			uint_arg(uregs_syscall_arg2(r));
			break;
		case SYS_SETTLSBASE:
			pointer_arg(uregs_syscall_arg1(r));
			break;
		case SYS_INSMOD:
			fd_arg(pid, uregs_syscall_arg1(r)); COMMA;
			int_arg(uregs_syscall_arg2(r)); COMMA;
			string_array_arg(pid, uregs_syscall_arg3(r));
			break;
		case SYS_SYMLINK:
			string_arg(pid, uregs_syscall_arg1(r)); COMMA;
			string_arg(pid, uregs_syscall_arg2(r));
			break;
		case SYS_READLINK:
			string_arg(pid, uregs_syscall_arg1(r)); COMMA;
			/* Plus two more when done */
			break;
		case SYS_REBOOT:
			int_arg(uregs_syscall_arg1(r));
			break;
		case SYS_GETGROUPS:
		case SYS_SETGROUPS:
			uint_arg(uregs_syscall_arg1(r)); COMMA;
			pointer_arg(uregs_syscall_arg2(r)); /* gid_t array */
			break;
		case SYS_TIMES:
			pointer_arg(uregs_syscall_arg1(r)); /* struct tms */
			break;
		/* These have no arguments: */
		case SYS_YIELD:
		case SYS_FORK:
		case SYS_GETEUID:
		case SYS_GETPID:
		case SYS_GETUID:
		case SYS_GETTID:
		case SYS_SETSID:
		case SYS_GETGID:
		case SYS_GETEGID:
		case SYS_GETPPID:
		case SYS_NPROC:
			break;
		default:
			fprintf(logfile, "...");
			break;
	}
	fflush(logfile);
	unfinished_child = pid;
}

static void finish_syscall(struct Pid * child, pid_t pid, int syscall, struct URegs * r) {
	if (syscall >= (int)sizeof(syscall_mask)) return;
	if (syscall >= 0 && !syscall_mask[syscall]) return;
	if (log_hidden) return;

	interrupt_log(pid);

	if (child->unfinished) fprintf(logfile, "/* %s resumed */ ", syscall_names[syscall]);
	child->unfinished = false;

	switch (syscall) {
		case -1:
			break; /* This is ptrace(PTRACE_TRACEME)... probably... */
		/* read() returns data in second value */
		case SYS_READ:
			buffer_arg(pid, uregs_syscall_arg2(r), uregs_syscall_result(r)); COMMA;
			uint_arg(uregs_syscall_arg3(r));
			maybe_errno(r);
			break;
		case SYS_PREAD:
			buffer_arg(pid, uregs_syscall_arg2(r), uregs_syscall_result(r)); COMMA;
			uint_arg(uregs_syscall_arg3(r)); COMMA;
			int_arg(uregs_syscall_arg4(r));
			maybe_errno(r);
			break;
		case SYS_GETHOSTNAME:
			string_arg(pid, uregs_syscall_arg1(r)); COMMA; /* is a nul-terminated string in gethostname */
			uint_arg(uregs_syscall_arg2(r));
			maybe_errno(r);
			break;
		case SYS_UNAME:
			struct_utsname_arg(pid, uregs_syscall_arg1(r));
			maybe_errno(r);
			break;
		case SYS_PIPE:
			fds_arg(pid, 2, uregs_syscall_arg1(r));
			maybe_errno(r);
			break;
		case SYS_PIPE2:
			fds_arg(pid, 2, uregs_syscall_arg1(r)); COMMA;
			open_flags(uregs_syscall_arg2(r));
			maybe_errno(r);
			break;
		case SYS_OPENPTY:
			fds_arg(pid, 1, uregs_syscall_arg1(r)); COMMA;
			fds_arg(pid, 1, uregs_syscall_arg2(r)); COMMA;
			pointer_arg(uregs_syscall_arg3(r)); COMMA; /* string but unused */
			pointer_arg(uregs_syscall_arg4(r)); COMMA; /* initial winsz but unused */
			pointer_arg(uregs_syscall_arg5(r)); /* size of winsz but unused */
			maybe_errno(r);
			break;
		case SYS_GETTIMEOFDAY:
			struct_timeval_arg(pid, uregs_syscall_arg1(r)); COMMA;
			pointer_arg(uregs_syscall_arg2(r));
			maybe_errno(r);
			break;
		/* sbrk() returns an address */
		case SYS_SBRK:
			fprintf(logfile, ") = %#zx\n", uregs_syscall_result(r));
			break;
		case SYS_EXECVE:
			if (r == NULL) fprintf(logfile, ") = 0\n");
			else maybe_errno(r);
			break;
		case SYS_SIGPROCMASK:
			sigset_ptr_arg(pid, uregs_syscall_arg3(r));
			maybe_errno(r);
			break;
		case SYS_SIGWAIT:
			signal_ptr_arg(pid, uregs_syscall_arg2(r));
			maybe_errno(r);
			break;
		case SYS_GETSOCKNAME:
		case SYS_GETPEERNAME:
			sockaddrp_arg(pid, uregs_syscall_arg2(r), uregs_syscall_arg3(r)); /* Consumes both */
			maybe_errno(r);
			break;
		case SYS_SIGACTION:
			sigaction_ptr_arg(pid, uregs_syscall_arg3(r));
			maybe_errno(r);
			break;
		case SYS_GETCWD:
			string_arg(pid, uregs_syscall_arg1(r)); COMMA;
			uint_arg(uregs_syscall_arg2(r));
			maybe_errno(r);
			break;
		case SYS_MMAP:
			if ((intptr_t)uregs_syscall_result(r) >= 0) fprintf(logfile, ") = %#zx\n", uregs_syscall_result(r));
			else maybe_errno(r);
			break;
		case SYS_GETRUSAGE:
			struct_rusage_arg(pid, uregs_syscall_arg2(r)); COMMA;
			maybe_errno(r);
			break;
		case SYS_UMASK:
			if ((intptr_t)uregs_syscall_result(r) >= 0) {
				fprintf(logfile, ") = ");
				mode_arg(uregs_syscall_result(r));
				fprintf(logfile, "\n");
			} else {
				maybe_errno(r);
			}
			break;
		case SYS_STATF:
		case SYS_STAT:
		case SYS_LSTAT:
			if ((intptr_t)uregs_syscall_result(r) >= 0) {
				struct_stat_arg(pid, uregs_syscall_arg2(r));
			} else {
				pointer_arg(uregs_syscall_arg2(r));
			}
			maybe_errno(r);
			break;
		case SYS_READDIR:
			if (uregs_syscall_result(r) > 0) {
				struct_dirent_arg(pid, uregs_syscall_arg3(r));
			} else if (uregs_syscall_result(r) == 0) {
				fprintf(logfile, "...");
			} else {
				pointer_arg(uregs_syscall_arg3(r));
			}
			maybe_errno(r);
			break;
		case SYS_READLINK:
			buffer_arg(pid, uregs_syscall_arg2(r), uregs_syscall_arg3(r)); COMMA;
			uint_arg(uregs_syscall_arg3(r));
			maybe_errno(r);
			break;
		case SYS_WAITPID:
			if ((intptr_t)uregs_syscall_result(r) >= 0) {
				wait_status_ptr_arg(pid, uregs_syscall_arg2(r)); COMMA;
			} else {
				pointer_arg(uregs_syscall_arg2(r)); COMMA;
			}
			wait_options_arg(uregs_syscall_arg3(r));
			maybe_errno(r);
			break;
		/* Most things return -errno, or positive valid result */
		default:
			maybe_errno(r);
			break;
	}
}

static int usage(char * argv[]) {
#define T_I "\033[3m"
#define T_O "\033[0m"
	fprintf(stderr, "usage: %s [-o logfile] [-e trace=...] [-p PID] [command...]\n"
			"  -o logfile   " T_I "Write tracing output to a file." T_O "\n"
			"  -h           " T_I "Show this help text." T_O "\n"
			"  -e trace=... " T_I "Trace the specified syscalls, or groups of syscalls:" T_O "\n"
			"               ",
			argv[0]);

	for (const struct SyscallSet * set = syscall_sets; set->name; set++) {
		fprintf(stderr, "%s%%%s", (set != syscall_sets) ? ", " : "", set->name);
	}

	fprintf(stderr,
			"\n"
			"  -p PID       " T_I "Trace an existing process." T_O "\n");
	return 1;
}

int main(int argc, char * argv[]) {
	logfile = stderr;

	pid_t p = 0;
	int opt;
	while ((opt = getopt(argc, argv, "+ho:e:p:f-:")) != -1) {
		switch (opt) {
			case 'p':
				p = atoi(optarg);
				break;
			case 'o':
				logfile = fopen(optarg, "w");
				if (!logfile) {
					fprintf(stderr, "%s: %s: %s\n", argv[0], optarg, strerror(errno));
					return 1;
				}
				break;
			case 'e':
				if (strstr(optarg,"trace=") == optarg) {
					/* First, disable everything. */
					memset(syscall_mask, 0, sizeof(syscall_mask));

					/* Now look at each comma-separated option */
					char * option = optarg + 6;
					char * comma = strstr(option, ",");
					while (1) {
						if (comma) *comma = '\0';

						if (*option == '%') {
							/* Check for special options */
							const int *syscalls = NULL;

							for (const struct SyscallSet * set = syscall_sets; set->name; set++) {
								if (!strcmp(set->name, option+1)) {
									syscalls = set->syscalls;
									break;
								}
							}

							if (syscalls) {
								for (const int *i = syscalls; *i != -1; i++) {
									syscall_mask[*i] = 1;
								}
							} else {
								fprintf(stderr, "%s: Unrecognized syscall group: %s\n", argv[0], option + 1);
								return 1;
							}
						} else {
							/* Check the list */
							int set_something = 0;
							for (size_t i = 0; i < sizeof(syscall_names) / sizeof(*syscall_names); ++i) {
								if (syscall_names[i] && !strcmp(option,syscall_names[i])) {
									syscall_mask[i] = 1;
									set_something = 1;
									break;
								}
							}
							if (!set_something) {
								fprintf(stderr, "%s: Unrecognized syscall name: %s\n", argv[0], option);
								return 1;
							}
						}
						if (!comma) break;
						option = comma + 1;
						comma = strstr(option, ",");
					}
				} else {
					char * eq = strstr(optarg, "=");
					if (eq) *eq = '\0';
					fprintf(stderr, "%s: Unrecognized -e option: %s\n", argv[0], optarg);
					return 1;
				}
				break;
			case 'f':
				follow_forks = true;
				break;
			case 'h':
				return usage(argv), 0;
			case '-':
				if (!strcmp(optarg,"follow-forks")) {
					follow_forks = true;
					break;
				} else if (!strcmp(optarg, "help")) {
					return usage(argv), 0;
				}
				fprintf(stderr, "%s: Unrecognized option: --%s\n", argv[0], optarg);
				// fallthrough
			case '?':
				return usage(argv);
		}
	}

	if (!p && optind == argc) {
		return usage(argv);
	}

	if (!p) {
		char *filename = argv[optind];
		if (!strchr(filename,'/')) {
			char *path = strdup(getenv("PATH") ?: "/bin:/usr/bin");
			char *p, *last;
			for ((p = strtok_r(path, ":", &last)); p;
			      p = strtok_r(NULL, ":", &last)) {
				char * exe = NULL;
				asprintf(&exe, "%s/%s", p, filename);
				if (!access(exe, X_OK)) {
					filename = exe;
					break;
				}
				free(exe);
			}
			free(path);

			if (filename == argv[optind]) {
				fprintf(stderr, "%s: Cannot find executable '%s'\n", argv[0], filename);
				return 1;
			}
		}

		struct stat sb;
		if (stat(filename, &sb)) {
			fprintf(stderr, "%s: Cannot stat '%s': %s\n", argv[0], filename, strerror(errno));
			return 1;
		}

		p = fork();
		if (!p) {
			if (ptrace(PTRACE_TRACEME, 0, NULL, NULL) < 0) {
				fprintf(stderr, "%s: ptrace: %s\n", argv[0], strerror(errno));
				return 1;
			}
			exit(execv(filename, &argv[optind]));
		}
		signal(SIGINT, SIG_IGN);
	} else {
		log_hidden = false;
	}

	/* Set up first child. */
	children = calloc(1, sizeof(struct Pid));
	children->pid = p;
	children->previous_syscall = -1;
	children_count = 1;

	if (ptrace(PTRACE_ATTACH, p, NULL, NULL) < 0) {
		fprintf(stderr, "%s: ptrace: %s\n", argv[0], strerror(errno));
		return 1;
	}

	if (follow_forks) {
		if (ptrace(PTRACE_SETOPTIONS, p, NULL, (void*)(uintptr_t)(PTRACE_O_TRACEFORK | PTRACE_O_TRACECLONE)) < 0) {
			fprintf(stderr, "%s: ptrace: %s\n", argv[0], strerror(errno));
			return 1;
		}
	}

	while (1) {
		int status = 0;
		pid_t res = waitpid(-1, &status, WSTOPPED);

		if (res < 0) {
			if (errno == ECHILD) break;
			fprintf(stderr, "%s: waitpid: %s\n", argv[0], strerror(errno));
		} else if (res == 0) {
			/* No child process? */
		} else {
			/* Do we know about 'res' yet? */
			struct Pid * child = find_pid(res);
			if (!child) {
				child = calloc(1, sizeof(struct Pid));
				child->next = children;
				child->pid = res;
				child->previous_syscall = -1;
				children->prev = child;
				children = child;
				children_count++;

				/* Just in case, only do this if we actually said to. */
				if (follow_forks) ptrace(PTRACE_SETOPTIONS, res, NULL, (void*)(uintptr_t)(PTRACE_O_TRACEFORK | PTRACE_O_TRACECLONE));

				interrupt_log(res);
				fprintf(logfile, "Process %d attached\n", res);
			}

			if (WIFSTOPPED(status)) {
				if (WSTOPSIG(status) == SIGTRAP) {
					struct URegs regs;
					ptrace(PTRACE_GETREGS, res, NULL, &regs);

					/* Event type */
					int event = (status >> 16) & 0xFF;
					switch (event) {
						case PTRACE_EVENT_SYSCALL_ENTER:
							if (child->previous_syscall == SYS_EXECVE) finish_syscall(child, res, SYS_EXECVE,NULL);
							child->previous_syscall = uregs_syscall_num(&regs);
							if (log_hidden && child->previous_syscall == SYS_EXECVE) log_hidden = false;
							handle_syscall(child, res, &regs);
							break;
						case PTRACE_EVENT_SYSCALL_EXIT:
							finish_syscall(child, res, child->previous_syscall, &regs);
							child->previous_syscall = -1;
							break;
						default:
							fprintf(logfile, "Unknown event.\n");
							break;
					}
					ptrace(PTRACE_CONT, res, NULL, NULL);
				} else {
					report_pid(child);
					fprintf(logfile, "--- ");
					signal_arg(WSTOPSIG(status));
					fprintf(logfile, " ---\n");

					ptrace(PTRACE_CONT, res, NULL, (void*)(uintptr_t)WSTOPSIG(status));
				}
			} else if (WIFSIGNALED(status)) {
				report_pid(child);
				fprintf(logfile, "+++ killed by ");
				signal_arg(WTERMSIG(status));
				fprintf(logfile, " +++\n");

				if (forget_child(child)) return 0;
			} else if (WIFEXITED(status)) {
				report_pid(child);
				fprintf(logfile, "+++ exited with %d +++\n", WEXITSTATUS(status));
				if (forget_child(child)) return 0;
			} else {
				fprintf(logfile, "??? unrecognized status %x\n", status);
			}
		}
	}

	return 0;
}
