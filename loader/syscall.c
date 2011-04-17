#include <syscall.h>

DEFN_SYSCALL1(exit,  0, int)
DEFN_SYSCALL1(print, 1, const char *)
DEFN_SYSCALL3(open,  2, const char *, int, int)
DEFN_SYSCALL3(read,  3, int, char *, int)
DEFN_SYSCALL3(write, 4, int, char *, int)
DEFN_SYSCALL1(close, 5, int)
