#include <syscall.h>
#include <errno.h>

int waitpid(int pid, int *status, int options) {
	/* XXX: status, options? */
	__sets_errno(syscall_waitpid(pid, status, options));
}

int wait(int *status) {
	return waitpid(-1, status, 0);
}
