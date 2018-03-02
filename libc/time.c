#include <time.h>
#include <sys/time.h>

time_t time(time_t * out) {
	struct timeval p;
	gettimeofday(&p, NULL);
	if (out) {
		*out = p.tv_sec;
	}
	return p.tv_sec;
}

double difftime(time_t a, time_t b) {
	return (double)(a - b);
}
