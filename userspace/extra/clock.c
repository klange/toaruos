/*
 * clock
 *
 * Displays the current time in the upper right corner
 * of your terminal session; forks on startup.
 */

#include <stdio.h>
#include <stdint.h>
#include <syscall.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>

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
