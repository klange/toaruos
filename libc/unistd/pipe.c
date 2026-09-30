#include <unistd.h>
#include <errno.h>
#include <libc/syscall.h>
#include <sys/syscall.h>

DEFN_SYSCALL2(pipe2, SYS_PIPE2, int *, int);

int pipe2(int fildes[2], int flag) {
	__sets_errno(syscall_pipe2((int *)fildes, flag));
}

int pipe(int fildes[2]) {
	return pipe2(fildes, 0);
}
