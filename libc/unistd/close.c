#include <unistd.h>
#include <syscall.h>

DEFN_SYSCALL1(close, 5, int);

int close(int file) {
	return syscall_close(file);
}
