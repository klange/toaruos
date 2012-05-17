#ifndef _SYSCALL_H
#define _SYSCALL_H

#define DECL_SYSCALL0(fn)                int syscall_##fn()
#define DECL_SYSCALL1(fn,p1)             int syscall_##fn(p1)
#define DECL_SYSCALL2(fn,p1,p2)          int syscall_##fn(p1,p2)
#define DECL_SYSCALL3(fn,p1,p2,p3)       int syscall_##fn(p1,p2,p3)
#define DECL_SYSCALL4(fn,p1,p2,p3,p4)    int syscall_##fn(p1,p2,p3,p4)
#define DECL_SYSCALL5(fn,p1,p2,p3,p4,p5) int syscall_##fn(p1,p2,p3,p4,p5)

#define DEFN_SYSCALL0(fn, num) \
	int syscall_##fn() { \
		int a; __asm__ __volatile__("int $0x7F" : "=a" (a) : "0" (num)); \
		return a; \
	}

#define DEFN_SYSCALL1(fn, num, P1) \
	int syscall_##fn(P1 p1) { \
		int a; __asm__ __volatile__("int $0x7F" : "=a" (a) : "0" (num), "b" ((int)p1)); \
		return a; \
	}

#define DEFN_SYSCALL2(fn, num, P1, P2) \
	int syscall_##fn(P1 p1, P2 p2) { \
		int a; __asm__ __volatile__("int $0x7F" : "=a" (a) : "0" (num), "b" ((int)p1), "c" ((int)p2)); \
		return a; \
	}

#define DEFN_SYSCALL3(fn, num, P1, P2, P3) \
	int syscall_##fn(P1 p1, P2 p2, P3 p3) { \
		int a; __asm__ __volatile__("int $0x7F" : "=a" (a) : "0" (num), "b" ((int)p1), "c" ((int)p2), "d" ((int)p3)); \
		return a; \
	}

#define DEFN_SYSCALL4(fn, num, P1, P2, P3, P4) \
	int syscall_##fn(P1 p1, P2 p2, P3 p3, P4 p4) { \
		int a; __asm__ __volatile__("int $0x7F" : "=a" (a) : "0" (num), "b" ((int)p1), "c" ((int)p2), "d" ((int)p3), "S" ((int)p4)); \
		return a; \
	}

#define DEFN_SYSCALL5(fn, num, P1, P2, P3, P4, P5) \
	int syscall_##fn(P1 p1, P2 p2, P3 p3, P4 p4, P5 p5) { \
		int a; __asm__ __volatile__("int $0x7F" : "=a" (a) : "0" (num), "b" ((int)p1), "c" ((int)p2), "d" ((int)p3), "S" ((int)p4), "D" ((int)p5)); \
		return a; \
	}

/* Core */
DECL_SYSCALL1(exit, int);
DECL_SYSCALL1(print, const char *);

/* Files */
DECL_SYSCALL1(close,  int);
DECL_SYSCALL3(open,   const char *, int, int);
DECL_SYSCALL3(read,   int, char *, int);
DECL_SYSCALL3(write,  int, char *, int);
DECL_SYSCALL2(fstat,  int, void *);
DECL_SYSCALL1(isatty, int);
DECL_SYSCALL2(link,   char *, char *);
DECL_SYSCALL1(unlink, char *);
DECL_SYSCALL3(lseek,  int, int, int);
DECL_SYSCALL2(stat,   const char *, void *);

/* Process Control */
DECL_SYSCALL0(getpid);
DECL_SYSCALL3(execve, char *, char **, char **);
DECL_SYSCALL0(fork);
DECL_SYSCALL2(kill, int, int);
DECL_SYSCALL1(wait, unsigned int);

/* Memory management */
DECL_SYSCALL1(sbrk, int);

DECL_SYSCALL2(gettimeofday, void *, void *);
DECL_SYSCALL0(getgraphicsaddress);


#endif
/*
 * vim:tabstop=4
 * vim:noexpandtab
 * vim:shiftwidth=4
 */
