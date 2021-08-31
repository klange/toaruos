#include <syscall.h>
#include <syscall_nums.h>
#include <sys/types.h>
#include <errno.h>

DEFN_SYSCALL1(setgid, SYS_SETGID, unsigned int);

int setgid(gid_t uid) {
	__sets_errno(syscall_setgid(uid));
}

