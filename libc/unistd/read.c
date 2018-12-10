#include <unistd.h>
#include <syscall.h>
#include <syscall_nums.h>

DEFN_SYSCALL3(read,  SYS_READ, int, char *, int);

int read(int file, void *ptr, size_t len) {
	return syscall_read(file,ptr,len);
}
