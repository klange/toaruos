#include <unistd.h>
#include <errno.h>
#include <libc/syscall.h>
#include <sys/syscall.h>

DEFN_SYSCALL1(unlink, SYS_UNLINK, char *);

int unlink(const char * pathname) {
	__sets_errno(syscall_unlink((char *)pathname));
}

DEFN_SYSCALL3(unlinkat, SYS_UNLINKAT, int, const char *, int);

int unlinkat(int fd, const char * pathname, int flag) {
	__sets_errno(syscall_unlinkat(fd, pathname, flag));
}
