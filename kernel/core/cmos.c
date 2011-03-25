#include <system.h>

#define from_bcd(val)  ((val / 16) * 10 + (val & 0xf))

void
get_time(
		uint16_t * hours,
		uint16_t * minutes,
		uint16_t * seconds
		) {
	uint16_t values[128];
	uint16_t index;
	__asm__ __volatile__ ("cli");
	for (index = 0; index < 128; ++index) {
		outportb(0x70, index);
		values[index] = inportb(0x71);
	}
	__asm__ __volatile__ ("sti");

	*hours   = from_bcd(values[4]);
	*minutes = from_bcd(values[2]);
	*seconds = from_bcd(values[0]);
}
