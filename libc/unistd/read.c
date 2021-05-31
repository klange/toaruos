#include <unistd.h>
#include <errno.h>
#include <syscall.h>
#include <syscall_nums.h>

DEFN_SYSCALL3(read,  SYS_READ, int, char *, size_t);

ssize_t read(int file, void *ptr, size_t len) {
	__sets_errno(syscall_read(file,ptr,len));
}
