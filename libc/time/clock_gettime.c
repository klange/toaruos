#include <time.h>
#include <errno.h>
#include <sys/time.h>

int clock_gettime(clockid_t clk_id, struct timespec *tp) {
	if (clk_id < 0 || clk_id > 1) {
		errno = EINVAL;
		return -1;
	}
	struct timeval t;
	gettimeofday(&t, NULL);

	tp->tv_sec  = t.tv_sec;
	tp->tv_nsec = t.tv_usec * 1000;

	return 0;
}
