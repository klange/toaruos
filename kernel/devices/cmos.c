/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2011-2014 Kevin Lange
 *
 * CMOS Driver
 *
 */

#include <system.h>

/* CMOS values are stored like so:
 * Say it's 8:42 AM, then the values are stored as:
 * 0x08, 0x42... why this was a good idea, I have no
 * clue, but that's how it usually is.
 *
 * This function will convert between this "BCD" format
 * and regular decimal integers. */
#define from_bcd(val)  ((val / 16) * 10 + (val & 0xf))

void
cmos_dump(
		uint16_t * values
		) {
	uint16_t index;
	for (index = 0; index < 128; ++index) {
		outportb(0x70, index);
		values[index] = inportb(0x71);
	}
}

/**
 * Get the current month and day.
 *
 * @param month Pointer to a short to store the month
 * @param day   Pointer to a short to store the day
 */
void
get_date(
		uint16_t * month,
		uint16_t * day
		) {
	uint16_t values[128]; /* CMOS dump */
	cmos_dump(values);

	*month = from_bcd(values[8]);
	*day   = from_bcd(values[7]);
}

/**
 * Get the current time.
 *
 * @param hours   Pointer to a short to store the current hour (/24)
 * @param minutes Pointer to a short to store the current minute
 * @param seconds Pointer to a short to store the current second
 */
void
get_time(
		uint16_t * hours,
		uint16_t * minutes,
		uint16_t * seconds
		) {
	uint16_t values[128]; /* CMOS dump */
	cmos_dump(values);

	*hours   = from_bcd(values[4]);
	*minutes = from_bcd(values[2]);
	*seconds = from_bcd(values[0]);
}

uint32_t secs_of_years(int years) {
	uint32_t days = 0;
	years += 2000;
	while (years > 1969) {
		days += 365;
		if (years % 4 == 0) {
			if (years % 100 == 0) {
				if (years % 400 == 0) {
					days++;
				}
			} else {
				days++;
			}
		}
		years--;
	}
	return days * 86400;
}

uint32_t secs_of_month(int months, int year) {
	year += 2000;

	uint32_t days = 0;
	switch(months) {
		case 11:
			days += 30;
		case 10:
			days += 31;
		case 9:
			days += 30;
		case 8:
			days += 31;
		case 7:
			days += 31;
		case 6:
			days += 30;
		case 5:
			days += 31;
		case 4:
			days += 30;
		case 3:
			days += 31;
		case 2:
			days += 28;
			if ((year % 4 == 0) && ((year % 100 != 0) || (year % 400 == 0))) {
				days++;
			}
		case 1:
			days += 31;
		default:
			break;
	}
	return days * 86400;
}

uint32_t boot_time = 0;

uint32_t read_cmos(void) {
	uint16_t values[128];
	cmos_dump(values);

	/* Math Time */
	uint32_t time = secs_of_years(from_bcd(values[9]) - 1) +
					secs_of_month(from_bcd(values[8]) - 1, from_bcd(values[9])) + 
					(from_bcd(values[7]) - 1) * 86400 +
					(from_bcd(values[4])) * 3600 +
					(from_bcd(values[2])) * 60 +
					from_bcd(values[0]) +
					0;
	return time;
}

int gettimeofday(struct timeval * t, void *z) {
	t->tv_sec = boot_time + timer_ticks + timer_drift;
	t->tv_usec = timer_subticks * 1000;
	return 0;
}

uint32_t now(void) {
	struct timeval t;
	gettimeofday(&t, NULL);
	return t.tv_sec;
}

