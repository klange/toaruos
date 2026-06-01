#include <signal.h>
#include <libc/syscall.h>
#include <sys/syscall.h>
#include <errno.h>

DEFN_SYSCALL1(sigpending, SYS_SIGPENDING, sigset_t *);

int sigpending(sigset_t * set) {
	__sets_errno(syscall_sigpending(set));
}


