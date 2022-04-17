#include <time.h>
#include <sys/time.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

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

static struct tm * fill_time(const time_t * timep, struct tm * _timevalue, const char * tzName, int tzOffset) {

	time_t timeVal = *timep + tzOffset;
	_timevalue->_tm_zone_name = tzName;
	_timevalue->_tm_zone_offset = tzOffset;

	long seconds = timeVal < 0 ? -2208988800L : 0;
	long year_sec = 0;

	int startYear = timeVal < 0 ? 1900 : 1970;

	for (int year = startYear; year < 2100; ++year) {
		long added = year_is_leap(year) ? 366 : 365;
		long secs = added * 86400;

		if (seconds + secs > timeVal) {
			_timevalue->tm_year = year - 1900;
			year_sec = seconds;
			for (int month = 1; month <= 12; ++month) {
				secs = days_in_month(month, year) * SEC_DAY;
				if (seconds + secs > timeVal) {
					_timevalue->tm_mon = month - 1;
					for (int day = 1; day <= days_in_month(month, year); ++day) {
						secs = 60 * 60 * 24;
						if (seconds + secs > timeVal) {
							_timevalue->tm_mday = day;
							for (int hour = 1; hour <= 24; ++hour) {
								secs = 60 * 60;
								if (seconds + secs > timeVal) {
									long remaining = timeVal - seconds;
									_timevalue->tm_hour = hour - 1;
									_timevalue->tm_min = remaining / 60;
									_timevalue->tm_sec = remaining % 60;
									_timevalue->tm_wday = day_of_week(timeVal);
									_timevalue->tm_yday = (timeVal - year_sec) / SEC_DAY;
									_timevalue->tm_isdst = 0;
									return _timevalue;
								} else {
									seconds += secs;
								}
							}
							return NULL;
						} else {
							seconds += secs;
						}
					}
					return NULL;
				} else {
					seconds += secs;
				}
			}
			return NULL;
		} else {
			seconds += secs;
		}
	}
	return (void *)0;
}

#define HOURS    3600
#define MINUTES  60

static int get_timezone_offset(void) {
	char * tzOff = getenv("TZ_OFFSET");
	if (!tzOff) return 0;
	char * endptr;
	int out = strtol(tzOff,&endptr,10);
	if (*endptr) return 0;
	return out;
}

struct timezone_offset_db {
	int offset;
	const char * abbrev;
};

static struct timezone_offset_db common_offsets[] = {
	{0, "UTC"},
	{1 * HOURS, "CEST"}, /* Central Europe Standard Time */
	{8 * HOURS, "SST"}, /* Singapore Standard Time */
	{9 * HOURS, "JST"}, /* Japan Standard Time */
	{-5 * HOURS, "EST"}, /* US Eastern Standard */
	{-6 * HOURS, "CST"}, /* US Central Standard */
	{-7 * HOURS, "MST"}, /* US Mountain Standard */
	{-8 * HOURS, "PST"}, /* US Pacific Standard */
	{0, NULL},
};

static char * get_timezone(void) {
	static char buf[20];
	char * tzEnv = getenv("TZ");
	if (!tzEnv) {
		/* Is there an offset? */
		int offset = get_timezone_offset();
		for (struct timezone_offset_db * db = common_offsets; db->abbrev; db++) {
			if (offset == db->offset) return (char*)db->abbrev;
		}
		/* Is it some number of hours? */
		if (offset % HOURS == 0) {
			if (offset > 0) {
				snprintf(buf, 20, "UTC+%d", offset / HOURS);
			} else {
				snprintf(buf, 20, "UTC-%d", -offset / HOURS);
			}
			return buf;
		}
		return "???";
	}
	return tzEnv;
}


struct tm *localtime_r(const time_t *timep, struct tm * _timevalue) {
	return fill_time(timep, _timevalue, get_timezone(), get_timezone_offset());
}

struct tm * gmtime_r(const time_t * timep, struct tm * tm) {
	return fill_time(timep, tm, "UTC", 0);
}

struct tm * localtime(const time_t *timep) {
	return fill_time(timep, &_timevalue, get_timezone(), get_timezone_offset());
}

struct tm *gmtime(const time_t *timep) {
	return fill_time(timep, &_timevalue, "UTC", 0);
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

static long secs_of_month(int months, int year) {
	long days = 0;
	for (int i = 1; i < months; ++i) {
		days += days_in_month(i, year);
	}
	return days * SEC_DAY;
}

time_t mktime(struct tm *tm) {
	return
	  secs_of_years(tm->tm_year + 1899) +
	  secs_of_month(tm->tm_mon + 1, tm->tm_year + 1900) +
	  (tm->tm_mday - 1) * 86400 +
	  (tm->tm_hour) * 3600 +
	  (tm->tm_min) * 60 +
	  (tm->tm_sec) - tm->_tm_zone_offset;
}


