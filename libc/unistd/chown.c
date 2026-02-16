#include <unistd.h>
#include <errno.h>
#include <syscall.h>
#include <syscall_nums.h>

DEFN_SYSCALL3(chown, SYS_CHOWN, char *, int, int);

int chown(const char * pathname, uid_t owner, gid_t group) {
	__sets_errno(syscall_chown((char*)pathname,owner,group));
}

DEFN_SYSCALL3(fchown, SYS_FCHOWN, int, int, int);

int fchown(int fd, uid_t owner, gid_t group) {
	__sets_errno(syscall_fchown(fd,owner,group));
}

