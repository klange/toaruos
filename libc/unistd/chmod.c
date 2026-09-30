#include <errno.h>
#include <fcntl.h>
#include <libc/syscall.h>
#include <sys/syscall.h>
#include <sys/stat.h>

DEFN_SYSCALL2(fchmod, SYS_FCHMOD, int, int);

int fchmod(int fd, mode_t mode) {
	__sets_errno(syscall_fchmod(fd, mode));
}

DEFN_SYSCALL4(fchmodat, SYS_FCHMODAT, int, const char *, mode_t, int);

int fchmodat(int fd, const char * path, mode_t mode, int flag) {
	__sets_errno(syscall_fchmodat(fd, path, mode, flag));
}

int chmod(const char *path, mode_t mode) {
	return fchmodat(AT_FDCWD, path, mode, 0);
}

