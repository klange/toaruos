#include <unistd.h>
#include <syscall.h>

DEFN_SYSCALL0(getpid, 9);

pid_t getpid(void) {
	return syscall_getpid();
}
