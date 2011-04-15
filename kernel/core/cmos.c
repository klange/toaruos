/*
 * CMOS Driver
 *
 * Part of the ToAruOS Kernel
 * (C) 2011 Kevin Lange
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
	__asm__ __volatile__ ("cli");
	for (index = 0; index < 128; ++index) {
		outportb(0x70, index);
		values[index] = inportb(0x71);
	}
	__asm__ __volatile__ ("sti");
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

/*
 * vim:tabstop=4
 * vim:noexpandtab
 */
