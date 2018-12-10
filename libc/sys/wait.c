#include <syscall.h>
#include <syscall_nums.h>
#include <errno.h>

DEFN_SYSCALL3(waitpid, SYS_WAITPID, int, int *, int);

int waitpid(int pid, int *status, int options) {
	/* XXX: status, options? */
	__sets_errno(syscall_waitpid(pid, status, options));
}

int wait(int *status) {
	return waitpid(-1, status, 0);
}
