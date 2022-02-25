#pragma once

#include <_cheader.h>
#include <stdint.h>
#include <stddef.h>

_Begin_C_Header

#define DECL_SYSCALL0(fn)                long syscall_##fn()
#define DECL_SYSCALL1(fn,p1)             long syscall_##fn(p1)
#define DECL_SYSCALL2(fn,p1,p2)          long syscall_##fn(p1,p2)
#define DECL_SYSCALL3(fn,p1,p2,p3)       long syscall_##fn(p1,p2,p3)
#define DECL_SYSCALL4(fn,p1,p2,p3,p4)    long syscall_##fn(p1,p2,p3,p4)
#define DECL_SYSCALL5(fn,p1,p2,p3,p4,p5) long syscall_##fn(p1,p2,p3,p4,p5)

#ifdef __x86_64__
#define DEFN_SYSCALL0(fn, num) \
	long syscall_##fn() { \
		long a = num; __asm__ __volatile__("int $0x7F" : "=a" (a) : "a" ((long)a)); \
		return a; \
	}

#define DEFN_SYSCALL1(fn, num, P1) \
	long syscall_##fn(P1 p1) { \
		long __res = num; __asm__ __volatile__("int $0x7F" \
				: "=a" (__res) \
				: "a" (__res), "b" ((long)(p1))); \
		return __res; \
	}

#define DEFN_SYSCALL2(fn, num, P1, P2) \
	long syscall_##fn(P1 p1, P2 p2) { \
		long __res = num; __asm__ __volatile__("int $0x7F" \
				: "=a" (__res) \
				: "a" (__res), "b" ((long)(p1)), "c"((long)(p2))); \
		return __res; \
	}

#define DEFN_SYSCALL3(fn, num, P1, P2, P3) \
	long syscall_##fn(P1 p1, P2 p2, P3 p3) { \
		long __res = num; __asm__ __volatile__("int $0x7F" \
				: "=a" (__res) \
				: "a" (__res), "b" ((long)(p1)), "c"((long)(p2)), "d"((long)(p3))); \
		return __res; \
	}

#define DEFN_SYSCALL4(fn, num, P1, P2, P3, P4) \
	long syscall_##fn(P1 p1, P2 p2, P3 p3, P4 p4) { \
		long __res = num; __asm__ __volatile__("int $0x7F" \
				: "=a" (__res) \
				: "a" (__res), "b" ((long)(p1)), "c"((long)(p2)), "d"((long)(p3)), "S"((long)(p4))); \
		return __res; \
	}

#define DEFN_SYSCALL5(fn, num, P1, P2, P3, P4, P5) \
	long syscall_##fn(P1 p1, P2 p2, P3 p3, P4 p4, P5 p5) { \
		long __res = num; __asm__ __volatile__("int $0x7F" \
				: "=a" (__res) \
				: "a" (__res), "b" ((long)(p1)), "c"((long)(p2)), "d"((long)(p3)), "S"((long)(p4)), "D"((long)(p5))); \
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

#else
# error "Invalid target, no system call linkage."
#endif


DECL_SYSCALL1(exit, int);
DECL_SYSCALL0(geteuid);
DECL_SYSCALL3(open, const char *, int, int);
DECL_SYSCALL3(read, int, char *, size_t);
DECL_SYSCALL3(write, int, char *, size_t);
DECL_SYSCALL1(close, int);
DECL_SYSCALL2(gettimeofday, void *, void *);
DECL_SYSCALL3(execve, char *, char **, char **);
DECL_SYSCALL0(fork);
DECL_SYSCALL0(getpid);
DECL_SYSCALL1(sbrk, int);
DECL_SYSCALL3(socket, int, int, int);
DECL_SYSCALL1(uname, void *);
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
DECL_SYSCALL0(reboot);
DECL_SYSCALL3(readdir, int, int, void *);
DECL_SYSCALL1(chdir, char *);
DECL_SYSCALL2(getcwd, char *, size_t);
DECL_SYSCALL3(clone, uintptr_t, uintptr_t, void *);
DECL_SYSCALL1(sethostname, char *);
DECL_SYSCALL1(gethostname, char *);
DECL_SYSCALL2(mkdir, char *, unsigned int);
DECL_SYSCALL2(shm_obtain, const char *, size_t *);
DECL_SYSCALL1(shm_release, const char *);
DECL_SYSCALL2(kill, int, int);
DECL_SYSCALL2(signal, int, void *);
DECL_SYSCALL3(recv,int,void*,int);
DECL_SYSCALL3(send,int,const void*,int);
DECL_SYSCALL0(gettid);
DECL_SYSCALL0(yield);
DECL_SYSCALL2(sysfunc, int, char **);
DECL_SYSCALL2(shutdown, int, int);
DECL_SYSCALL2(sleepabs, unsigned long, unsigned long);
DECL_SYSCALL2(sleep, unsigned long, unsigned long);
DECL_SYSCALL3(ioctl, int, unsigned long, void *);
DECL_SYSCALL2(access, char *, int);
DECL_SYSCALL2(statf, char *, void *);
DECL_SYSCALL2(chmod, char *, int);
DECL_SYSCALL1(umask, int);
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

_End_C_Header

