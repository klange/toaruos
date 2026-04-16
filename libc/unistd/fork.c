#include <unistd.h>
#include <syscall.h>
#include <syscall_nums.h>
#include <errno.h>

DEFN_SYSCALL0(fork, SYS_FORK);

pid_t fork(void) {
	__sets_errno(syscall_fork());
}
