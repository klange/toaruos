/**
 * @file kernel/arch/x86_64/cmos.c
 * @author K. Lange
 * @brief Real-time clock.
 *
 * Provides access to the CMOS RTC for initial boot time and
 * calibrates the TSC to use as a general timing source. IRQ 0
 * handler is also in here because it updates the wall clock time
 * and triggers timeout-based wakeups.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021 K. Lange
 */
#include <errno.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/process.h>
#include <kernel/arch/x86_64/ports.h>
#include <kernel/arch/x86_64/irq.h>
#include <sys/time.h>

uint64_t arch_boot_time = 0; /**< Time (in seconds) according to the CMOS right before we examine the TSC */
uint64_t tsc_basis_time = 0; /**< Accumulated time (in microseconds) on the TSC, when we timed it; eg. how long did boot take */
uint64_t tsc_mhz = 3500;     /**< MHz rating we determined for the TSC. Usually also the core speed? */

/* Crusty old CMOS code follows. */

#define from_bcd(val)  ((val / 16) * 10 + (val & 0xf))
#define CMOS_ADDRESS   0x70
#define CMOS_DATA      0x71

enum {
	CMOS_SECOND = 0,
	CMOS_MINUTE = 2,
	CMOS_HOUR = 4,
	CMOS_DAY = 7,
	CMOS_MONTH = 8,
	CMOS_YEAR = 9
};

/**
 * @brief Read the contents of the RTC CMOS
 *
 * @param values (out) Where to stick the values read.
 */
static void cmos_dump(uint16_t * values) {
	for (uint16_t index = 0; index < 128; ++index) {
		outportb(CMOS_ADDRESS, index);
		values[index] = inportb(CMOS_DATA);
	}
}

/**
 * @brief Check if the CMOS is currently being updated.
 */
static int is_update_in_progress(void) {
	outportb(CMOS_ADDRESS, 0x0a);
	return inportb(CMOS_DATA) & 0x80;
}

/**
 * @brief Poorly convert years to Unix timestamps.
 *
 * @param years Years since 2000
 * @returns Seconds since the Unix epoch, maybe...
 */
