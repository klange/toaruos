#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>

int main(int argc, char * argv[]) {
	struct timeval now, later;
	struct tm * timeinfo;
	int up, upa, upb, upc;
	FILE * uptime;
	gettimeofday(&now, NULL);
	uptime = fopen("/proc/uptime","r");
	fscanf(uptime, "%d.%2d", &up, &upa);
	fclose(uptime);

	sleep(5);
	gettimeofday(&later, NULL);
	uptime = fopen("/proc/uptime","r");
	fscanf(uptime, "%d.%2d", &upb, &upc);
	fclose(uptime);

	fprintf(stderr, "Before: %d, %d.%2d\n", now.tv_sec, up, upa);
	fprintf(stderr, "After:  %d, %d.%2d\n", later.tv_sec, upb, upc);
	return 0;
}
