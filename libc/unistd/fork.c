#include <unistd.h>
#include <libc/syscall.h>
#include <sys/syscall.h>
#include <errno.h>

#include <libc/internal.h>

DEFN_SYSCALL0(fork, SYS_FORK);

pid_t _Fork(void) {
	__sets_errno(syscall_fork());
}

pid_t fork(void) {
	/* TODO atfork things */
	__libc_take_malloc_lock();
	pid_t response = syscall_fork();
	__libc_release_malloc_lock();
	return response;
}
