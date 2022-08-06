#include <sys/signal.h>
#include <syscall.h>
#include <syscall_nums.h>
#include <errno.h>

DEFN_SYSCALL1(sigpending, SYS_SIGPENDING, sigset_t *);

int sigpending(sigset_t * set) {
	__sets_errno(syscall_sigpending(set));
}


