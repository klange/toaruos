#include <signal.h>
#include <syscall.h>
#include <syscall_nums.h>
#include <stdio.h>

DEFN_SYSCALL2(signal, SYS_SIGNAL, uint32_t, void *);

/* XXX This syscall interface is screwy, doesn't allow for good errno handling */
sighandler_t signal(int signum, sighandler_t handler) {
	return (sighandler_t)syscall_signal(signum, (void *)handler);
}

int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact) {
	fprintf(stderr, "sigaction() is a stub!\n");
	return -1;
}
