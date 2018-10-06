#include <unistd.h>
#include <syscall.h>

DEFN_SYSCALL3(write, 4, int, char *, int);

ssize_t write(int file, const void *ptr, size_t len) {
	return syscall_write(file,(char *)ptr,len);
}
