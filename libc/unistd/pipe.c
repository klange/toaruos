#include <unistd.h>
#include <errno.h>
#include <syscall.h>

DEFN_SYSCALL1(pipe, 54, int *);

int pipe(int fildes[2]) {
	__sets_errno(syscall_pipe((int *)fildes));
}
