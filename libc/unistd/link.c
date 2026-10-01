#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/syscall.h>
#include <libc/syscall.h>

DEFN_SYSCALL5(linkat, SYS_LINKAT, int, const char*, int, const char*, int);

int linkat(int fd1, const char *path1, int fd2, const char *path2, int flag) {
	__sets_errno(syscall_linkat(fd1, path1, fd2, path2, flag));
}

int link(const char *dest, const char *src) {
	return linkat(AT_FDCWD, dest, AT_FDCWD, src, 0);
}
