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

	while (1) {
		int result = sigsuspend(&mask);
		fprintf(stderr, "result = %d, errno = %s\n", result, strerror(errno));
	}

	return 0;
}

