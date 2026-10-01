#include <signal.h>
#include <sys/signal.h>
#include <sys/signal_defs.h>
#include <string.h>
#include <errno.h>

int sigemptyset(sigset_t * set) {
	*set = 0;
	return 0;
}

int sigfillset(sigset_t * set) {
	memset(set, 0xFF, sizeof(sigset_t));
	return 0;
}

int sigaddset(sigset_t * set, int signum) {
	if (signum > NUMSIGNALS) return errno = EINVAL, -1;
	*set |= (1UL << signum);
	return 0;
}

int sigdelset(sigset_t * set, int signum) {
	if (signum > NUMSIGNALS) return errno = EINVAL, -1;
	*set &= ~(1UL << signum);
	return 0;
}

int sigismember(sigset_t * set, int signum) {
	if (signum > NUMSIGNALS) return errno = EINVAL, -1;
	return !!(*set & (1UL << signum));
}
