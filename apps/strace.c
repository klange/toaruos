/**
 * @brief Process system call tracer.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021 K. Lange
 */
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/signal.h>
#include <sys/signal_defs.h>
#include <sys/sysfunc.h>
#include <sys/utsname.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/uregs.h>
#include <syscall_nums.h>

static FILE * logfile;

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
	[SYS_SYSFUNC]      = "sysfunc",
	[SYS_IOCTL]        = "ioctl",
	[SYS_ACCESS]       = "access",
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
	[SYS_PREAD]        = "pread",
	[SYS_PWRITE]       = "pwrite",
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
	[SYS_SYSFUNC]      = 1,
	[SYS_IOCTL]        = 1,
	[SYS_ACCESS]       = 1,
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
};


const char * signal_names[NSIG] = {
	M(SIGHUP),
	M(SIGINT),
	M(SIGQUIT),
	M(SIGILL),
	M(SIGTRAP),
	M(SIGABRT),
	M(SIGEMT),
	M(SIGFPE),
	M(SIGKILL),
	M(SIGBUS),
	M(SIGSEGV),
	M(SIGSYS),
	M(SIGPIPE),
	M(SIGALRM),
	M(SIGTERM),
	M(SIGUSR1),
	M(SIGUSR2),
	M(SIGCHLD),
	M(SIGPWR),
	M(SIGWINCH),
	M(SIGURG),
	M(SIGPOLL),
	M(SIGSTOP),
	M(SIGTSTP),
	M(SIGCONT),
	M(SIGTTIN),
	M(SIGTTOUT),
	M(SIGVTALRM),
	M(SIGPROF),
	M(SIGXCPU),
	M(SIGXFSZ),
	M(SIGWAITING),
	M(SIGDIAF),
	M(SIGHATE),
	M(SIGWINEVENT),
	M(SIGCAT),
	M(SIGTTOU),
};

