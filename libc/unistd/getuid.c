#include <unistd.h>
#include <libc/syscall.h>
#include <sys/syscall.h>

DEFN_SYSCALL0(getuid, SYS_GETUID);

uid_t getuid(void) {
	return syscall_getuid();
}

