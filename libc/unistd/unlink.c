#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <libc/syscall.h>
#include <sys/syscall.h>

DEFN_SYSCALL3(unlinkat, SYS_UNLINKAT, int, const char *, int);

int unlinkat(int fd, const char * pathname, int flag) {
	__sets_errno(syscall_unlinkat(fd, pathname, flag));
}

int unlink(const char * pathname) {
	return unlinkat(AT_FDCWD, pathname, AT_REMOVEDIR); /* we allow this */
}

