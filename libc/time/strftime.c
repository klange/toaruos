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
		return sprintf(s, "[tm is null]");
	}
	char * b = s;
	size_t count = 0;
	for (const char *f = fmt; *f; f++) {
		if (*f != '%') {
			count++;
			*b++ = *f;
			if (count == max) return b - s;
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
		switch (*f) {
			case 'a':
				b += sprintf(b, "%s", weekdays_short[tm->tm_wday]);
				break;
			case 'A':
				b += sprintf(b, "%s", weekdays[tm->tm_wday]);
				break;
			case 'h':
			case 'b':
				b += sprintf(b, "%s", months_short[tm->tm_mon]);
				break;
			case 'B':
				b += sprintf(b, "%s", months[tm->tm_mon]);
				break;
			case 'c':
				b += sprintf(b, "%s %s %02d %02d:%02d:%02d %04d",
						weekdays_short[tm->tm_wday],
						months_short[tm->tm_mon],
						tm->tm_mday,
						tm->tm_hour,
						tm->tm_min,
						tm->tm_sec,
						tm->tm_year + 1900);
				break;
			case 'C':
				b += sprintf(b, "%02d", (tm->tm_year + 1900) / 100);
				break;
			case 'd':
				b += sprintf(b, "%02d", tm->tm_mday);
				break;
			case 'D':
				b += sprintf(b, "%02d/%02d/%02d", tm->tm_mon+1, tm->tm_mday, tm->tm_year % 100);
				break;
			case 'e':
				b += sprintf(b, "%2d", tm->tm_mday);
				break;
			case 'F':
				b += sprintf(b, "%04d-%02d-%02d", tm->tm_year + 1900, tm->tm_mon+1, tm->tm_mday);
				break;
			case 'H':
				b += sprintf(b, "%02d", tm->tm_hour);
				break;
			case 'I':
				b += sprintf(b, "%02d", tm->tm_hour == 0 ? 12 : (tm->tm_hour == 12 ? 12 : (tm->tm_hour % 12)));
				break;
			case 'j':
				b += sprintf(b, "%03d", tm->tm_yday);
				break;
			case 'k':
				b += sprintf(b, "%2d", tm->tm_hour);
				break;
			case 'l':
				b += sprintf(b, "%2d", tm->tm_hour == 0 ? 12 : (tm->tm_hour == 12 ? 12 : (tm->tm_hour % 12)));
				break;
			case 'm':
				b += sprintf(b, "%02d", tm->tm_mon+1);
				break;
			case 'M':
				b += sprintf(b, "%02d", tm->tm_min);
				break;
			case 'n':
				b += sprintf(b, "\n");
				break;
			case 'p':
				b += sprintf(b, "%s", tm->tm_hour < 12 ? "AM" : "PM");
				break;
			case 'P':
				b += sprintf(b, "%s", tm->tm_hour < 12 ? "am" : "pm");
				break;
			case 'r':
				b += sprintf(b, "%02d:%02d:%02d %s",
						tm->tm_hour == 0 ? 12 : (tm->tm_hour == 12 ? 12 : (tm->tm_hour % 12)),
						tm->tm_min,
						tm->tm_sec,
						tm->tm_hour < 12 ? "AM" : "PM");
				break;
			case 'R':
				b += sprintf(b, "%02d:%02d",
						tm->tm_hour,
						tm->tm_min);
				break;
			case 'S':
				b += sprintf(b, "%02d", tm->tm_sec);
				break;
			case 't':
				b += sprintf(b, "\t");
				break;
			case 'T':
				b += sprintf(b, "%02d:%02d:%02d",
						tm->tm_hour,
						tm->tm_min,
						tm->tm_sec);
				break;
			case 'u':
				b += sprintf(b, "%d", tm->tm_wday == 0 ? 7 : tm->tm_wday);
				break;
			case 'w':
				b += sprintf(b, "%d", tm->tm_wday);
				break;
			case 'x':
				b += sprintf(b, "%02d/%02d/%02d", tm->tm_mon+1, tm->tm_mday, tm->tm_year % 100);
				break;
			case 'X':
				b += sprintf(b, "%02d:%02d:%02d",
						tm->tm_hour,
						tm->tm_min,
						tm->tm_sec);
				break;
			case 'y':
				b += sprintf(b, "%02d", tm->tm_year % 100);
				break;
			case 'Y':
				b += sprintf(b, "%04d", tm->tm_year + 1900);
				break;
			case 'z':
				b += sprintf(b, "+0000");
				break;
			case 'Z':
				b += sprintf(b, "UTC");
				break;
			case '%':
				b += sprintf(b, "%c", '%');
				break;
			case 'V':
			case 'W':
			case 's':
			case 'U':
			case 'G':
			case 'g':
				b += sprintf(b, "<%c unsupported>", *f);
				break;
		}
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

