#include <unistd.h>
#include <syscall.h>
#include <syscall_nums.h>

DEFN_SYSCALL0(fork, SYS_FORK);

pid_t fork(void) {
	return syscall_fork();
}
