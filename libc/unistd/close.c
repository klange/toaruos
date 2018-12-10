#include <unistd.h>
#include <syscall.h>
#include <syscall_nums.h>

DEFN_SYSCALL1(close, SYS_CLOSE, int);

int close(int file) {
	return syscall_close(file);
}
