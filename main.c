#include <system.h>

/*
 * memcpy
 * Copy from source to destination. Assumes that
 * source and destination are not overlapping.
 */
void *
memcpy(
		void * restrict dest,
		const void * restrict src,
		size_t count
	  ) {
	size_t i;
	unsigned char *a = dest;
	const unsigned char *b = src;
	for ( i = 0; i < count; ++i ) {
		a[i] = b[i];
	}
	return dest;
}

/*
 * memset
 * Set `count` bytes to `val`.
 */
void *
memset(
		void *b,
		int val,
		size_t count
	  ) {
	size_t i;
	unsigned char * dest = b;
	for ( i = 0; i < count; ++i ) {
		dest[i] = (unsigned char)val;
	}
	return b;
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

void beer() {
	int i = 99;
	while (i > 0) {
		if (i == 1) {
			puts ("One bottle of beer on the wall, one bottle of beer. Take one down, pass it around, ");
		} else {
			kprintf("%d bottles of beer on the wall, %d bottles of beer...\n", i, i);
		}
		i--;
		if (i == 1) {
			puts("One bottle of beer on the wall.\n");
		} else {
			kprintf("%d bottles of beer on the wall.\n", i);
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
