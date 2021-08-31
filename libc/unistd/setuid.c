#include <syscall.h>
#include <syscall_nums.h>
#include <sys/types.h>
#include <errno.h>

DEFN_SYSCALL1(setuid, SYS_SETUID, unsigned int);

int setuid(uid_t uid) {
	__sets_errno(syscall_setuid(uid));
}
