#define _TOARU_SOURCE
#include <unistd.h>
#include <time.h>

unsigned int alarm(unsigned int seconds) {
	struct timespec nalarm = {0};
	struct timespec oalarm = {0};
	nalarm.tv_sec = seconds;
	nalarm.tv_nsec = 0;

	if (nanoalarm(&nalarm, &oalarm) == 0) {
		return oalarm.tv_sec;
	}

	return 0;
}
