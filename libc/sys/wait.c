#include <syscall.h>
#include <errno.h>

int waitpid(int pid, int *status, int options) {
	/* XXX: status, options? */
	int i = syscall_waitpid(pid, status, options);
	if (i < 0) {
		errno = -i;
		return -1;
	}
	return i;
}

int wait(int *status) {
	return waitpid(-1, status, 0);
}
