#include <unistd.h>
#include <libc/syscall.h>
#include <sys/syscall.h>
#include <errno.h>

DEFN_SYSCALL0(getuid, SYS_GETUID);

uid_t getuid(void) {
	return syscall_getuid();
}

DEFN_SYSCALL3(getresgid, SYS_GETRESGID, gid_t*, gid_t*, gid_t*);

int getresgid(gid_t *rgid, gid_t *egid, gid_t *sgid) {
	__sets_errno(syscall_getresgid(rgid,egid,sgid));
}

DEFN_SYSCALL3(getresuid, SYS_GETRESUID, uid_t*, uid_t*, uid_t*);

int getresuid(uid_t *ruid, uid_t *euid, uid_t *suid) {
	__sets_errno(syscall_getresuid(ruid,euid,suid));
}

