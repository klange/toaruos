#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <libc/syscall.h>
#include <sys/syscall.h>

DEFN_SYSCALL4(readlinkat, SYS_READLINKAT, int, const char *, char *, size_t);

ssize_t readlinkat(int fd, const char * name, char * buf, size_t len) {
	__sets_errno(syscall_readlinkat(fd, name, buf, len));
}

ssize_t readlink(const char * name, char * buf, size_t len) {
	return readlinkat(AT_FDCWD, name, buf, len);
}