static uint64_t secs_of_years(int years) {
	uint64_t days = 0;
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

/**
 * @brief How long was a month in a given year?
 *
 * Tries to do leap year stuff for February.
 *
 * @param months 1~12 calendar month
 * @param year   Years since 2000
 * @return Number of seconds in that month.
 */
static uint64_t secs_of_month(int months, int year) {
	year += 2000;

	uint64_t days = 0;
	switch(months) {
		case 11:
			days += 30; /* fallthrough */
		case 10:
			days += 31; /* fallthrough */
		case 9:
			days += 30; /* fallthrough */
		case 8:
			days += 31; /* fallthrough */
		case 7:
			days += 31; /* fallthrough */
		case 6:
			days += 30; /* fallthrough */
		case 5:
			days += 31; /* fallthrough */
		case 4:
			days += 30; /* fallthrough */
		case 3:
			days += 31; /* fallthrough */
		case 2:
			days += 28;
			if ((year % 4 == 0) && ((year % 100 != 0) || (year % 400 == 0))) {
				days++;
			} /* fallthrough */
		case 1:
			days += 31; /* fallthrough */
		default:
			break;
	}
	return days * 86400;
}

/**
 * @brief Convert the CMOS time to a Unix timestamp.
 *
 * Reads BCD data from the RTC CMOS and does some dumb
 * math to convert the display time to a Unix timestamp.
 *
 * @return Current Unix time
 */
uint64_t read_cmos(void) {
	uint16_t values[128];
	uint16_t old_values[128];

	while (is_update_in_progress());
	cmos_dump(values);

	do {
		memcpy(old_values, values, 128);
		while (is_update_in_progress());
		cmos_dump(values);
	} while ((old_values[CMOS_SECOND] != values[CMOS_SECOND]) ||
		(old_values[CMOS_MINUTE] != values[CMOS_MINUTE]) ||
		(old_values[CMOS_HOUR] != values[CMOS_HOUR])     ||
		(old_values[CMOS_DAY] != values[CMOS_DAY])       ||
		(old_values[CMOS_MONTH] != values[CMOS_MONTH])   ||
		(old_values[CMOS_YEAR] != values[CMOS_YEAR]));

	/* Math Time */
	uint64_t time =
		secs_of_years(from_bcd(values[CMOS_YEAR]) - 1) +
		secs_of_month(from_bcd(values[CMOS_MONTH]) - 1,
		from_bcd(values[CMOS_YEAR])) +
		(from_bcd(values[CMOS_DAY]) - 1) * 86400 +
		(from_bcd(values[CMOS_HOUR])) * 3600 +
		(from_bcd(values[CMOS_MINUTE])) * 60 +
		from_bcd(values[CMOS_SECOND]) + 0;

	return time;
}

/**
 * @brief Helper to read timestamp counter
 */
static inline uint64_t read_tsc(void) {
	uint32_t lo, hi;
	asm volatile ( "rdtsc" : "=a"(lo), "=d"(hi) );
	return ((uint64_t)hi << 32UL) | (uint64_t)lo;
}

/**
 * @brief Exported interface to read timestamp counter.
 *
 * Used by various things in the kernel to get a quick performance
 * timer value. This is always scaled by @c arch_cpu_mhz when it
 * needs to be converted to something user-friendly.
 */
uint64_t arch_perf_timer(void) {
	return read_tsc();
}

/**
 * @brief What to scale performance counter times by.
 *
 * I've called this "arch_cpu_mhz" but I don't know if that's
 * always going to be true, so this may need to be renamed at
 * some point...
 */
size_t arch_cpu_mhz(void) {
	return tsc_mhz;
}

/**
 * @brief Initializes boot time, system time, TSC rate, etc.
 *
 * We determine TSC rate with a one-shot PIT, which seems
 * to work fine... the PIT is the only thing with both reasonable
 * precision and actual known wall-clock configuration.
 *
 * In Bochs, this has a tendency to be 1) completely wrong (usually
 * about half the time that actual execution will run at, in my
 * experiences) and 2) loud, as despite the attempt to turn off
 * the speaker it still seems to beep it (the second channel of the
 * PIT controls the beeper).
 *
 * In QEMU, VirtualBox, VMware, and on all real hardware I've tested,
 * including everything from a ThinkPad T410 to a Surface Pro 6, this
 * has been surprisingly accurate and good enough to use the TSC as
 * our only wall clock source.
 */
void arch_clock_initialize(void) {
	dprintf("tsc: Calibrating system timestamp counter.\n");
	arch_boot_time = read_cmos();
	uintptr_t end_lo, end_hi;
	uint32_t start_lo, start_hi;
	asm volatile (
		/* Disables and sets gating for channel 2 */
		"inb   $0x61, %%al\n"
		"andb  $0xDD, %%al\n"
		"orb   $0x01, %%al\n"
		"outb  %%al, $0x61\n"
		/* Configure channel 2 to one-shot, next two bytes are low/high */
		"movb  $0xB2, %%al\n" /* 0b10110010 */
		"outb  %%al, $0x43\n"
		/* 0x__9b */
		"movb  $0x9B, %%al\n"
		"outb  %%al, $0x42\n"
		"inb   $0x60, %%al\n"
		/*  0x2e__ */
		"movb  $0x2E, %%al\n"
		"outb  %%al, $0x42\n"
		/* Re-enable */
		"inb   $0x61, %%al\n"
		"andb  $0xDE, %%al\n"
		"outb  %%al, $0x61\n"
		/* Pulse high */
		"orb   $0x01, %%al\n"
		"outb  %%al, $0x61\n"
		/* Read TSC and store in vars */
		"rdtsc\n"
		"movl  %%eax, %2\n"
		"movl  %%edx, %3\n"
		/* In QEMU and VirtualBox, this seems to flip low.
		 * On real hardware and VMware it flips high. */
		"inb   $0x61, %%al\n"
		"andb  $0x20, %%al\n"
		"jz   2f\n"
		/* Loop until output goes low? */
	"1:\n"
		"inb   $0x61, %%al\n"
		"andb  $0x20, %%al\n"
		"jnz   1b\n"
		"rdtsc\n"
		"jmp   3f\n"
		/* Loop until output goes high */
	"2:\n"
		"inb   $0x61, %%al\n"
		"andb  $0x20, %%al\n"
		"jz   2b\n"
		"rdtsc\n"
	"3:\n"
		: "=a"(end_lo), "=d"(end_hi), "=r"(start_lo), "=r"(start_hi)
	);

	uintptr_t end   = ((end_hi & 0xFFFFffff)   << 32) | (end_lo & 0xFFFFffff);
	uintptr_t start = ((uintptr_t)(start_hi & 0xFFFFffff) << 32) | (start_lo & 0xFFFFffff);
	tsc_mhz = (end - start) / 10000;
	if (tsc_mhz == 0) tsc_mhz = 2000; /* uh oh */
	tsc_basis_time = start / tsc_mhz;

	dprintf("tsc: TSC timed at %lu MHz..\n", tsc_mhz);
	dprintf("tsc: Boot time is %lus.\n", arch_boot_time);
	dprintf("tsc: Initial TSC timestamp was %luus.\n", tsc_basis_time);
}

#define SUBSECONDS_PER_SECOND 1000000

/**
 * @brief Subdivide ticks into seconds in subticks.
 */
static void update_ticks(uint64_t ticks, uint64_t *timer_ticks, uint64_t *timer_subticks) {
	*timer_subticks = ticks - tsc_basis_time;
	*timer_ticks = *timer_subticks / SUBSECONDS_PER_SECOND;
	*timer_subticks = *timer_subticks % SUBSECONDS_PER_SECOND;
}

/**
 * @brief Exposed interface for wall clock time.
 *
 * Note that while the kernel version of this takes a *z option that is
 * supposed to have timezone information, we don't actually use  it,
 * and I'm pretty sure it's NULL everywhere?
 *
 * We calculate wall time using the TSC, the calculate TSC tick rate,
 * and the boot time retrieved from the CMOS, subdivide the result
 * into seconds and "subseconds" (microseconds), and store that.
 */
int gettimeofday(struct timeval * t, void *z) {
	uint64_t tsc = read_tsc();
	uint64_t timer_ticks, timer_subticks;
	update_ticks(tsc / tsc_mhz, &timer_ticks, &timer_subticks);
	t->tv_sec = arch_boot_time + timer_ticks;
	t->tv_usec = timer_subticks;
	return 0;
}

/**
 * @brief Dumb convenience function for things that just want a Unix timestamp.
 *
 * @return Wall clock time as a Unix timestamp (seconds since the epoch).
 */
uint64_t now(void) {
	struct timeval t;
	gettimeofday(&t, NULL);
	return t.tv_sec;
}

static spin_lock_t _time_set_lock;

/**
 * Set the system clock time
 *
 * TODO: A lot of this time stuff needs to be made more generic,
 *       it's shared pretty directly with aarch64...
 */
int settimeofday(struct timeval * t, void *z) {
	if (!t) return -EINVAL;
	if (t->tv_sec < 0 || t->tv_usec < 0 || t->tv_usec > 1000000) return -EINVAL;

	spin_lock(_time_set_lock);
	uint64_t clock_time = now();
	arch_boot_time += t->tv_sec - clock_time;
	spin_unlock(_time_set_lock);

	return 0;
}


/**
 * @brief Calculate a time in the future.
 *
 * Takes the current raw TSC time and adds @p seconds seconds and @p subseconds microseconds to it.
 * Stores the result seconds and microseconds in @p out_seconds and @p out_subseconds. If
 * @p seconds and @p subseconds are both zero, this is effectively equivalent to @c update_ticks.
 *
 * This is an exposed interface used throughout the kernel, usually to calculate
 * timeouts and yielding delays.
 *
 * This uses raw TSC time, which is not adjusted for either boot time or wall clock time.
 */
void relative_time(unsigned long seconds, unsigned long subseconds, unsigned long * out_seconds, unsigned long * out_subseconds) {
	if (!arch_boot_time) {
		*out_seconds = 0;
		*out_subseconds = 0;
		return;
	}

	uint64_t tsc = read_tsc();
	uint64_t timer_ticks, timer_subticks;
	update_ticks(tsc / tsc_mhz, &timer_ticks, &timer_subticks);
	if (subseconds + timer_subticks >= SUBSECONDS_PER_SECOND) {
		*out_seconds    = timer_ticks + seconds + (subseconds + timer_subticks) / SUBSECONDS_PER_SECOND;
		*out_subseconds = (subseconds + timer_subticks) % SUBSECONDS_PER_SECOND;
	} else {
		*out_seconds    = timer_ticks + seconds;
		*out_subseconds = timer_subticks + subseconds;
	}
}

static uint64_t time_slice_basis = 0; /**< When the last clock update happened */
static spin_lock_t clock_lock = { 0 }; /**< Allow only one core to do this */

/**
 * @brief Update the global timer tick values.
 *
 * Updates process CPU usage times and wakes up any timed sleepers
 * based on the current TSC time.
 */
void arch_update_clock(void) {
	spin_lock(clock_lock);

	/* Obtain TSC timestamp, in microseconds */
	uint64_t clock_ticks = read_tsc() / tsc_mhz;

	/* Convert it to seconds and subseconds */
	uint64_t timer_ticks, timer_subticks;
	update_ticks(clock_ticks, &timer_ticks, &timer_subticks);

	/**
	 * Update per-process quarter-second usage statistics
	 *
	 * XXX I think this was a bad idea and it should be removed.
	 *     We store four quarter-second usage values in a sliding
	 *     array and update them for every process, so that we can
	 *     query CPU% without having to sample, but that's a lot
	 *     more work in the kernel than we need...
	 */
	if (time_slice_basis + SUBSECONDS_PER_SECOND/4 <= clock_ticks) {
		update_process_usage(clock_ticks - time_slice_basis, tsc_mhz);
		time_slice_basis = clock_ticks;
	}
	spin_unlock(clock_lock);

	/* Wake up any processes that have expired timeouts */
	wakeup_sleepers(timer_ticks, timer_subticks);
}

