#include <stdio.h>
#include <libc/syscall.h>
#include <sys/syscall.h>
#include <errno.h>

DEFN_SYSCALL2(rename, SYS_RENAME, const char *, const char *);

int rename(const char * oldpath, const char * newpath) {
	__sets_errno(syscall_rename(oldpath, newpath));
}
