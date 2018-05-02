#include <unistd.h>
#include <errno.h>
#include <syscall.h>

DEFN_SYSCALL1(pipe, 54, int *);

int pipe(int fildes[2]) {
	int ret = syscall_pipe((int *)fildes);
	if (ret < 0) {
		errno = -ret;
		return -1;
	}
	return ret;
}
