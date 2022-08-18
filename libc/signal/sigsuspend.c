#include <signal.h>
#include <sys/signal.h>
#include <syscall.h>
#include <syscall_nums.h>
#include <errno.h>

DEFN_SYSCALL0(sigsuspend_cur, SYS_SIGSUSPEND);

int sigsuspend(const sigset_t * restrict set) {
	sigset_t old;
	sigprocmask(SIG_SETMASK, set, &old);
	int ret = syscall_sigsuspend_cur();
	if (ret < 0) { errno = -ret; ret = -1; }
	sigprocmask(SIG_SETMASK, &old, NULL);
	return ret;
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
