#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <libc/syscall.h>
#include <sys/syscall.h>

DEFN_SYSCALL3(fchown, SYS_FCHOWN, int, int, int);

int fchown(int fd, uid_t owner, gid_t group) {
	__sets_errno(syscall_fchown(fd,owner,group));
}

DEFN_SYSCALL5(fchownat, SYS_FCHOWNAT, int, const char *, uid_t, gid_t, int);

int fchownat(int fd, const char * pathname, uid_t owner, gid_t group, int flag) {
	__sets_errno(syscall_fchownat(fd, pathname, owner, group, flag));
}

int chown(const char * pathname, uid_t owner, gid_t group) {
	return fchownat(AT_FDCWD, pathname, owner, group, 0);
}

int lchown(const char * pathname, uid_t owner, gid_t group) {
	return fchownat(AT_FDCWD, pathname, owner, group, AT_SYMLINK_NOFOLLOW);
}

