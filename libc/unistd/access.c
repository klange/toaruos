#define _GNU_SOURCE
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <libc/syscall.h>
#include <sys/syscall.h>

DEFN_SYSCALL4(faccessat, SYS_FACCESSAT, int, const char *, int, int);

int faccessat(int fd, const char * path, int amode, int flag) {
	__sets_errno(syscall_faccessat(fd, path, amode, flag));
}

int access(const char *pathname, int mode) {
	return faccessat(AT_FDCWD, pathname, mode, 0);
}

int eaccess(const char *pathname, int mode) {
	return faccessat(AT_FDCWD, pathname, mode, AT_EACCESS);
}
