#include <unistd.h>
#include <libc/syscall.h>
#include <sys/syscall.h>

DEFN_SYSCALL0(geteuid, SYS_GETEUID);

uid_t geteuid(void) {
	return syscall_geteuid();
}
