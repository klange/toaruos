#include <unistd.h>
#include <errno.h>
#include <syscall.h>
#include <syscall_nums.h>

DEFN_SYSCALL1(pipe, SYS_PIPE, int *);

int pipe(int fildes[2]) {
	__sets_errno(syscall_pipe((int *)fildes));
}

DEFN_SYSCALL2(pipe2, SYS_PIPE2, int *, int);

int pipe2(int fildes[2], int flag) {
	__sets_errno(syscall_pipe2((int *)fildes, flag));
}
