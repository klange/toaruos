#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include <string.h>

void handler(int sig) {
	fprintf(stderr, "received %d\n", sig);
}

int main(int argc, char * argv[]) {
	signal(SIGINT, handler);
	signal(SIGWINCH, handler);

	sigset_t mask;
	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);
	sigprocmask(SIG_BLOCK, &mask, NULL);

	while (1) {
		int sig = 0;
		int result = sigwait(&mask, &sig);
		fprintf(stderr, "result = %d, sig = %d, errno = %s\n", result, sig, strerror(errno));
	}

	return 0;
}