static void open_flags(int flags) {
	if (!flags) {
		fprintf(logfile, "O_RDONLY");
		return;
	}

	/* That's all that's valid right now */
	flags &= 0xFFFF;

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

	if (flags) {
		fprintf(logfile, "(%#x)", flags);
	}
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

		if (buf == '\\') fprintf(logfile, "\\\\");
		else if (buf == '"') fprintf(logfile, "\\\"");
		else if (buf >= ' ' && buf < '~') fprintf(logfile, "%c", buf);
		else if (buf == '\r') fprintf(logfile, "\\r");
		else if (buf == '\n') fprintf(logfile, "\\n");
		else fprintf(logfile, "\\x%02x", buf);

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

static void fd_arg(pid_t pid, int val) {
	/* TODO: Look up file in user data? */
	fprintf(logfile, "%d", val);
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

			if (buf == '\\') fprintf(logfile, "\\\\");
			else if (buf == '"') fprintf(logfile, "\\\"");
			else if (buf >= ' ' && buf < '~') fprintf(logfile, "%c", buf);
			else if (buf == '\r') fprintf(logfile, "\\r");
			else if (buf == '\n') fprintf(logfile, "\\n");
			else fprintf(logfile, "\\x%02x", buf);

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

static void signal_arg(int signum) {
	if (signum >= 0 && signum < NSIG && signal_names[signum]) {
		fprintf(logfile, "%s", signal_names[signum]);
	} else {
		fprintf(logfile, "%d", signum);
	}
}

static void handle_syscall(pid_t pid, struct URegs * r) {
	if (uregs_syscall_num(r) >= sizeof(syscall_mask)) return;
	if (!syscall_mask[uregs_syscall_num(r)]) return;

	fprintf(logfile, "%s(", syscall_names[uregs_syscall_num(r)]);
	switch (uregs_syscall_num(r)) {
		case SYS_OPEN:
			string_arg(pid, uregs_syscall_arg1(r)); COMMA;
			open_flags(uregs_syscall_arg2(r));
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
			uint_arg(uregs_syscall_arg4(r));
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
				case 0: fprintf(logfile, "SEEK_SET"); break;
				case 1: fprintf(logfile, "SEEK_CUR"); break;
				case 2: fprintf(logfile, "SEEK_END"); break;
				default: int_arg(uregs_syscall_arg3(r)); break;
			}
			break;
		case SYS_STATF:
			string_arg(pid, uregs_syscall_arg1(r)); COMMA;
			pointer_arg(uregs_syscall_arg2(r));
			break;
		case SYS_LSTAT:
			string_arg(pid, uregs_syscall_arg1(r)); COMMA;
			pointer_arg(uregs_syscall_arg2(r));
			break;
		case SYS_READDIR:
			fd_arg(pid, uregs_syscall_arg1(r)); COMMA;
			int_arg(uregs_syscall_arg2(r)); COMMA;
			pointer_arg(uregs_syscall_arg3(r));
			break;
		case SYS_KILL:
			int_arg(uregs_syscall_arg1(r)); COMMA; /* pid_arg? */
			int_arg(uregs_syscall_arg2(r)); /* TODO signal name */
			break;
		case SYS_CHDIR:
			string_arg(pid, uregs_syscall_arg1(r));
			break;
		case SYS_GETCWD:
			/* output is first arg */
			pointer_arg(uregs_syscall_arg1(r)); COMMA; /* TODO syscall outputs */
			uint_arg(uregs_syscall_arg2(r));
			break;
		case SYS_CLONE:
			pointer_arg(uregs_syscall_arg1(r)); COMMA;
			pointer_arg(uregs_syscall_arg2(r)); COMMA;
			pointer_arg(uregs_syscall_arg3(r));
			break;
		case SYS_SETHOSTNAME:
			string_arg(pid, uregs_syscall_arg1(r));
			break;
		case SYS_GETHOSTNAME:
			/* plus one more when done */
			break;
		case SYS_MKDIR:
			string_arg(pid, uregs_syscall_arg1(r)); COMMA;
			uint_arg(uregs_syscall_arg2(r));
			break;
		case SYS_SHUTDOWN:
			int_arg(uregs_syscall_arg1(r)); COMMA;
			int_arg(uregs_syscall_arg2(r));
			break;
		case SYS_ACCESS:
			string_arg(pid, uregs_syscall_arg1(r)); COMMA;
			int_arg(uregs_syscall_arg2(r));
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
			pointer_arg(uregs_syscall_arg3(r));
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
		case SYS_SYSFUNC:
			switch (uregs_syscall_arg1(r)) {
				C(TOARU_SYS_FUNC_SYNC);
				C(TOARU_SYS_FUNC_LOGHERE);
				C(TOARU_SYS_FUNC_KDEBUG);
				C(TOARU_SYS_FUNC_INSMOD);
				C(TOARU_SYS_FUNC_SETHEAP);
				C(TOARU_SYS_FUNC_MMAP);
				C(TOARU_SYS_FUNC_THREADNAME);
				C(TOARU_SYS_FUNC_SETGSBASE);
				C(TOARU_SYS_FUNC_NPROC);
				default: int_arg(uregs_syscall_arg1(r)); break;
			} COMMA;
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
			pointer_arg(uregs_syscall_arg2(r)); COMMA;
			int_arg(uregs_syscall_arg3(r)); /* TODO waitpid options */
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
		case SYS_DUP2:
			fd_arg(pid, uregs_syscall_arg1(r)); COMMA;
			fd_arg(pid, uregs_syscall_arg2(r));
			break;
		case SYS_MOUNT:
			string_arg(pid, uregs_syscall_arg1(r)); COMMA;
			string_arg(pid, uregs_syscall_arg2(r)); COMMA;
			uint_arg(uregs_syscall_arg3(r)); COMMA;
			pointer_arg(uregs_syscall_arg4(r));
			break;
		case SYS_UMASK:
			int_arg(uregs_syscall_arg1(r));
			break;
		case SYS_UNLINK:
			string_arg(pid, uregs_syscall_arg1(r));
			break;
		case SYS_GETTIMEOFDAY:
			/* two output args */
			break;
		case SYS_SIGACTION: break;
		case SYS_RECV:
		case SYS_SEND:
			fd_arg(pid, uregs_syscall_arg1(r)); COMMA;
			msghdr_arg(pid, uregs_syscall_arg2(r)); COMMA;
			int_arg(uregs_syscall_arg3(r));
			break;
		/* These have no arguments: */
		case SYS_YIELD:
		case SYS_FORK:
		case SYS_GETEUID:
		case SYS_GETPID:
		case SYS_GETUID:
		case SYS_REBOOT:
		case SYS_GETTID:
		case SYS_SETSID:
		case SYS_GETGID:
		case SYS_GETEGID:
			break;
		default:
			fprintf(logfile, "...");
			break;
	}
	fflush(stdout);
}

static void finish_syscall(pid_t pid, int syscall, struct URegs * r) {
	if (syscall >= (int)sizeof(syscall_mask)) return;
	if (syscall >= 0 && !syscall_mask[syscall]) return;

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
			uint_arg(uregs_syscall_arg4(r));
			maybe_errno(r);
			break;
		case SYS_GETHOSTNAME:
			string_arg(pid, uregs_syscall_arg1(r));
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
		case SYS_GETTIMEOFDAY:
			struct_timeval_arg(pid, uregs_syscall_arg1(r));
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
			"  -e trace=... " T_I "Set tracing options." T_O "\n"
			"  -p PID       " T_I "Trace an existing process." T_O "\n",
			argv[0]);
	return 1;
}

