#include <time.h>
#include <sys/time.h>
#include <stdio.h>

#define SEC_DAY 86400

#define fprintf(...)

static struct tm _timevalue;

static int year_is_leap(int year) {
	return ((year % 4 == 0) && ((year % 100 != 0) || (year % 400 == 0)));
}

// 0 was a Thursday

static int day_of_week(long seconds) {
	long day = seconds / SEC_DAY;
	day += 4;
	return day % 7;
}

static long days_in_month(int month, int year) {
	switch(month) {
		case 12:
			return 31;
		case 11:
			return 30;
		case 10:
			return 31;
		case 9:
			return 30;
		case 8:
			return 31;
		case 7:
			return 31;
		case 6:
			return 30;
		case 5:
			return 31;
		case 4:
			return 30;
		case 3:
			return 31;
		case 2:
			return year_is_leap(year) ? 29 : 28;
		case 1:
			return 31;
	}
	return 0;
}

static long secs_of_month(int months, int year) {
	long days = 0;
	for (int i = 1; i < months; ++i) {
		days += days_in_month(months, year);
	}
	return days * SEC_DAY;
}

struct tm *localtime_r(const time_t *timep, struct tm * _timevalue) {

	fprintf(stderr, "Hello world? %p %d\n", _timevalue, *timep);

	long seconds = 0; // this needs to be bigger, but whatever

	long year_sec = 0;

	for (int year = 1970; year < 2100; ++year) {
		fprintf(stderr, "Checking year %d\n", year);
		long added = year_is_leap(year) ? 366 : 365;
		fprintf(stderr, "adding %d...\n", added);
		long secs = added * 86400;

		if (seconds + secs > *timep) {
			_timevalue->tm_year = year - 1900;
			year_sec = seconds;
			fprintf(stderr, "The year is %d, year_sec=%d\n", year, year_sec);
			for (int month = 1; month <= 12; ++month) {
				fprintf(stderr, "Checking %d\n", month);
				secs = days_in_month(month, year) * SEC_DAY;
				fprintf(stderr, "%d vs %d\n", seconds + secs, *timep);
				if (seconds + secs > *timep) {
					fprintf(stderr, "The month is %d.\n", month);
					_timevalue->tm_mon = month - 1;
					for (int day = 1; day <= days_in_month(month, year); ++day) {
						secs = 60 * 60 * 24;
						fprintf(stderr, "Checking day %d, %d vs . %d\n", day, seconds + secs, *timep);
						if (seconds + secs > *timep) {
							fprintf(stderr, "The day is %d.\n", day);
							_timevalue->tm_mday = day;
							for (int hour = 1; hour <= 24; ++hour) {
								secs = 60 * 60;
								if (seconds + secs > *timep) {
									long remaining = *timep - seconds;
									_timevalue->tm_hour = hour - 1;
									_timevalue->tm_min = remaining / 60;
									_timevalue->tm_sec = remaining % 60; // can be 60 on a leap second, ignore that
									_timevalue->tm_wday = day_of_week(*timep); // oh shit
									_timevalue->tm_yday = (*timep - year_sec) / SEC_DAY;
									_timevalue->tm_isdst = 0; // never because UTC
									return _timevalue;
								} else {
									seconds += secs;
								}
							}
							fprintf(stderr, "Failed but this is definitely the right day, returning NULL (seconds = %dbut need %d)\n", seconds, *timep);
							return NULL;
						} else {
							seconds += secs;
						}
					}
					fprintf(stderr, "Failed but this is definitely the right month, returning NULL\n");
					return NULL;
				} else {
					seconds += secs;
				}
			}
			fprintf(stderr, "Failed but this is definitely the right year, returning NULL\n");
			return NULL;
		} else {
			seconds += secs;
		}
	}

	fprintf(stderr, "Uh, no?\n");
	return (void *)0; /// uh what
}

static unsigned int secs_of_years(int years) {
	unsigned int days = 0;
	while (years > 1969) {
		days += 365;
		if (year_is_leap(years)) {
			days++;
		}
		years--;
	}
	return days * 86400;
}


time_t mktime(struct tm *tm) {
	return
	  secs_of_years(tm->tm_year + 1900) +
	  secs_of_month(tm->tm_mon, tm->tm_year + 1900) +
	  (tm->tm_mday - 1) * 86400 +
	  (tm->tm_hour) * 3600 +
	  (tm->tm_min) * 60 +
	  (tm->tm_sec);

}

struct tm * gmtime_r(const time_t * timep, struct tm * tm) {
	return localtime_r(timep, tm);
}

struct tm * localtime(const time_t *timep) {
	return localtime_r(timep, &_timevalue);
}

struct tm *gmtime(const time_t *timep) {
	return localtime(timep);
}

