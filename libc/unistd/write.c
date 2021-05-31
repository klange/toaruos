#include <unistd.h>
#include <errno.h>
#include <syscall.h>
#include <syscall_nums.h>

DEFN_SYSCALL3(write, SYS_WRITE, int, char *, size_t);

ssize_t write(int file, const void *ptr, size_t len) {
	__sets_errno(syscall_write(file,(char *)ptr,len));
}
