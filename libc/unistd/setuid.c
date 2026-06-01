#include <unistd.h>
#include <libc/syscall.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <errno.h>

DEFN_SYSCALL1(setuid, SYS_SETUID, unsigned int);

int setuid(uid_t uid) {
	__sets_errno(syscall_setuid(uid));
}

DEFN_SYSCALL2(setreuid, SYS_SETREUID, uid_t, uid_t);

int setreuid(uid_t ruid, uid_t euid) {
	__sets_errno(syscall_setreuid(ruid, euid));
}

DEFN_SYSCALL3(setresuid, SYS_SETRESUID, uid_t, uid_t, uid_t);

int setresuid(uid_t ruid, uid_t euid, uid_t suid) {
	__sets_errno(syscall_setresuid(ruid, euid, suid));
}

int seteuid(uid_t euid) {
	return setresuid(-1, euid, -1);
}

