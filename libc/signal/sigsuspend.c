#include <signal.h>
#include <sys/signal.h>
#include <syscall.h>
#include <syscall_nums.h>
#include <errno.h>

DEFN_SYSCALL1(sigsuspend, SYS_SIGSUSPEND,const sigset_t *);

int sigsuspend(const sigset_t * restrict set) {
	__sets_errno(syscall_sigsuspend(set));
}

DEFN_SYSCALL2(sigwait,SYS_SIGWAIT,const sigset_t *,int *);

int sigwait(const sigset_t * set, int * sig) {
	int res;
	do {
		res = syscall_sigwait(set,sig);
	} while (res == -EINTR);

	if (res < 0) {
		res = -res;
		errno = res;
	}

	return res;
}
