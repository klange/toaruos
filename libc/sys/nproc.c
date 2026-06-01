#include <libc/syscall.h>
#include <sys/syscall.h>

DEFN_SYSCALL0(nproc, SYS_NPROC);
