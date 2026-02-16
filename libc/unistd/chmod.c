#include <errno.h>
#include <syscall.h>
#include <syscall_nums.h>
#include <sys/stat.h>

DEFN_SYSCALL2(chmod, SYS_CHMOD, char *, int);

int chmod(const char *path, mode_t mode) {
	__sets_errno(syscall_chmod((char *)path, mode));
}

DEFN_SYSCALL2(fchmod, SYS_FCHMOD, int, int);

int fchmod(int fd, mode_t mode) {
	__sets_errno(syscall_fchmod(fd, mode));
}
