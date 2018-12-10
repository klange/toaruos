#include <unistd.h>
#include <errno.h>
#include <syscall.h>
#include <syscall_nums.h>

DEFN_SYSCALL1(pipe, SYS_PIPE, int *);

int pipe(int fildes[2]) {
	__sets_errno(syscall_pipe((int *)fildes));
}
