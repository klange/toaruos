#include <syscall.h>
#include <sys/syscall.h>

DEFN_SYSCALL3(insmod, SYS_INSMOD, int, int, char**);

