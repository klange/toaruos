#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

void handler(int sig) {
	fprintf(stderr, "received %d\n", sig);
}

int main(int argc, char * argv[]) {
	signal(SIGINT, handler);
	signal(SIGWINCH, handler);

	sigset_t all, prev;
	sigfillset(&all);
	sigprocmask(SIG_SETMASK,&all,&prev);

	fprintf(stderr, "Ignoring signals and pausing for three seconds.\n");
	sleep(3);
	fprintf(stderr, "Sleep is over, calling sigsuspend.\n");

	int result = sigsuspend(&prev);
	fprintf(stderr, "result = %d, errno = %s\n", result, strerror(errno));

	sigprocmask(SIG_SETMASK,&prev,NULL);
	fprintf(stderr, "Restoring mask\n");

	return 0;
}

