/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2011-2014 Kevin Lange
 *
 * Kernel printf implementation
 *
 * Simple, painfully lacking, implementation of printf(),
 * for the kernel of all things.
 */
#include <system.h>
#include <process.h>
#include <va_list.h>
#include <fs.h>

/*
 * Integer to string
 */
static void print_dec(unsigned int value, unsigned int width, char * buf, int * ptr ) {
	unsigned int n_width = 1;
	unsigned int i = 9;
	while (value > i && i < UINT32_MAX) {
		n_width += 1;
		i *= 10;
		i += 9;
	}

	int printed = 0;
	while (n_width + printed < width) {
		buf[*ptr] = '0';
		*ptr += 1;
		printed += 1;
	}

	i = n_width;
	while (i > 0) {
		unsigned int n = value / 10;
		int r = value % 10;
		buf[*ptr + i - 1] = r + '0';
		i--;
		value = n;
	}
	*ptr += n_width;
}

/*
 * Hexadecimal to string
 */
static void print_hex(unsigned int value, unsigned int width, char * buf, int * ptr) {
	int i = width;

	if (i == 0) i = 8;

	unsigned int n_width = 1;
	unsigned int j = 0x0F;
	while (value > j && j < UINT32_MAX) {
		n_width += 1;
		j *= 0x10;
		j += 0x0F;
	}

	while (i > (int)n_width) {
		buf[*ptr] = '0';
		*ptr += 1;
		i--;
	}

	i = (int)n_width;
	while (i-- > 0) {
		buf[*ptr] = "0123456789abcdef"[(value>>(i*4))&0xF];
		*ptr += + 1;
	}
}

/*
 * vasprintf()
 */
size_t vasprintf(char * buf, const char * fmt, va_list args) {
	int i = 0;
	char * s;
	char * b = buf;
	for (const char *f = fmt; *f; f++) {
		if (*f != '%') {
			*b++ = *f;
			continue;
		}
		++f;
		unsigned int arg_width = 0;
		while (*f >= '0' && *f <= '9') {
			arg_width *= 10;
			arg_width += *f - '0';
			++f;
		}
		/* fmt[i] == '%' */
		switch (*f) {
			case 's': /* String pointer -> String */
				s = (char *)va_arg(args, char *);
				if (s == NULL) {
					s = "(null)";
				}
				while (*s) {
					*b++ = *s++;
				}
				break;
			case 'c': /* Single character */
				*b++ = (char)va_arg(args, int);
				break;
			case 'x': /* Hexadecimal number */
				i = b - buf;
				print_hex((unsigned long)va_arg(args, unsigned long), arg_width, buf, &i);
				b = buf + i;
				break;
			case 'd': /* Decimal number */
				i = b - buf;
				print_dec((unsigned long)va_arg(args, unsigned long), arg_width, buf, &i);
				b = buf + i;
				break;
			case '%': /* Escape */
				*b++ = '%';
				break;
			default: /* Nothing at all, just dump it */
				*b++ = *f;
				break;
		}
	}
	/* Ensure the buffer ends in a null */
	*b = '\0';
	return b - buf;

}

static unsigned short * textmemptr = (unsigned short *)0xB8000;
static void placech(unsigned char c, int x, int y, int attr) {
	unsigned short *where;
	unsigned att = attr << 8;
	where = textmemptr + (y * 80 + x);
	*where = c | att;
}

int fprintf(fs_node_t * device, char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	char buffer[1024];
	vasprintf(buffer, fmt, args);
	va_end(args);

	return write_fs(device, 0, strlen(buffer), (uint8_t *)buffer);
}


int sprintf(char * buf, const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	int out = vasprintf(buf, fmt, args);
	va_end(args);
	return out;
}

