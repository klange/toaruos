#include <unistd.h>
#include <libc/syscall.h>
#include <sys/syscall.h>

DEFN_SYSCALL0(getegid, SYS_GETEGID);

gid_t getegid(void) {
	return syscall_getegid();
}

