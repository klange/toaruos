#include <unistd.h>
#include <libc/syscall.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <errno.h>

DEFN_SYSCALL1(setgid, SYS_SETGID, unsigned int);

int setgid(gid_t uid) {
	__sets_errno(syscall_setgid(uid));
}

DEFN_SYSCALL2(setregid, SYS_SETREGID, gid_t, gid_t);

int setregid(gid_t rgid, gid_t egid) {
	__sets_errno(syscall_setregid(rgid, egid));
}

DEFN_SYSCALL3(setresgid, SYS_SETRESGID, gid_t, gid_t, gid_t);

int setresgid(gid_t rgid, gid_t egid, gid_t sgid) {
	__sets_errno(syscall_setresgid(rgid, egid, sgid));
}

int setegid(gid_t egid) {
	return setresgid(-1, egid, -1);
}

