#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include <sys/signal.h>
#include <sys/wait.h>

void sig_action(int sig, siginfo_t * info, void * ctx) {
	fprintf(stderr, "entered signal handler, %d; info=%p, ctx=%p\n",
		sig, (void*)info, ctx);

	fprintf(stderr, "info->si_signo = %d\n", info->si_signo);
	fprintf(stderr, "info->si_code = %d\n", info->si_code);
	fprintf(stderr, "info->si_value.sival_int = %d\n", info->si_value.sival_int);
	fprintf(stderr, "info->si_addr = %p\n", info->si_addr);

	fprintf(stderr, "sa_pid = %d\n", info->si_pid);
	fprintf(stderr, "sa_uid = %d\n", info->si_uid);
	fprintf(stderr, "sa_status = %d\n", info->si_status);

	ucontext_t * uc = ctx;

	fprintf(stderr, "uc signo is %ld\n", uc->uc__signo);
	fprintf(stderr, "uc link is %p\n", uc->uc_link);

	uint64_t sigset = (uint64_t)uc->uc_sigmask;
	fprintf(stderr, "{");
	for (int i = 1; i < NUMSIGNALS; ++i) {
		uint64_t s = 1ULL << i;
		if (sigset & s) {
			char signame[SIG2STR_MAX] = {0};
			if (i != 0 && !sig2str(i, signame)) {
				fprintf(stderr, "SIG%s", signame);
			} else {
				fprintf(stderr, "%d", i);
			}
			sigset &= ~s;
			if (sigset) {
				fprintf(stderr, "|");
			}
		}
	}
	fprintf(stderr, "}\n");

	if (sig == SIGSEGV) exit(127);
}

int main(int argc, char * argv[]) {
	struct sigaction sa = {0};
	sa.sa_sigaction = sig_action;
	sa.sa_flags = SA_SIGINFO;
	sigaction(SIGINT, &sa, NULL);
	//sigaction(SIGSEGV, &sa, NULL);
	sigaction(SIGCHLD, &sa, NULL);

	volatile int * foo = (volatile int*)0xffffffff00000000;

	pid_t c = fork();

	if (!c) {
		//fprintf(stderr, "%d\n", *foo);
		raise(SIGTSTP);
		exit(123);
	}

	while (1) {
		int res = waitpid(c, NULL, 0);
		fprintf(stderr, "%d\n", res);

		if (res < 0) {
			if (errno == EINTR) continue;
			fprintf(stderr, "errno = %d\n", errno);
			break;
		}
	}
	return 0;

#if 0
	while (1) {
		sleep(1);
		fprintf(stderr, "%d\n", *foo);

	}
#endif
}