int main(int argc, char * argv[]) {
	logfile = stdout;

	pid_t p = 0;
	int opt;
	while ((opt = getopt(argc, argv, "ho:e:p:")) != -1) {
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
					do {
						if (comma) *comma = '\0';

						if (*option == '%') {
							/* Check for special options */
							if (!strcmp(option+1,"net") || !strcmp(option+1,"network")) {
								int syscalls[] = {
									SYS_SOCKET, SYS_SETSOCKOPT, SYS_BIND, SYS_ACCEPT, SYS_LISTEN,
									SYS_CONNECT, SYS_GETSOCKOPT, SYS_RECV, SYS_SEND, SYS_SHUTDOWN,
									0
								};
								for (int *i = syscalls; *i; i++) {
									syscall_mask[*i] = 1;
								}
							} else if (!strcmp(option+1,"file")) {
								int syscalls[] = {
									SYS_OPEN, SYS_STATF, SYS_LSTAT, SYS_ACCESS, SYS_EXECVE,
									SYS_GETCWD, SYS_CHDIR, SYS_MKDIR, SYS_SYMLINK, SYS_UNLINK,
									SYS_CHMOD, SYS_CHOWN, SYS_MOUNT, SYS_READLINK,
									0
								};
								for (int *i = syscalls; *i; i++) {
									syscall_mask[*i] = 1;
								}
							} else if (!strcmp(option+1,"desc")) {
								int syscalls[] = {
									SYS_OPEN, SYS_READ, SYS_WRITE, SYS_CLOSE, SYS_STAT, SYS_FSWAIT,
									SYS_FSWAIT2, SYS_FSWAIT3, SYS_SEEK, SYS_IOCTL, SYS_PIPE, SYS_MKPIPE,
									SYS_DUP2, SYS_READDIR, SYS_OPENPTY, SYS_PREAD, SYS_PWRITE,
									0
								};
								for (int *i = syscalls; *i; i++) {
									syscall_mask[*i] = 1;
								}
							} else if (!strcmp(option+1,"memory")) {
								int syscalls[] = {
									SYS_SBRK, SYS_SHM_OBTAIN, SYS_SHM_RELEASE,
									0
								};
								for (int *i = syscalls; *i; i++) {
									syscall_mask[*i] = 1;
								}
							} else if (!strcmp(option+1,"ipc")) {
								int syscalls[] = {
									SYS_SHM_OBTAIN, SYS_SHM_RELEASE,
									0
								};
								for (int *i = syscalls; *i; i++) {
									syscall_mask[*i] = 1;
								}
							} else if (!strcmp(option+1,"signal")) {
								int syscalls[] = {
									SYS_SIGNAL, SYS_KILL,
									0
								};
								for (int *i = syscalls; *i; i++) {
									syscall_mask[*i] = 1;
								}
							} else if (!strcmp(option+1,"process")) {
								int syscalls[] = {
									SYS_EXT, SYS_EXECVE, SYS_FORK, SYS_CLONE, SYS_WAITPID, SYS_KILL,
									0
								};
								for (int *i = syscalls; *i; i++) {
									syscall_mask[*i] = 1;
								}
							} else if (!strcmp(option+1,"creds")) {
								int syscalls[] = {
									SYS_GETUID, SYS_GETGID, SYS_GETGROUPS, SYS_GETEGID, SYS_GETEUID,
									SYS_SETUID, SYS_SETGID, SYS_SETGROUPS,
									0
								};
								for (int *i = syscalls; *i; i++) {
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
						if (comma) { option = comma + 1; }
					} while (comma);
				} else {
					char * eq = strstr(optarg, "=");
					if (eq) *eq = '\0';
					fprintf(stderr, "%s: Unrecognized -e option: %s\n", argv[0], optarg);
					return 1;
				}
				break;
			case 'h':
				return (usage(argv), 0);
			case '?':
				return usage(argv);
		}
	}

	if (!p && optind == argc) {
		return usage(argv);
	}

	if (!p) {
		p = fork();
		if (!p) {
			if (ptrace(PTRACE_TRACEME, 0, NULL, NULL) < 0) {
				fprintf(stderr, "%s: ptrace: %s\n", argv[0], strerror(errno));
				return 1;
			}
			execvp(argv[optind], &argv[optind]);
			return 1;
		}
		signal(SIGINT, SIG_IGN);
	} else {
		if (ptrace(PTRACE_ATTACH, p, NULL, NULL) < 0) {
			fprintf(stderr, "%s: ptrace: %s\n", argv[0], strerror(errno));
			return 1;
		}
	}

	int previous_syscall = -1;
	while (1) {
		int status = 0;
		pid_t res = waitpid(p, &status, WSTOPPED);

		if (res < 0) {
			fprintf(stderr, "%s: waitpid: %s\n", argv[0], strerror(errno));
		} else {
			if (WIFSTOPPED(status)) {
				if (WSTOPSIG(status) == SIGTRAP) {
					struct URegs regs;
					ptrace(PTRACE_GETREGS, p, NULL, &regs);

					/* Event type */
					int event = (status >> 16) & 0xFF;
					switch (event) {
						case PTRACE_EVENT_SYSCALL_ENTER:
							if (previous_syscall == SYS_EXECVE) finish_syscall(p,SYS_EXECVE,NULL);
							previous_syscall = uregs_syscall_num(&regs);
							handle_syscall(p, &regs);
							break;
						case PTRACE_EVENT_SYSCALL_EXIT:
							finish_syscall(p, previous_syscall, &regs);
							previous_syscall = -1;
							break;
						default:
							fprintf(logfile, "Unknown event.\n");
							break;
					}
					ptrace(PTRACE_CONT, p, NULL, NULL);
				} else {
					fprintf(logfile, "--- %s ---\n", signal_names[WSTOPSIG(status)]);
					ptrace(PTRACE_CONT, p, NULL, (void*)(uintptr_t)WSTOPSIG(status));
				}
			} else if (WIFSIGNALED(status)) {
				fprintf(logfile, "+++ killed by %s +++\n", signal_names[WTERMSIG(status)]);
				return 0;
			} else if (WIFEXITED(status)) {
				fprintf(logfile, "+++ exited with %d +++\n", WEXITSTATUS(status));
				return 0;
			}
		}
	}

	return 0;
}
