#include <sys/reboot.h>
#include <libc/syscall.h>
#include <sys/syscall.h>
#include <errno.h>

DEFN_SYSCALL1(reboot, SYS_REBOOT, int);

int reboot(int op) {
	__sets_errno(syscall_reboot(op));
}

