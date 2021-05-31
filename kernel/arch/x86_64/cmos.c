/**
 * @file kernel/arch/x86_64/cmos.c
 * @author K. Lange
 * @brief Real-time clock.
 *
 * Provides access to the CMOS RTC for initial boot time and
 * calibrates the TSC to use as a general timing source. IRQ 0
 * handler is also in here because it updates the wall clock time
 * and triggers timeout-based wakeups.
 */
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/process.h>
#include <kernel/arch/x86_64/ports.h>
#include <kernel/arch/x86_64/irq.h>
#include <sys/time.h>

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

static void cmos_dump(uint16_t * values) {
	for (uint16_t index = 0; index < 128; ++index) {
		outportb(CMOS_ADDRESS, index);
		values[index] = inportb(CMOS_DATA);
	}
}

static int is_update_in_progress(void) {
	outportb(CMOS_ADDRESS, 0x0a);
	return inportb(CMOS_DATA) & 0x80;
}

static uint32_t secs_of_years(int years) {
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

static uint32_t secs_of_month(int months, int year) {
	year += 2000;

	uint32_t days = 0;
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

uint32_t read_cmos(void) {
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
	uint32_t time =
		secs_of_years(from_bcd(values[CMOS_YEAR]) - 1) +
		secs_of_month(from_bcd(values[CMOS_MONTH]) - 1,
		from_bcd(values[CMOS_YEAR])) +
		(from_bcd(values[CMOS_DAY]) - 1) * 86400 +
		(from_bcd(values[CMOS_HOUR])) * 3600 +
		(from_bcd(values[CMOS_MINUTE])) * 60 +
		from_bcd(values[CMOS_SECOND]) + 0;

	return time;
}

static uint64_t boot_time = 0;
uint64_t timer_ticks = 0;
uint64_t timer_subticks = 0;

unsigned long tsc_mhz = 3500; /* XXX */

static inline uint64_t read_tsc(void) {
	uint32_t lo, hi;
	asm volatile ( "rdtsc" : "=a"(lo), "=d"(hi) );
	return ((uint64_t)hi << 32) | (uint64_t)lo;
}

size_t arch_cpu_mhz(void) {
	return tsc_mhz;
}

void arch_clock_initialize(void) {
	boot_time = read_cmos();
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
}

#define SUBTICKS_PER_TICK 1000000
static void update_ticks(void) {
	uint64_t tsc = read_tsc();
	timer_subticks = tsc / tsc_mhz;
	timer_ticks = timer_subticks / SUBTICKS_PER_TICK;
	timer_subticks = timer_subticks % SUBTICKS_PER_TICK;
}

int gettimeofday(struct timeval * t, void *z) {
	update_ticks();
	t->tv_sec = boot_time + timer_ticks;
	t->tv_usec = timer_subticks;
	return 0;
}

uint64_t now(void) {
	struct timeval t;
	gettimeofday(&t, NULL);
	return t.tv_sec;
}


void relative_time(unsigned long seconds, unsigned long subseconds, unsigned long * out_seconds, unsigned long * out_subseconds) {
	update_ticks();
	if (subseconds + timer_subticks >= SUBTICKS_PER_TICK) {
		*out_seconds    = timer_ticks + seconds + (subseconds + timer_subticks) / SUBTICKS_PER_TICK;
		*out_subseconds = (subseconds + timer_subticks) % SUBTICKS_PER_TICK;
	} else {
		*out_seconds    = timer_ticks + seconds;
		*out_subseconds = timer_subticks + subseconds;
	}
}

int cmos_time_stuff(struct regs *r) {
	update_ticks();
	wakeup_sleepers(timer_ticks, timer_subticks);
	irq_ack(0);
	switch_task(1);
	asm volatile (
		".global _ret_from_preempt_source\n"
		"_ret_from_preempt_source:"
	);
	return 1;
}

