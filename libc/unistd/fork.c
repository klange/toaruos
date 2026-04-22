#include <unistd.h>
#include <syscall.h>
#include <syscall_nums.h>
#include <errno.h>

DEFN_SYSCALL0(fork, SYS_FORK);

extern void __libc_take_malloc_lock(void);
extern void __libc_release_malloc_lock(void);

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
