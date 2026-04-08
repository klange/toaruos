#include <unistd.h>
#include <errno.h>
#include <syscall.h>
#include <syscall_nums.h>

DEFN_SYSCALL2(access, SYS_ACCESS, char *, int);

int access(const char *pathname, int mode) {
	__sets_errno(syscall_access((char*)pathname, mode));
}

DEFN_SYSCALL2(eaccess, SYS_EACCESS, char *, int);

int eaccess(const char *pathname, int mode) {
	__sets_errno(syscall_eaccess((char*)pathname, mode));
}

