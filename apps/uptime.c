/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2015-2018 K. Lange
 *
 * uptime - Print system uptime
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>

void print_time(void) {
	struct timeval now;
	struct tm * timeinfo;
	char clocktime[10];

	gettimeofday(&now, NULL);
	timeinfo = localtime((time_t *)&now.tv_sec);
	strftime(clocktime, 80, "%H:%M:%S", timeinfo);

	printf(" %s ", clocktime);
}

#define MINUTE (60)
#define HOUR (60 * MINUTE)
#define DAY (24 * HOUR)
void print_seconds(int seconds) {
	if (seconds > DAY) {
		int days = seconds / DAY;
		seconds -= DAY * days;
		printf("%d day%s, ", days, days != 1 ? "s" : "");
	}
	if (seconds > HOUR) {
		int hours = seconds / HOUR;
		seconds -= HOUR * hours;
		int minutes = seconds / MINUTE;
		printf("%2d:%02d", hours, minutes);
		return;
	} else if (seconds > MINUTE) {
		int minutes = seconds / MINUTE;
		printf("%d minute%s,  ", minutes, minutes != 1 ? "s" : "");
		seconds -= MINUTE * minutes;
	}

	printf("%2d second%s", seconds, seconds != 1 ? "s" : "");
}

void print_uptime(void) {
	FILE * f = fopen("/proc/uptime", "r");
	if (!f) return;

	int seconds;

	char buf[1024] = {0};
	fgets(buf, 1024, f);
	char * dot = strchr(buf, '.');
	*dot = '\0';
	dot++;
	dot[3] = '\0';

	seconds = atoi(buf);

	printf("up ");

	print_seconds(seconds);
}

void show_usage(int argc, char * argv[]) {
	printf(
			"uptime - display system uptime information\n"
			"\n"
			"usage: %s [-p]\n"
			"\n"
			" -p     \033[3mshow just the uptime info\033[0m\n"
			" -?     \033[3mshow this help text\033[0m\n"
			"\n", argv[0]);
}

int main(int argc, char * argv[]) {
	int just_pretty_uptime = 0;
	int opt;

	while ((opt = getopt(argc, argv, "?p")) != -1 ) {
		switch (opt) {
			case 'p':
				just_pretty_uptime = 1;
				break;
			case '?':
				show_usage(argc, argv);
				return 0;
		}
	}


	if (!just_pretty_uptime)
		print_time();
	print_uptime();

	printf("\n");

	return 0;
}


