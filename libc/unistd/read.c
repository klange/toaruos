#include <unistd.h>
#include <syscall.h>

DEFN_SYSCALL3(read,  3, int, char *, int);

int read(int file, void *ptr, size_t len) {
	return syscall_read(file,ptr,len);
}
