#include <stddef.h>
#include <time.h>
#include <sys/time.h>
#include <stdio.h>

static char * weekdays[] = {
	"Sunday",
	"Monday",
	"Tuesday",
	"Wednesday",
	"Thursday",
	"Friday",
	"Saturday"
};

static char * weekdays_short[] = {
	"Sun","Mon","Tue","Wed","Thu","Fri","Sat"
};

static char * months[] = {
	"January",
	"February",
	"March",
	"April",
	"May",
	"June",
	"July",
	"August",
	"September",
	"October",
	"November",
	"December"
};

static char * months_short[] = {
	"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"
};

size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm) {
	if (!tm) {
		if (max < sizeof("[tm is null]")) return 0;
		return sprintf(s, "[tm is null]");
	}
	char * b = s;
	for (const char *f = fmt; *f; f++) {
		if (*f != '%') {
			if (max == 0) return 0;
			max--;
			*b++ = *f;
			continue;
		}
		++f;
		int _alte = 0;
		int _alto = 0;
		if (*f == 'E') {
			_alte = 1;
			++f;
		} else if (*f == '0') {
			_alto = 1;
			++f;
		}
		(void)_alte; /* TODO: Implement these */
		(void)_alto;
		int w = 0;
		switch (*f) {
			case 'a':
				w = snprintf(b, max, "%s", weekdays_short[tm->tm_wday]);
				break;
			case 'A':
				w = snprintf(b, max, "%s", weekdays[tm->tm_wday]);
				break;
			case 'h':
			case 'b':
				w = snprintf(b, max, "%s", months_short[tm->tm_mon]);
				break;
			case 'B':
				w = snprintf(b, max, "%s", months[tm->tm_mon]);
				break;
			case 'c':
				w = snprintf(b, max, "%s %s %02d %02d:%02d:%02d %04d",
						weekdays_short[tm->tm_wday],
						months_short[tm->tm_mon],
						tm->tm_mday,
						tm->tm_hour,
						tm->tm_min,
						tm->tm_sec,
						tm->tm_year + 1900);
				break;
			case 'C':
				w = snprintf(b, max, "%02d", (tm->tm_year + 1900) / 100);
				break;
			case 'd':
				w = snprintf(b, max, "%02d", tm->tm_mday);
				break;
			case 'D':
				w = snprintf(b, max, "%02d/%02d/%02d", tm->tm_mon+1, tm->tm_mday, tm->tm_year % 100);
				break;
			case 'e':
				w = snprintf(b, max, "%2d", tm->tm_mday);
				break;
			case 'F':
				w = snprintf(b, max, "%04d-%02d-%02d", tm->tm_year + 1900, tm->tm_mon+1, tm->tm_mday);
				break;
			case 'H':
				w = snprintf(b, max, "%02d", tm->tm_hour);
				break;
			case 'I':
				w = snprintf(b, max, "%02d", tm->tm_hour == 0 ? 12 : (tm->tm_hour == 12 ? 12 : (tm->tm_hour % 12)));
				break;
			case 'j':
				w = snprintf(b, max, "%03d", tm->tm_yday);
				break;
			case 'k':
				w = snprintf(b, max, "%2d", tm->tm_hour);
				break;
			case 'l':
				w = snprintf(b, max, "%2d", tm->tm_hour == 0 ? 12 : (tm->tm_hour == 12 ? 12 : (tm->tm_hour % 12)));
				break;
			case 'm':
				w = snprintf(b, max, "%02d", tm->tm_mon+1);
				break;
			case 'M':
				w = snprintf(b, max, "%02d", tm->tm_min);
				break;
			case 'n':
				w = snprintf(b, max, "\n");
				break;
			case 'p':
				w = snprintf(b, max, "%s", tm->tm_hour < 12 ? "AM" : "PM");
				break;
			case 'P':
				w = snprintf(b, max, "%s", tm->tm_hour < 12 ? "am" : "pm");
				break;
			case 'r':
				w = snprintf(b, max, "%02d:%02d:%02d %s",
						tm->tm_hour == 0 ? 12 : (tm->tm_hour == 12 ? 12 : (tm->tm_hour % 12)),
						tm->tm_min,
						tm->tm_sec,
						tm->tm_hour < 12 ? "AM" : "PM");
				break;
			case 'R':
				w = snprintf(b, max, "%02d:%02d",
						tm->tm_hour,
						tm->tm_min);
				break;
			case 's':
				w = snprintf(b, max, "%ld", mktime((struct tm*)tm));
				break;
			case 'S':
				w = snprintf(b, max, "%02d", tm->tm_sec);
				break;
			case 't':
				w = snprintf(b, max, "\t");
				break;
			case 'T':
				w = snprintf(b, max, "%02d:%02d:%02d",
						tm->tm_hour,
						tm->tm_min,
						tm->tm_sec);
				break;
			case 'u':
				w = snprintf(b, max, "%d", tm->tm_wday == 0 ? 7 : tm->tm_wday);
				break;
			case 'w':
				w = snprintf(b, max, "%d", tm->tm_wday);
				break;
			case 'x':
				w = snprintf(b, max, "%02d/%02d/%02d", tm->tm_mon+1, tm->tm_mday, tm->tm_year % 100);
				break;
			case 'X':
				w = snprintf(b, max, "%02d:%02d:%02d",
						tm->tm_hour,
						tm->tm_min,
						tm->tm_sec);
				break;
			case 'y':
				w = snprintf(b, max, "%02d", tm->tm_year % 100);
				break;
			case 'Y':
				w = snprintf(b, max, "%04d", tm->tm_year + 1900);
				break;
			case 'z': {
				int zone_offset = tm->_tm_zone_offset >= 0 ? tm->_tm_zone_offset : -tm->_tm_zone_offset;
				char sign = tm->_tm_zone_offset >= 0 ? '+' : '-';
				int hour = zone_offset / 3600;
				int mins = (zone_offset / 60) % 60;
				w = snprintf(b, max, "%c%02d%02d", sign, hour, mins);
				break;
			}
			case 'Z':
				w = snprintf(b, max, tm->_tm_zone_name);
				break;
			case '%':
				w = snprintf(b, max, "%c", '%');
				break;
			case 'V':
			case 'W':
			case 'U':
			case 'G':
			case 'g':
				w = snprintf(b, max, "<%c unsupported>", *f);
				break;
		}
		if (w < 0) return 0; /* error while formatting */
		if ((size_t)w >= max) return 0; /* output was truncated */
		max -= w;
		b += w;
	}
	/* Ensure the buffer ends in a null */
	*b = '\0';
	return b - s;
}

static char output[26];
char * asctime(const struct tm *tm) {
	strftime(output, 26, "%a %b %d %T %Y\n", tm);
	return output;
}

