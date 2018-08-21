/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 *
 * date - Print the current date and time.
 *
 * TODO: The traditional POSIX version of this tool is supposed
 *       to accept a format *and* allow you to set the time.
 *       We currently lack system calls for setting the time,
 *       but when we add those this should probably be updated.
 *
 *       At the very least, improving this to print the "correct"
 *       default format would be good.
 */
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>

static void show_usage(int argc, char * argv[]) {
	printf(
			"%s - print the time and day\n"
			"\n"
			"usage: %s [-?] +FORMAT\n"
			"\n"
			"    Note: This implementation is not currently capable of\n"
			"          setting the system time.\n"
			"\n"
			" -?     \033[3mshow this help text\033[0m\n"
			"\n", argv[0], argv[0]);
}

int main(int argc, char * argv[]) {
	char * format = "%a %b %d %T %Y";
	struct tm * timeinfo;
	struct timeval now;
	char buf[BUFSIZ] = {0};
	int opt;

	while ((opt = getopt(argc,argv,"?")) != -1) {
		switch (opt) {
			case '?':
				show_usage(argc,argv);
				return 1;
		}
	}

	if (optind < argc && *argv[optind] == '+') {
		format = &argv[optind][1];
	}

	gettimeofday(&now, NULL); //time(NULL);
	timeinfo = localtime((time_t *)&now.tv_sec);

	strftime(buf,BUFSIZ,format,timeinfo);
	puts(buf);
	return 0;
}
