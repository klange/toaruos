#include <signal.h>
#include <sys/signal.h>
#include <syscall.h>
#include <syscall_nums.h>
#include <errno.h>

DEFN_SYSCALL3(sigprocmask, SYS_SIGPROCMASK, int, const sigset_t * restrict, sigset_t* restrict);

int sigprocmask(int how, const sigset_t * restrict set, sigset_t * restrict oset) {
	__sets_errno(syscall_sigprocmask(how,set,oset));
}
