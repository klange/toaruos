#include <sys/signal.h>
#include <syscall.h>
#include <syscall_nums.h>
#include <errno.h>

DEFN_SYSCALL3(sigaction, SYS_SIGACTION, int, struct sigaction*, struct sigaction*);

int sigaction(int signum, struct sigaction *act, struct sigaction *oldact) {
	__sets_errno(syscall_sigaction(signum, act, oldact));
}

