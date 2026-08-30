#include <unistd.h>
#include <time.h>

unsigned int sleep(unsigned int seconds) {
	struct timespec ts, rem;
	ts.tv_sec = seconds;
	ts.tv_nsec = 0;
	nanosleep(&ts,&rem);
	return rem.tv_sec;
}

