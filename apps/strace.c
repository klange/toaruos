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
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/signal.h>
#include <sys/signal_defs.h>
#include <sys/sysfunc.h>
#include <sys/utsname.h>
#include <sys/time.h>
#include <syscall_nums.h>

static FILE * logfile;

struct regs {
	/* Pushed by common stub */
	uintptr_t r15, r14, r13, r12;
	uintptr_t r11, r10, r9, r8;
	uintptr_t rbp, rdi, rsi, rdx, rcx, rbx, rax;

	/* Pushed by wrapper */
	uintptr_t int_no, err_code;

	/* Pushed by interrupt */
	uintptr_t rip, cs, rflags, rsp, ss;
};

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
};

#define M(e) [e] = #e
const char * errno_names[256] = {
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
};


const char * signal_names[256] = {
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
};

#if 0
static void dump_regs(struct regs * r) {
	fprintf(logfile,
		"  $rip=0x%016lx\n"
		"  $rsi=0x%016lx,$rdi=0x%016lx,$rbp=0x%016lx,$rsp=0x%016lx\n"
		"  $rax=0x%016lx,$rbx=0x%016lx,$rcx=0x%016lx,$rdx=0x%016lx\n"
		"  $r8= 0x%016lx,$r9= 0x%016lx,$r10=0x%016lx,$r11=0x%016lx\n"
		"  $r12=0x%016lx,$r13=0x%016lx,$r14=0x%016lx,$r15=0x%016lx\n"
		"  cs=0x%016lx  ss=0x%016lx rflags=0x%016lx int=0x%02lx err=0x%02lx\n",
		r->rip,
		r->rsi, r->rdi, r->rbp, r->rsp,
		r->rax, r->rbx, r->rcx, r->rdx,
		r->r8, r->r9, r->r10, r->r11,
		r->r12, r->r13, r->r14, r->r15,
		r->cs, r->ss, r->rflags, r->int_no, r->err_code
	);
}
#endif

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

static void print_error(int err) {
	if (err > 255) return;
	fprintf(logfile, " %s (%s)", errno_names[err], strerror(err));
}

