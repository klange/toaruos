#include <unistd.h>
#include <errno.h>
#include <syscall.h>
#include <syscall_nums.h>

DEFN_SYSCALL0(getpid, SYS_GETPID);

pid_t getpid(void) {
	return syscall_getpid();
}

DEFN_SYSCALL0(getppid, SYS_GETPPID);

pid_t getppid(void) {
	return syscall_getppid();
}
