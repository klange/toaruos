#include <unistd.h>
#include <syscall.h>
#include <syscall_nums.h>

DEFN_SYSCALL0(reboot, SYS_REBOOT);

/* TODO: define reboot() */
