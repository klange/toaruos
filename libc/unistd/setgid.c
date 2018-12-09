#include <syscall.h>
#include <syscall_nums.h>
#include <sys/types.h>

DEFN_SYSCALL1(setgid, SYS_SETGID, unsigned int);

int setgid(gid_t gid) {
	return syscall_setgid(gid);
}
