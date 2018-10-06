#include <unistd.h>
#include <errno.h>
#include <syscall.h>
#include <syscall_nums.h>

DEFN_SYSCALL3(readlink, SYS_READLINK, char *, char *, int);

ssize_t readlink(const char * name, char * buf, size_t len) {
	__sets_errno(syscall_readlink((char*)name, buf, len));
}

