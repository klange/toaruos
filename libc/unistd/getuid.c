#include <unistd.h>
#include <syscall.h>
#include <syscall_nums.h>

DEFN_SYSCALL0(getuid, SYS_GETUID);

uid_t getuid(void) {
	return syscall_getuid();
}

