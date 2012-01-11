/*
 * clock
 *
 * Test shell for ToAruOS
 */

#include <stdio.h>
#include <stdint.h>
#include <syscall.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

struct timeval {
	unsigned int tv_sec;
	unsigned int tv_usec;
};

int main(int argc, char ** argv) {
	if (!fork()) {
		struct timeval now;
		int last = 0;
		struct tm * timeinfo;
		char   buffer[80];
		while (1) {
			syscall_gettimeofday(&now, NULL); //time(NULL);
			if (now.tv_sec != last) {
				last = now.tv_sec;
				timeinfo = localtime((time_t *)&now.tv_sec);
				strftime(buffer, 80, "%H:%M:%S", timeinfo);
				printf("\033[s\033[1;200H\033[9D%s\033[u", buffer);
				fflush(stdout);
			}
		}
	}
	return 0;
}
