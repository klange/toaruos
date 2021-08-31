#include <unistd.h>
#include <syscall.h>
#include <syscall_nums.h>

DEFN_SYSCALL0(getegid, SYS_GETEGID);

gid_t getegid(void) {
	return syscall_getegid();
}

