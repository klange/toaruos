#include <signal.h>
#include <syscall.h>
#include <syscall_nums.h>
#include <errno.h>

DEFN_SYSCALL2(send_signal, SYS_KILL, uint32_t, uint32_t);

int kill(int pid, int sig) {
	__sets_errno(syscall_send_signal(pid, sig));
}

