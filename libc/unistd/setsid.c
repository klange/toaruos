#include <unistd.h>
#include <syscall.h>
#include <syscall_nums.h>
#include <errno.h>

DEFN_SYSCALL0(setsid, SYS_SETSID);

pid_t setsid(void) {
	__sets_errno(syscall_setsid());
}

