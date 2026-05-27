#include <syscall.h>
#include <syscall_nums.h>

DEFN_SYSCALL3(insmod, SYS_INSMOD, int, int, char**);

