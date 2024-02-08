#include <unistd.h>
#include <errno.h>
#include <syscall.h>
#include <syscall_nums.h>

DEFN_SYSCALL4(pread,  SYS_PREAD, int, void *, size_t, off_t);

ssize_t pread(int fd, void *buf, size_t count, off_t offset) {
	__sets_errno(syscall_pread(fd,buf,count,offset));
}

