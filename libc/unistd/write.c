#include <unistd.h>
#include <syscall.h>
#include <syscall_nums.h>

DEFN_SYSCALL3(write, SYS_WRITE, int, char *, int);

ssize_t write(int file, const void *ptr, size_t len) {
	return syscall_write(file,(char *)ptr,len);
}
