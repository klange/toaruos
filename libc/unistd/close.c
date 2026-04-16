#include <unistd.h>
#include <syscall.h>
#include <syscall_nums.h>
#include <errno.h>

DEFN_SYSCALL1(close, SYS_CLOSE, int);

int close(int file) {
	__sets_errno(syscall_close(file));
}
