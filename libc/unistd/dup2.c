#include <unistd.h>
#include <syscall.h>
#include <syscall_nums.h>
#include <errno.h>

DEFN_SYSCALL2(dup2, SYS_DUP2, int, int);

int dup2(int oldfd, int newfd) {
	/* System call accepts -1 to do dup() behavior; libc binding should not */
	if (newfd < 0) return (errno = EBADF), -1;
	__sets_errno(syscall_dup2(oldfd, newfd));
}

int dup(int oldfd) {
	__sets_errno(syscall_dup2(oldfd, -1));
}

DEFN_SYSCALL3(dup3, SYS_DUP3, int, int, int);

int dup3(int oldfd, int newfd, int flag) {
	__sets_errno(syscall_dup3(oldfd, newfd, flag));
}
