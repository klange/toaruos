/**
 * date - Print the current date and time.
 *
 * TODO: The traditional POSIX version of this tool is supposed
 *       to accept a format *and* allow you to set the time.
 *       We currently lack system calls for setting the time,
 *       but when we add those this should probably be updated.
 *
 *       At the very least, improving this to print the "correct"
 *       default format would be good.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 */
#include <stdio.h>
#include <string.h>
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

int digits(const char * s, int len) {
	for (int i = 0; i < len; ++i) {
		if (s[i] < '0' || s[i] > '9') return 0;
	}
	return 1;
}

int mmddhhmm(struct tm * tm, const char * str) {
	int month = (str[0]-'0') * 10 + (str[1]-'0');
	int day   = (str[2]-'0') * 10 + (str[3]-'0');
	int hour  = (str[4]-'0') * 10 + (str[5]-'0');
	int min   = (str[6]-'0') * 10 + (str[7]-'0');

	if (month < 1 || month > 12) return 0;
	if (day < 1 || day > 31) return 0;
	if (hour < 0 || hour > 23) return 0;
	if (min < 0 || min > 59) return 0;

	tm->tm_mon = month - 1;
	tm->tm_mday = day;
	tm->tm_hour = hour;
	tm->tm_min = min;

	return 1;
}

int ddyy(struct tm * tm, const char * str) {
	int year = (str[0]-'0') * 1000 + (str[1]-'0') * 100 + (str[2]-'0') * 10 + (str[3]-'0');
	tm->tm_year = year - 1900;
	return 1;
}

int secs(struct tm * tm, const char * str) {
	int sec = (str[0]-'0') * 10 + (str[1]-'0');
	if (sec < 0 || sec > 59) return 0;
	tm->tm_sec = sec;
	return 1;
}

int main(int argc, char * argv[]) {
	char * format = "%a %d %b %Y %T %Z";
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

	gettimeofday(&now, NULL);
	timeinfo = localtime((time_t *)&now.tv_sec);

	if (optind < argc && *argv[optind] == '+') {
		format = &argv[optind][1];
	} else if (optind < argc) {

		int len =strlen(argv[optind]);

		if (len == 8) {
			if (!digits(argv[optind], 8)) goto _invalid;
			if (!mmddhhmm(timeinfo, argv[optind])) goto _invalid;
			goto set_time;
		} else if (len == 11) {
			if (argv[optind][8] != '.') goto _invalid;
			if (!digits(argv[optind], 8) || !digits(argv[optind]+9,2)) goto _invalid;
			if (!mmddhhmm(timeinfo, argv[optind])) goto _invalid;
			if (!secs(timeinfo, argv[optind]+9)) goto _invalid;
			goto set_time;
		} else if (len == 12) {
			if (!digits(argv[optind], 12)) goto _invalid;
			if (!mmddhhmm(timeinfo, argv[optind])) goto _invalid;
			if (!ddyy(timeinfo, argv[optind]+8)) goto _invalid;
			goto set_time;
		} else if (len == 15) {
			if (argv[optind][12] != '.') goto _invalid;
			if (!digits(argv[optind], 12) || !digits(argv[optind]+13,2)) goto _invalid;
			if (!mmddhhmm(timeinfo, argv[optind])) goto _invalid;
			if (!ddyy(timeinfo, argv[optind]+8)) goto _invalid;
			if (!secs(timeinfo, argv[optind]+13)) goto _invalid;
			goto set_time;
		}
_invalid:
		fprintf(stderr, "date: only 'MMDDhhmm', 'MMDDhhmm.ss', 'MMDDhhmmCCYY' and 'MMDDhhmmCCYY.ss' are supported for setting time.\n");
		return 1;

set_time:
		now.tv_usec = 0;
		now.tv_sec = mktime(timeinfo);
		return settimeofday(&now, NULL);
	}

	strftime(buf,BUFSIZ,format,timeinfo);
	puts(buf);
	return 0;
}
