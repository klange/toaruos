#include <syscall.h>

DEFN_SYSCALL1(exit,  0, int)
DEFN_SYSCALL1(print, 1, const char *)
DEFN_SYSCALL3(open,  2, const char *, int, int)
DEFN_SYSCALL3(read,  3, int, char *, int)
DEFN_SYSCALL3(write, 4, int, char *, int)
DEFN_SYSCALL1(close, 5, int)
DEFN_SYSCALL2(gettimeofday, 6, void *, void *)
DEFN_SYSCALL3(execve, 7, char *, char **, char **)
DEFN_SYSCALL0(fork, 8)
DEFN_SYSCALL0(getpid, 9)
DEFN_SYSCALL1(sbrk, 10, int)

DEFN_SYSCALL1(wait, 16, unsigned int)
