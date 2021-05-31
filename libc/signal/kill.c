#include <signal.h>
#include <syscall.h>
#include <syscall_nums.h>
#include <errno.h>

DEFN_SYSCALL2(kill, SYS_KILL, int, int);

int kill(int pid, int sig) {
	__sets_errno(syscall_kill(pid, sig));
}

