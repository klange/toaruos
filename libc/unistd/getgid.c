#include <unistd.h>
#include <syscall.h>
#include <sys/syscall.h>

DEFN_SYSCALL0(getgid, SYS_GETGID);

gid_t getgid(void) {
	return syscall_getgid();
}

