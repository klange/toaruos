#include <unistd.h>
#include <errno.h>
#include <syscall.h>
#include <syscall_nums.h>

DEFN_SYSCALL0(getpid, SYS_GETPID);

pid_t getpid(void) {
	return syscall_getpid();
}

pid_t getppid(void) {
	errno = ENOTSUP;
	return -1;
}
