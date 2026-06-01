#include <signal.h>
#include <libc/syscall.h>
#include <sys/syscall.h>
#include <errno.h>

DEFN_SYSCALL3(sigqueue, SYS_SIGQUEUE, pid_t, int, uintptr_t);

int sigqueue(pid_t pid, int sig, union sigval value) {
	__sets_errno(syscall_sigqueue(pid, sig, (uintptr_t)value.sival_ptr));
}


