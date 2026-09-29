#define _GNU_SOURCE
#include <unistd.h>
#include <errno.h>
#include <libc/syscall.h>
#include <sys/syscall.h>

DEFN_SYSCALL2(access, SYS_ACCESS, char *, int);

int access(const char *pathname, int mode) {
	__sets_errno(syscall_access((char*)pathname, mode));
}

DEFN_SYSCALL2(eaccess, SYS_EACCESS, char *, int);

int eaccess(const char *pathname, int mode) {
	__sets_errno(syscall_eaccess((char*)pathname, mode));
}

DEFN_SYSCALL4(faccessat, SYS_FACCESSAT, int, const char *, int, int);

int faccessat(int fd, const char * path, int amode, int flag) {
	__sets_errno(syscall_faccessat(fd, path, amode, flag));
}
