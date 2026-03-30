#include <unistd.h>
#include <syscall.h>
#include <syscall_nums.h>
#include <errno.h>

DEFN_SYSCALL1(reboot, SYS_REBOOT, int);

int reboot(int op) {
	__sets_errno(syscall_reboot(op));
}

