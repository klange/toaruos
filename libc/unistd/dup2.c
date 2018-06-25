#include <unistd.h>
#include <syscall.h>

DEFN_SYSCALL2(dup2, 22, int, int);

int dup2(int oldfd, int newfd) {
	return syscall_dup2(oldfd, newfd);
}

int dup(int oldfd) {
	return dup2(oldfd, -1);
}
