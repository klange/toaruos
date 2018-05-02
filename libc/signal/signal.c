#include <signal.h>
#include <syscall.h>

DEFN_SYSCALL2(signal, 38, uint32_t, void *);

sighandler_t signal(int signum, sighandler_t handler) {
	return (sighandler_t)syscall_signal(signum, (void *)handler);
}
