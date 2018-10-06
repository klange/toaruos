#include <unistd.h>
#include <syscall.h>

DEFN_SYSCALL0(fork, 8);

pid_t fork(void) {
	return syscall_fork();
}
