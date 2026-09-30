#include <errno.h>
#include <fcntl.h>
#include <libc/syscall.h>
#include <sys/syscall.h>
#include <sys/stat.h>

DEFN_SYSCALL3(mkdirat, SYS_MKDIRAT, int, const char *, mode_t);

int mkdirat(int fd, const char *pathname, mode_t mode) {
	__sets_errno(syscall_mkdirat(fd, pathname, mode));
}

int mkdir(const char *pathname, mode_t mode) {
	return mkdirat(AT_FDCWD, pathname, mode);
}

