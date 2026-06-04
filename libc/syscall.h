#pragma once

#include <_cheader.h>
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/times.h>
#include <sys/signal.h>
#include <sys/resource.h>

#include <libc/internal.h>

_Begin_C_Header

#define DECL_SYSCALL0(fn)                   _hidden long syscall_##fn(void)
#define DECL_SYSCALL1(fn,p1)                _hidden long syscall_##fn(p1)
#define DECL_SYSCALL2(fn,p1,p2)             _hidden long syscall_##fn(p1,p2)
#define DECL_SYSCALL3(fn,p1,p2,p3)          _hidden long syscall_##fn(p1,p2,p3)
#define DECL_SYSCALL4(fn,p1,p2,p3,p4)       _hidden long syscall_##fn(p1,p2,p3,p4)
#define DECL_SYSCALL5(fn,p1,p2,p3,p4,p5)    _hidden long syscall_##fn(p1,p2,p3,p4,p5)
#define DECL_SYSCALL6(fn,p1,p2,p3,p4,p5,p6) _hidden long syscall_##fn(p1,p2,p3,p4,p5,p6)

#ifdef __x86_64__

#define __SYSCALL_ENTRY_INST "syscall"
#define __SYSCALL_CLOBBERS   "rcx", "r11", "memory"

#define DEFN_SYSCALL0(fn, num) \
	long syscall_##fn(void) { \
		long a = num; __asm__ __volatile__(__SYSCALL_ENTRY_INST : "=a" (a) : "a" ((long)a) : __SYSCALL_CLOBBERS); \
		return a; \
	}

#define DEFN_SYSCALL1(fn, num, P1) \
	long syscall_##fn(P1 p1) { \
		long __res = num; __asm__ __volatile__(__SYSCALL_ENTRY_INST \
				: "=a" (__res) \
				: "a" (__res), "D" ((long)(p1)) : __SYSCALL_CLOBBERS ); \
		return __res; \
	}

#define DEFN_SYSCALL2(fn, num, P1, P2) \
	long syscall_##fn(P1 p1, P2 p2) { \
		long __res = num; __asm__ __volatile__(__SYSCALL_ENTRY_INST \
				: "=a" (__res) \
				: "a" (__res), "D" ((long)(p1)), "S"((long)(p2)) : __SYSCALL_CLOBBERS ); \
		return __res; \
	}

#define DEFN_SYSCALL3(fn, num, P1, P2, P3) \
	long syscall_##fn(P1 p1, P2 p2, P3 p3) { \
		long __res = num; __asm__ __volatile__(__SYSCALL_ENTRY_INST \
				: "=a" (__res) \
				: "a" (__res), "D" ((long)(p1)), "S"((long)(p2)), "d"((long)(p3)) : __SYSCALL_CLOBBERS ); \
		return __res; \
	}

#define DEFN_SYSCALL4(fn, num, P1, P2, P3, P4) \
	long syscall_##fn(P1 p1, P2 p2, P3 p3, P4 p4) { \
		register long p4_ __asm__("r10") = (long)p4; \
		long __res = num; __asm__ __volatile__(__SYSCALL_ENTRY_INST \
				: "=a" (__res) \
				: "a" (__res), "D" ((long)(p1)), "S"((long)(p2)), "d"((long)(p3)), "r"((long)(p4_)) : __SYSCALL_CLOBBERS ); \
		return __res; \
	}

#define DEFN_SYSCALL5(fn, num, P1, P2, P3, P4, P5) \
	long syscall_##fn(P1 p1, P2 p2, P3 p3, P4 p4, P5 p5) { \
		register long p4_ __asm__("r10") = (long)p4; \
		register long p5_ __asm__("r8") = (long)p5; \
		long __res = num; __asm__ __volatile__(__SYSCALL_ENTRY_INST \
				: "=a" (__res) \
				: "a" (__res), "D" ((long)(p1)), "S"((long)(p2)), "d"((long)(p3)), "r"((long)(p4_)), "r"((long)(p5_)) : __SYSCALL_CLOBBERS ); \
		return __res; \
	}

#define DEFN_SYSCALL6(fn, num, P1, P2, P3, P4, P5, P6) \
	long syscall_##fn(P1 p1, P2 p2, P3 p3, P4 p4, P5 p5, P6 p6) { \
		register long p4_ __asm__("r10") = (long)p4; \
		register long p5_ __asm__("r8") = (long)p5; \
		register long p6_ __asm__("r9") = (long)p6; \
		long __res = num; __asm__ __volatile__(__SYSCALL_ENTRY_INST \
				: "=a" (__res) \
				: "a" (__res), "D" ((long)(p1)), "S"((long)(p2)), "d"((long)(p3)), "r"((long)(p4_)), "r"((long)(p5_)), "r"((long)(p6_)) : __SYSCALL_CLOBBERS ); \
		return __res; \
	}
#elif defined(__aarch64__)

#define DEFN_SYSCALL0(fn, num) \
	long syscall_##fn() { \
		register long __res __asm__ ("x0") = num; \
		__asm__ __volatile__("svc 0" : "=r" (__res) : \
			"r" (__res) \
		); \
		return __res; \
	}

