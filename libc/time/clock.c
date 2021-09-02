#include <time.h>
#include <sys/times.h>

clock_t clock(void) {
	struct tms timeValues;
	times(&timeValues);
	return timeValues.tms_utime;
}
