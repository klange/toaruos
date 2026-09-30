#include <stdio.h>
#include <fcntl.h>
#include <libc/syscall.h>
#include <sys/syscall.h>
#include <errno.h>

DEFN_SYSCALL4(renameat, SYS_RENAMEAT, int, const char *, int, const char*);

int renameat(int oldfd, const char * oldpath, int newfd, const char * newpath) {
	__sets_errno(syscall_renameat(oldfd, oldpath, newfd, newpath));
}

int rename(const char * oldpath, const char * newpath) {
	return renameat(AT_FDCWD, oldpath, AT_FDCWD, newpath);
}

