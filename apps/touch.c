/**
 * @brief touch - Create or update file timestamps
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2026 K. Lange
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <err.h>
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>

static int usage(char * argv[]) {
	fprintf(stderr, "usage: %s [-acm] [-r ref_file|-t time|-d date_Time] file...\n", argv[0]);
	return 1;
}

static int atoi2_loud(char * str) {
	if (str[0] < '0' || str[0] > '9') goto _error;
	if (str[1] < '0' || str[1] > '9') goto _error;

	return (str[0] - '0') * 10 + (str[1] - '0');

_error:
	errx(1, "invalid time string");
	return 0;
}

static struct timespec make_time(char * str) {
	struct timeval now;
	gettimeofday(&now, NULL);
	struct tm *t = localtime((time_t *)&now.tv_sec);

	char * seconds = strchrnul(str, '.');
	t->tm_sec = *seconds ? atoi2_loud(seconds + 1) : 0;
	*seconds = '\0';

	int year_prefix = 0;

	switch (strlen(str)) {
		case 12:
			/* CC */
			year_prefix = 1;
			t->tm_year = atoi2_loud(str) * 100 - 1900;
			str += 2;
			/* fallthrough */
		case 10:
			/* YY */
			if (year_prefix) {
				t->tm_year = (t->tm_year / 100) * 100 + atoi2_loud(str);
			} else {
				t->tm_year = atoi2_loud(str);
				if (t->tm_year < 69) t->tm_year += 100;
			}
			str += 2;
			/* fallthrough */
		case 8:
			/* MMDDhhmm */
			t->tm_mon  = atoi2_loud(str) - 1;
			t->tm_mday = atoi2_loud(str+2);
			t->tm_hour = atoi2_loud(str+4);
			t->tm_min  = atoi2_loud(str+6);
			break;
		default:
			errx(1, "invalid time string");
			break;
	}

	struct timespec ts;
	ts.tv_sec = mktime(t);
	ts.tv_nsec = 0;
	return ts;
}

static struct timespec make_date(char * str) {
	struct timeval now;
	gettimeofday(&now, NULL);
	struct tm *t = localtime((time_t *)&now.tv_sec);

	if (strlen(str) < 19 || str[4] != '-' || str[7] != '-' ||
		(str[10] != ' ' && str[10] != 'T') || str[13] != ':' || str[16] != ':') errx(1, "invalid date string");

	t->tm_year = (atoi2_loud(str) * 100 + atoi2_loud(str+2)) - 1900;
	t->tm_mon  = atoi2_loud(str+5) - 1;
	t->tm_mday = atoi2_loud(str+8);
	t->tm_hour = atoi2_loud(str+11);
	t->tm_min  = atoi2_loud(str+14);
	t->tm_sec  = atoi2_loud(str+17);

	if (str[strlen(str)-1] == 'Z') {
		/* UTC */
		t->tm_gmtoff = 0;
	}

	struct timespec ts;
	ts.tv_sec = mktime(t);
	ts.tv_nsec = 0;

	if (strlen(str) > 20 && (str[19] == '.' || str[19] == ',')) {
		float frac = atof(str + 19);
		ts.tv_nsec = frac * 1000000000;
	}

	return ts;
}

int main(int argc, char * argv[]) {
	struct timespec omit    = {.tv_nsec = UTIME_OMIT};
	struct timespec desired_m_time = {.tv_nsec = UTIME_NOW};
	struct timespec desired_a_time = {.tv_nsec = UTIME_NOW};
	int set_times = 0;
	int dont_create = 0;
	int opt;

	while ((opt = getopt(argc, argv, "acmd:r:t:")) != -1) {
		switch (opt) {
			case 'a':
				set_times |= 1;
				break;
			case 'm':
				set_times |= 2;
				break;
			case 'c':
				dont_create = 1;
				break;
			case 'r': {
				struct stat st;
				if (stat(optarg, &st) == -1) err(1, "%s", optarg);
				desired_a_time = st.st_atim;
				desired_m_time = st.st_mtim;
				break;
			}
			case 't':
				desired_a_time = desired_m_time = make_time(optarg);
				break;
			case 'd':
				desired_a_time = desired_m_time = make_date(optarg);
				break;
			default:
				return usage(argv);
		}
	}

	if (optind == argc) return usage(argv);

	struct timespec times[2];
	times[0] = (set_times == 0 || (set_times & 1)) ? desired_a_time : omit;
	times[1] = (set_times == 0 || (set_times & 2)) ? desired_m_time : omit;

	int out = 0;

	for (int i = optind; i < argc; ++i) {
		if (access(argv[i], F_OK) && errno == ENOENT) {
			if (dont_create) continue;
			int fd = creat(argv[i], S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH);
			if (fd == -1 || futimens(fd, times) == -1) {
				warn(argv[i]);
				out |= 1;
			}
			if (fd != -1) close(fd);
		} else {
			if (utimensat(AT_FDCWD, argv[i], times, 0) == -1) {
				warn(argv[i]);
				out |= 1;
			}
		}
	}

	return out;
}
