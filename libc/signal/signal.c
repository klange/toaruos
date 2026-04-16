#include <signal.h>
#include <syscall.h>
#include <syscall_nums.h>
#include <errno.h>

DEFN_SYSCALL2(signal, SYS_SIGNAL, int, void *);

sighandler_t signal(int signum, sighandler_t handler) {
	__sets_errno_type(sighandler_t,syscall_signal(signum, (void *)handler));
}
