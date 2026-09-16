#include <signal.h>
#include <sys/signal.h>
#include <libc/syscall.h>
#include <sys/syscall.h>
#include <errno.h>

DEFN_SYSCALL1(sigsuspend, SYS_SIGSUSPEND,const sigset_t *);

int sigsuspend(const sigset_t * restrict set) {
	__sets_errno(syscall_sigsuspend(set));
}

DEFN_SYSCALL2(sigwait,SYS_SIGWAIT,const sigset_t *,siginfo_t *);

int sigwaitinfo(const sigset_t * set, siginfo_t * info) {
	int res;
	do {
		res = syscall_sigwait(set,info);
	} while (res == -EINTR);

	if (res < 0) {
		errno = -res;
		return -1;
	}

	return res;
}

int sigwait(const sigset_t * set, int * sig) {
	siginfo_t info;
	if (sigwaitinfo(set, &info) < 0) return -1;

	*sig = info.si_signo;
	return 0;
}
