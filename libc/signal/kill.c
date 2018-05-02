#include <signal.h>
#include <syscall.h>

DEFN_SYSCALL2(send_signal, 37, uint32_t, uint32_t);

int kill(int pid, int sig) {
	return syscall_send_signal(pid, sig);
}

