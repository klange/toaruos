/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Kernel printf implementation
 *
 * Simple, painfully lacking, implementation of printf(),
 * for the kernel of all things.
 */
#include <system.h>
#include <process.h>
#include <va_list.h>

/*
 * Integer to string
 */
static void
parse_num(
		unsigned int value,
		unsigned int base,
		char * buf,
		int * ptr
		) {
	unsigned int n = value / base;
	int r = value % base;
	if (r < 0) {
		r += base;
		--n;
	}
	if (value >= base) {
		parse_num(n, base, buf, ptr);
	}
	buf[*ptr] = (r+'0');
	*ptr = *ptr + 1;
}

/*
 * Hexadecimal to string
 */
static void
parse_hex(
		unsigned int value,
		char * buf,
		int * ptr
		) {
	int i = 8;
	while (i-- > 0) {
		buf[*ptr] = "0123456789abcdef"[(value>>(i*4))&0xF];
		*ptr = *ptr + 1;
	}
}

/*
 * vasprintf()
 */
size_t
vasprintf(char * buf, const char *fmt, va_list args) {
	int i = 0;
	char *s;
	int ptr = 0;
	int len = strlen(fmt);
	for ( ; i < len && fmt[i]; ++i) {
		if (fmt[i] != '%') {
			buf[ptr++] = fmt[i];
			continue;
		}
		/* fmt[i] == '%' */
		switch (fmt[++i]) {
			case 's': /* String pointer -> String */
				s = (char *)va_arg(args, char *);
				while (*s) {
					buf[ptr++] = *s++;
				}
				break;
			case 'c': /* Single character */
				buf[ptr++] = (char)va_arg(args, int);
				break;
			case 'x': /* Hexadecimal number */
				parse_hex((unsigned long)va_arg(args, unsigned long), buf, &ptr);
				break;
			case 'd': /* Decimal number */
				parse_num((unsigned long)va_arg(args, unsigned long), 10, buf, &ptr);
				break;
			case '%': /* Escape */
				buf[ptr++] = '%';
				break;
			default: /* Nothing at all, just dump it */
				buf[ptr++] = fmt[i];
				break;
		}
	}
	/* Ensure the buffer ends in a null */
	buf[ptr] = '\0';
	return ptr;

}

/**
 * (Kernel) Print a formatted string.
 * %s, %c, %x, %d, %%
 *
 * @param fmt Formatted string to print
 * @param ... Additional arguments to format
 */
int
kprintf(
		const char *fmt,
		...
	   ) {
	char buf[1024] = {-1};
	va_list args;
	va_start(args, fmt);
	int out = vasprintf(buf, fmt, args);
	/* We're done with our arguments */
	va_end(args);
	/* Print that sucker */
	serial_string(buf);
	return out;
}

int
sprintf(
		char * buf,
		const char *fmt,
		...
	   ) {
	va_list args;
	va_start(args, fmt);
	int out = vasprintf(buf, fmt, args);
	va_end(args);
	return out;
}