static void maybe_errno(struct regs * r) {
	fprintf(logfile, ") = %ld", r->rax);
	if ((intptr_t)r->rax < 0) print_error(-r->rax);
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

static void handle_syscall(pid_t pid, struct regs * r) {
	fprintf(logfile, "%s(", syscall_names[r->rax]);
	switch (r->rax) {
		case SYS_OPEN:
			string_arg(pid, r->rbx); COMMA;
			open_flags(r->rcx);
			break;
		case SYS_READ:
			fd_arg(pid, r->rbx); COMMA;
			/* Plus two more when done */
			break;
		case SYS_WRITE:
			fd_arg(pid, r->rbx); COMMA;
			buffer_arg(pid, r->rcx, r->rdx); COMMA;
			uint_arg(r->rdx);
			break;
		case SYS_CLOSE:
			fd_arg(pid, r->rbx);
			break;
		case SYS_SBRK:
			uint_arg(r->rbx);
			break;
		case SYS_SEEK:
			fd_arg(pid, r->rbx); COMMA;
			int_arg(r->rcx); COMMA;
			switch (r->rdx) {
				case 0: fprintf(logfile, "SEEK_SET"); break;
				case 1: fprintf(logfile, "SEEK_CUR"); break;
				case 2: fprintf(logfile, "SEEK_END"); break;
				default: int_arg(r->rdx); break;
			}
			break;
		case SYS_STATF:
			string_arg(pid, r->rbx); COMMA;
			pointer_arg(r->rcx);
			break;
		case SYS_LSTAT:
			string_arg(pid, r->rbx); COMMA;
			pointer_arg(r->rcx);
			break;
		case SYS_READDIR:
			fd_arg(pid, r->rbx); COMMA;
			int_arg(r->rcx); COMMA;
			pointer_arg(r->rdx);
			break;
		case SYS_KILL:
			int_arg(r->rbx); COMMA; /* pid_arg? */
			int_arg(r->rcx); /* TODO signal name */
			break;
		case SYS_CHDIR:
			string_arg(pid, r->rbx);
			break;
		case SYS_GETCWD:
			/* output is first arg */
			pointer_arg(r->rbx); COMMA; /* TODO syscall outputs */
			uint_arg(r->rcx);
			break;
		case SYS_CLONE:
			pointer_arg(r->rbx); COMMA;
			pointer_arg(r->rcx); COMMA;
			pointer_arg(r->rdx);
			break;
		case SYS_SETHOSTNAME:
			string_arg(pid, r->rbx);
			break;
		case SYS_GETHOSTNAME:
			/* plus one more when done */
			break;
		case SYS_MKDIR:
			string_arg(pid, r->rbx); COMMA;
			uint_arg(r->rcx);
			break;
		case SYS_SHUTDOWN:
			int_arg(r->rbx); COMMA;
			int_arg(r->rcx);
			break;
		case SYS_ACCESS:
			string_arg(pid, r->rbx); COMMA;
			int_arg(r->rcx);
			break;
		case SYS_PTRACE:
			switch (r->rbx) {
				C(PTRACE_ATTACH);
				C(PTRACE_CONT);
				C(PTRACE_DETACH);
				C(PTRACE_TRACEME);
				C(PTRACE_GETREGS);
				C(PTRACE_PEEKDATA);
				default: int_arg(r->rbx); break;
			} COMMA;
			int_arg(r->rcx); COMMA;
			pointer_arg(r->rdx); COMMA;
			pointer_arg(r->rsi);
			break;
		case SYS_EXECVE:
			string_arg(pid, r->rbx); COMMA;
			string_array_arg(pid, r->rcx); COMMA;
			pointer_arg(r->rdx);
			break;
		case SYS_SHM_OBTAIN:
			string_arg(pid, r->rbx); COMMA;
			pointer_arg(r->rcx);
			break;
		case SYS_SHM_RELEASE:
			string_arg(pid, r->rbx);
			break;
		case SYS_SIGNAL:
			int_arg(r->rbx); COMMA; /* TODO signal name */
			pointer_arg(r->rcx);
			break;
		case SYS_SYSFUNC:
			switch (r->rbx) {
				C(TOARU_SYS_FUNC_SYNC);
				C(TOARU_SYS_FUNC_LOGHERE);
				C(TOARU_SYS_FUNC_KDEBUG);
				C(TOARU_SYS_FUNC_INSMOD);
				C(TOARU_SYS_FUNC_SETHEAP);
				C(TOARU_SYS_FUNC_MMAP);
				C(TOARU_SYS_FUNC_THREADNAME);
				C(TOARU_SYS_FUNC_SETGSBASE);
				C(TOARU_SYS_FUNC_NPROC);
				default: int_arg(r->rbx); break;
			} COMMA;
			pointer_arg(r->rcx);
			break;
		case SYS_FSWAIT:
			int_arg(r->rbx); COMMA;
			fds_arg(pid, r->rbx, r->rcx);
			break;
		case SYS_FSWAIT2:
			int_arg(r->rbx); COMMA;
			fds_arg(pid, r->rbx, r->rcx); COMMA;
			int_arg(r->rdx);
			break;
		case SYS_FSWAIT3:
			int_arg(r->rbx); COMMA;
			fds_arg(pid, r->rbx, r->rcx); COMMA;
			int_arg(r->rdx); COMMA;
			pointer_arg(r->rsi);
			break;
		case SYS_IOCTL:
			fd_arg(pid, r->rbx); COMMA;
			int_arg(r->rcx); COMMA;
			pointer_arg(r->rdx);
			break;
		case SYS_WAITPID:
			int_arg(r->rbx); COMMA;
			pointer_arg(r->rcx); COMMA;
			int_arg(r->rdx); /* TODO waitpid options */
			break;
		case SYS_EXT:
			int_arg(r->rbx);
			fprintf(logfile, ") = ?\n");
			return;
		case SYS_UNAME:
			/* One output arg */
			break;
		case SYS_SLEEPABS:
			uint_arg(r->rbx); COMMA;
			uint_arg(r->rcx);
			break;
		case SYS_SLEEP:
			uint_arg(r->rbx); COMMA;
			uint_arg(r->rcx);
			break;
		case SYS_PIPE:
			/* Arg is a pointer */
			break;
		case SYS_DUP2:
			fd_arg(pid, r->rbx); COMMA;
			fd_arg(pid, r->rcx);
			break;
		case SYS_MOUNT:
			string_arg(pid, r->rbx); COMMA;
			string_arg(pid, r->rcx); COMMA;
			uint_arg(r->rdx); COMMA;
			pointer_arg(r->rsi);
			break;
		case SYS_UMASK:
			int_arg(r->rbx);
			break;
		case SYS_UNLINK:
			string_arg(pid, r->rbx);
			break;
		case SYS_GETTIMEOFDAY:
			/* two output args */
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

static void finish_syscall(pid_t pid, int syscall, struct regs * r) {
	switch (syscall) {
		case -1:
			break; /* This is ptrace(PTRACE_TRACEME)... probably... */
		/* read() returns data in second value */
		case SYS_READ:
			buffer_arg(pid, r->rcx, r->rax); COMMA;
			uint_arg(r->rdx);
			maybe_errno(r);
			break;
		case SYS_GETHOSTNAME:
			string_arg(pid, r->rbx);
			maybe_errno(r);
			break;
		case SYS_UNAME:
			struct_utsname_arg(pid, r->rbx);
			maybe_errno(r);
			break;
		case SYS_PIPE:
			fds_arg(pid, 2, r->rbx);
			maybe_errno(r);
			break;
		case SYS_GETTIMEOFDAY:
			struct_timeval_arg(pid, r->rbx);
			maybe_errno(r);
			break;
		/* sbrk() returns an address */
		case SYS_SBRK:
			fprintf(logfile, ") = %#zx\n", r->rax);
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
	fprintf(stderr, "usage: %s [-o logfile] command...\n"
			"  -o logfile " T_I "Write tracing output to a file." T_O "\n"
			"  -h         " T_I "Show this help text." T_O "\n",
			argv[0]);
	return 1;
}

int main(int argc, char * argv[]) {
	logfile = stdout;

	int opt;
	while ((opt = getopt(argc, argv, "ho:")) != -1) {
		switch (opt) {
			case 'o':
				logfile = fopen(optarg, "w");
				if (!logfile) {
					fprintf(stderr, "%s: %s: %s\n", argv[0], optarg, strerror(errno));
					return 1;
				}
				break;
			case 'h':
				return (usage(argv), 0);
			case '?':
				return usage(argv);
		}
	}

	if (optind == argc) {
		return usage(argv);
	}

	pid_t p = fork();
	if (!p) {
		if (ptrace(PTRACE_TRACEME, 0, NULL, NULL) < 0) {
			fprintf(stderr, "%s: ptrace: %s\n", argv[0], strerror(errno));
			return 1;
		}
		execvp(argv[optind], &argv[optind]);
		return 1;
	} else {
		int previous_syscall = -1;
		while (1) {
			int status = 0;
			pid_t res = waitpid(p, &status, WSTOPPED);

			if (res < 0) {
				fprintf(stderr, "%s: waitpid: %s\n", argv[0], strerror(errno));
			} else {
				if (WIFSTOPPED(status)) {
					if (WSTOPSIG(status) == SIGTRAP) {
						struct regs regs;
						ptrace(PTRACE_GETREGS, p, NULL, &regs);

						/* Event type */
						int event = (status >> 16) & 0xFF;
						switch (event) {
							case PTRACE_EVENT_SYSCALL_ENTER:
								previous_syscall = regs.rax;
								handle_syscall(p, &regs);
								break;
							case PTRACE_EVENT_SYSCALL_EXIT:
								finish_syscall(p, previous_syscall, &regs);
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
	}

	return 0;
}
