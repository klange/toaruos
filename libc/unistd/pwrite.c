#include <unistd.h>
#include <errno.h>
#include <libc/syscall.h>
#include <sys/syscall.h>

DEFN_SYSCALL4(pwrite, SYS_PWRITE, int, const void *, size_t, off_t);

ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset) {
	__sets_errno(syscall_pwrite(fd,buf,count,offset));
}

