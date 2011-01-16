#include <system.h>

/*
 * memcpy
 * Copy from source to destination. Assumes that
 * source and destination are not overlapping.
 */
unsigned char *
memcpy(
		unsigned char *dest,
		const unsigned char *src,
		int count
	  ) {
	int i;
	i = 0;
	for ( ; i < count; ++i ) {
		dest[i] = src[i];
		
	}
	return dest;
}

/*
 * memset
 * Set `count` bytes to `val`.
 */
unsigned char *
memset(
		unsigned char *dest,
		unsigned char val,
		int count
	  ) {
	int i;
	i = 0;
	for ( ; i < count; ++i ) {
		dest[i] = val;
	}
	return dest;
}

/*
 * memsetw
 * Set `count` shorts to `val`.
 */
unsigned short *
memsetw(
		unsigned short *dest,
		unsigned short val,
		int count
	  ) {
	int i;
	i = 0;
	for ( ; i < count; ++i ) {
		dest[i] = val;
	}
	return dest;
}

/*
 * strlen
 * Returns the length of a given `str`.
 */
int
strlen(
		const char *str
	  ) {
	int i = 0;
	while (str[i] != (char)0) {
		++i;
	}
	return i;
}

/*
 * inportb
 * Read from an I/O port.
 */
unsigned char
inportb(
		unsigned short _port
	   ) {
	unsigned char rv;
	__asm__ __volatile__ ("inb %1, %0" : "=a" (rv) : "dN" (_port));
	return rv;
}

/*
 * outportb
 * Write to an I/O port.
 */
void
outportb(
		unsigned short _port,
		unsigned char _data
		) {
	__asm__ __volatile__ ("outb %1, %0" : : "dN" (_port), "a" (_data));
}

void putsmallint(int i) {
	if (i > 89) { putch('9'); }
	else if (i > 79) { putch('8'); }
	else if (i > 69) { putch('7'); }
	else if (i > 59) { putch('6'); }
	else if (i > 49) { putch('5'); }
	else if (i > 39) { putch('4'); }
	else if (i > 29) { putch('3'); }
	else if (i > 19) { putch('2'); }
	else if (i > 9)  { putch('1'); }
	if (i % 10 == 9) { putch('9'); }
	else if (i % 10 == 8) { putch('8'); }
	else if (i % 10 == 7) { putch('7'); }
	else if (i % 10 == 6) { putch('6'); }
	else if (i % 10 == 5) { putch('5'); }
	else if (i % 10 == 4) { putch('4'); }
	else if (i % 10 == 3) { putch('3'); }
	else if (i % 10 == 2) { putch('2'); }
	else if (i % 10 == 1) { putch('1'); }
	else if (i % 10 == 0) { putch('0'); }
}

void beer() {
	int i = 99;
	while (i > 0) {
		if (i == 1) {
			puts ("One bottle of beer on the wall, one bottle of beer. Take one down, pass it around, ");
		} else {
			putsmallint(i);
			puts(" bottles of beer on the wall, ");
			putsmallint(i);
			puts(" bottles of beer. Take one down, pass it around, ");
		}
		i--;
		putsmallint(i);
		if (i == 1) {
			puts(" bottle of beer on the wall.\n");
		} else {
			puts(" bottles of beer on the wall.\n");
		}
		timer_wait(3);
	}
	puts("No more bottles of beer on the wall.\n");
}

/*
 * Kernel Entry Point
 */
int
main() {
	gdt_install();
	idt_install();
	isrs_install();
	irq_install();
	__asm__ __volatile__("sti");
	timer_install();
	keyboard_install();
	init_video();
	puts("Good Morning!\n");
	beer();
	for (;;);
	return 0;
}
