#include <signal.h>
#include <sys/signal.h>
#include <libc/syscall.h>
#include <sys/syscall.h>
#include <errno.h>

DEFN_SYSCALL3(sigaction, SYS_SIGACTION, int, struct sigaction*, struct sigaction*);

int sigaction(int signum, struct sigaction *act, struct sigaction *oldact) {
	__sets_errno(syscall_sigaction(signum, act, oldact));
}