#define DEFN_SYSCALL1(fn, num, P1) \
	long syscall_##fn(P1 p1) { \
		register long __res __asm__ ("x0") = num; \
		register long x1 __asm__("x1") = (long)p1; \
		__asm__ __volatile__("svc 0" : "=r" (__res) : \
			"r" (__res), \
			"r" (x1) \
		); \
		return __res; \
	}

#define DEFN_SYSCALL2(fn, num, P1, P2) \
	long syscall_##fn(P1 p1, P2 p2) { \
		register long __res __asm__ ("x0") = num; \
		register long x1 __asm__("x1") = (long)p1; \
		register long x2 __asm__("x2") = (long)p2; \
		__asm__ __volatile__("svc 0" : "=r" (__res) : \
			"r" (__res), \
			"r" (x1), \
			"r" (x2) \
		); \
		return __res; \
	}

#define DEFN_SYSCALL3(fn, num, P1, P2, P3) \
	long syscall_##fn(P1 p1, P2 p2, P3 p3) { \
		register long __res __asm__ ("x0") = num; \
		register long x1 __asm__("x1") = (long)p1; \
		register long x2 __asm__("x2") = (long)p2; \
		register long x3 __asm__("x3") = (long)p3; \
		__asm__ __volatile__("svc 0" : "=r" (__res) : \
			"r" (__res), \
			"r" (x1), \
			"r" (x2), \
			"r" (x3) \
		); \
		return __res; \
	}

#define DEFN_SYSCALL4(fn, num, P1, P2, P3, P4) \
	long syscall_##fn(P1 p1, P2 p2, P3 p3, P4 p4) { \
		register long __res __asm__ ("x0") = num; \
		register long x1 __asm__("x1") = (long)p1; \
		register long x2 __asm__("x2") = (long)p2; \
		register long x3 __asm__("x3") = (long)p3; \
		register long x4 __asm__("x4") = (long)p4; \
		__asm__ __volatile__("svc 0" : "=r" (__res) : \
			"r" (__res), \
			"r" (x1), \
			"r" (x2), \
			"r" (x3), \
			"r" (x4) \
		); \
		return __res; \
	}

#define DEFN_SYSCALL5(fn, num, P1, P2, P3, P4, P5) \
	long syscall_##fn(P1 p1, P2 p2, P3 p3, P4 p4, P5 p5) { \
		register long __res __asm__ ("x0") = num; \
		register long x1 __asm__("x1") = (long)p1; \
		register long x2 __asm__("x2") = (long)p2; \
		register long x3 __asm__("x3") = (long)p3; \
		register long x4 __asm__("x4") = (long)p4; \
		register long x5 __asm__("x5") = (long)p5; \
		__asm__ __volatile__("svc 0" : "=r" (__res) : \
			"r" (__res), \
			"r" (x1), \
			"r" (x2), \
			"r" (x3), \
			"r" (x4), \
			"r" (x5) \
		); \
		return __res; \
	}

#define DEFN_SYSCALL6(fn, num, P1, P2, P3, P4, P5, P6) \
	long syscall_##fn(P1 p1, P2 p2, P3 p3, P4 p4, P5 p5, P6 p6) { \
		register long __res __asm__ ("x0") = num; \
		register long x1 __asm__("x1") = (long)p1; \
		register long x2 __asm__("x2") = (long)p2; \
		register long x3 __asm__("x3") = (long)p3; \
		register long x4 __asm__("x4") = (long)p4; \
		register long x5 __asm__("x5") = (long)p5; \
		register long x8 __asm__("x8") = (long)p6; \
		__asm__ __volatile__("svc 0" : "=r" (__res) : \
			"r" (__res), \
			"r" (x1), \
			"r" (x2), \
			"r" (x3), \
			"r" (x4), \
			"r" (x5), \
			"r" (x8) \
		); \
		return __res; \
	}

#else
# error "Invalid target, no system call linkage."
#endif


