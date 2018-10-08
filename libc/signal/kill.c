#include <signal.h>
#include <syscall.h>
#include <errno.h>

DEFN_SYSCALL2(send_signal, 37, uint32_t, uint32_t);

int kill(int pid, int sig) {
	__sets_errno(syscall_send_signal(pid, sig));
}

