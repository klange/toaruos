#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include <string.h>

static void do_int(int sig) {
	/* boop */
}

int main(int argc, char * argv[]) {
	if (argc < 3) return 1;

	signal(SIGINT, do_int);

	struct timespec a, b;

	a.tv_sec  = strtol(argv[1], NULL, 0);
	a.tv_nsec = strtol(argv[2], NULL, 0);

	b.tv_sec = 0;
	b.tv_nsec = 0;

	int ret = nanosleep(&a, &b);

	fprintf(stderr, "ret=%d, sec=%zu, nsec=%zu, errno=%s\n", ret, b.tv_sec, b.tv_nsec, strerror(errno));

	return 0;
}