DECL_SYSCALL1(exit, int);
DECL_SYSCALL0(geteuid);
DECL_SYSCALL3(open, const char *, long, mode_t);
DECL_SYSCALL3(read, int, char *, size_t);
DECL_SYSCALL3(write, int, char *, size_t);
DECL_SYSCALL1(close, int);
DECL_SYSCALL2(gettimeofday, void *, void *);
DECL_SYSCALL3(execve, char *, char **, char **);
DECL_SYSCALL0(fork);
DECL_SYSCALL0(getpid);
DECL_SYSCALL1(sbrk, int);
DECL_SYSCALL3(socket, int, int, int);
DECL_SYSCALL1(uname, struct utsname*);
DECL_SYSCALL5(openpty, int *, int *, char *, void *, void *);
DECL_SYSCALL3(seek, int, long, int);
DECL_SYSCALL2(stat, int, void *);
DECL_SYSCALL5(setsockopt,int,int,int,const void*,size_t);
DECL_SYSCALL3(bind,int,const void*,size_t);
DECL_SYSCALL4(accept,int,void*,size_t*,int);
DECL_SYSCALL2(listen,int,int);
DECL_SYSCALL3(connect,int,const void*,size_t);
DECL_SYSCALL2(dup2, int, int);
DECL_SYSCALL0(getuid);
DECL_SYSCALL1(setuid, unsigned int);
DECL_SYSCALL5(getsockopt,int,int,int,void*,size_t*);
DECL_SYSCALL1(reboot,int);
DECL_SYSCALL3(readdir, int, int, void *);
DECL_SYSCALL1(chdir, char *);
DECL_SYSCALL2(getcwd, char *, size_t);
DECL_SYSCALL3(clone, uintptr_t, uintptr_t, void *);
DECL_SYSCALL2(sethostname, char *, size_t);
DECL_SYSCALL2(gethostname, char *, size_t);
DECL_SYSCALL2(mkdir, char *, mode_t);
DECL_SYSCALL2(shm_obtain, const char *, size_t *);
DECL_SYSCALL1(shm_release, const char *);
DECL_SYSCALL2(kill, int, int);
DECL_SYSCALL2(signal, int, void *);
DECL_SYSCALL3(recv,int,void*,int);
DECL_SYSCALL3(send,int,const void*,int);
DECL_SYSCALL0(gettid);
DECL_SYSCALL0(yield);
DECL_SYSCALL2(shutdown, int, int);
DECL_SYSCALL2(sleepabs, unsigned long, unsigned long);
DECL_SYSCALL2(sleep, unsigned long, unsigned long);
DECL_SYSCALL3(ioctl, int, unsigned long, void *);
DECL_SYSCALL2(access, char *, int);
DECL_SYSCALL2(statf, char *, void *);
DECL_SYSCALL2(chmod, char *, int);
DECL_SYSCALL1(umask, mode_t);
DECL_SYSCALL1(unlink, char *);
DECL_SYSCALL3(waitpid, int, int *, int);
DECL_SYSCALL1(pipe,  int *);
DECL_SYSCALL5(mount, char *, char *, char *, unsigned long, void *);
DECL_SYSCALL2(symlink, const char *, const char *);
DECL_SYSCALL3(readlink, char *, char *, int);
DECL_SYSCALL2(lstat, char *, void *);
DECL_SYSCALL2(fswait,int,int*);
DECL_SYSCALL3(fswait2,int,int*,int);
DECL_SYSCALL3(chown,char*,int,int);
DECL_SYSCALL0(setsid);
DECL_SYSCALL2(setpgid,int,int);
DECL_SYSCALL1(getpgid,int);
DECL_SYSCALL4(fswait3, int, int*, int, int*);
DECL_SYSCALL0(getgid);
DECL_SYSCALL0(getegid);
DECL_SYSCALL1(setgid, unsigned int);
DECL_SYSCALL2(getgroups, int, int*);
DECL_SYSCALL2(setgroups, int, const int*);
DECL_SYSCALL1(times, struct tms*);
DECL_SYSCALL4(ptrace, int, int, void*, void*);
DECL_SYSCALL2(settimeofday, void *, void *);
DECL_SYSCALL0(nproc);
DECL_SYSCALL3(insmod, int, int, char**);

DECL_SYSCALL0(getppid);
DECL_SYSCALL1(set_tls_base, uintptr_t);
DECL_SYSCALL1(sigpending, sigset_t *);
DECL_SYSCALL1(sigsuspend,const sigset_t *);
DECL_SYSCALL2(eaccess, char *, int);
DECL_SYSCALL2(fchmod, int, int);
DECL_SYSCALL2(ftruncate, int, off_t);
DECL_SYSCALL2(getrusage, int, struct rusage*);
DECL_SYSCALL2(munmap, void*, size_t);
DECL_SYSCALL2(pipe2, int *, int);
DECL_SYSCALL2(rename, const char *, const char *);
DECL_SYSCALL2(setregid, gid_t, gid_t);
DECL_SYSCALL2(setreuid, uid_t, uid_t);
DECL_SYSCALL2(sigwait, const sigset_t *,int *);
DECL_SYSCALL2(truncate, char *, off_t);
DECL_SYSCALL3(dup3, int, int, int);
DECL_SYSCALL3(fchown, int, int, int);
DECL_SYSCALL3(fcntl, int, int, long);
DECL_SYSCALL3(getpeername, int,void*,size_t*);
DECL_SYSCALL3(getsockname, int,void*,size_t*);
DECL_SYSCALL3(lchown, const char *, uid_t, gid_t);
DECL_SYSCALL3(setresgid, gid_t, gid_t, gid_t);
DECL_SYSCALL3(setresuid, uid_t, uid_t, uid_t);
DECL_SYSCALL3(sigaction, int, struct sigaction*, struct sigaction*);
DECL_SYSCALL3(sigprocmask, int, const sigset_t * restrict, sigset_t* restrict);
DECL_SYSCALL3(sigqueue, pid_t, int, uintptr_t);
DECL_SYSCALL4(pread, int, void *, size_t, off_t);
DECL_SYSCALL4(pwrite, int, const void *, size_t, off_t);
DECL_SYSCALL6(mmap, void*, size_t, int, int, int, off_t);

_End_C_Header

