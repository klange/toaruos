#include <unistd.h>
#include <syscall.h>
#include <syscall_nums.h>

DEFN_SYSCALL0(geteuid, SYS_GETEUID);

uid_t geteuid(void) {
	return syscall_geteuid();
}
