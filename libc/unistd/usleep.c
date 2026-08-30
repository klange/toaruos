#include <unistd.h>
#include <time.h>

int usleep(useconds_t usec) {
	struct timespec ts;
	ts.tv_sec = usec  / 1000000;
	ts.tv_nsec = (usec % 1000000) * 1000;
	return nanosleep(&ts, NULL);
}

