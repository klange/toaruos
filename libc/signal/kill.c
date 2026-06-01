#include <signal.h>
#include <libc/syscall.h>
#include <sys/syscall.h>
#include <errno.h>

DEFN_SYSCALL2(kill, SYS_KILL, int, int);

int kill(int pid, int sig) {
	__sets_errno(syscall_kill(pid, sig));
}

