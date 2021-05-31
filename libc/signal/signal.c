#include <signal.h>
#include <syscall.h>
#include <syscall_nums.h>

DEFN_SYSCALL2(signal, SYS_SIGNAL, int, void *);

/* XXX This syscall interface is screwy, doesn't allow for good errno handling */
sighandler_t signal(int signum, sighandler_t handler) {
	return (sighandler_t)syscall_signal(signum, (void *)handler);
}
