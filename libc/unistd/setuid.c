#include <syscall.h>
#include <sys/types.h>

DEFN_SYSCALL1(setuid, 24, unsigned int);

int setuid(uid_t uid) {
	return syscall_setuid(uid);
}
