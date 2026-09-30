#include <errno.h>
#include <libc/syscall.h>
#include <sys/syscall.h>
#include <sys/stat.h>

DEFN_SYSCALL2(mkdir, SYS_MKDIR, char *, mode_t);

int mkdir(const char *pathname, mode_t mode) {
	__sets_errno(syscall_mkdir((char *)pathname, mode));
}

DEFN_SYSCALL3(mkdirat, SYS_MKDIRAT, int, const char *, mode_t);

int mkdirat(int fd, const char *pathname, mode_t mode) {
	__sets_errno(syscall_mkdirat(fd, pathname, mode));
}
