/**
 * @file  kprintf.c
 * @brief Kernel printf implementation.
 *
 * This is the newer, 64-bit-friendly printf originally implemented
 * for EFI builds of Kuroko. It was merged into the ToaruOS libc
 * and later became the kernel printf in Misaka. It supports the
 * standard set of width specifiers, '0' or ' ' padding, left or
 * right alignment, and the usermode version has a (rudimentary,
 * inaccurate) floating point printer. This kernel version excludes
 * float support. printf output is passed to callback functions
 * which can write the output to a string buffer or spit them
 * directly at an MMIO port. Support is also present for bounded
 * writes, and we've left @c sprintf out of the kernel entirely
 * in favor of @c snprintf.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2011-2021 K. Lange
 */
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

size_t (*printf_output)(size_t, uint8_t *) = NULL;

#define OUT(c) do { callback(userData, (c)); written++; } while (0)
static size_t print_dec(unsigned long long value, unsigned int width, int (*callback)(void*,char), void * userData, int fill_zero, int align_right, int precision) {
	size_t written = 0;
	unsigned long long n_width = 1;
	unsigned long long i = 9;
	if (precision == -1) precision = 1;

	if (value == 0) {
		n_width = 0;
	} else {
		unsigned long long val = value;
		while (val >= 10UL) {
			val /= 10UL;
			n_width++;
		}
	}

	if (n_width < (unsigned long long)precision) n_width = precision;

	int printed = 0;
	if (align_right) {
		while (n_width + printed < width) {
			OUT(fill_zero ? '0' : ' ');
			printed += 1;
		}

		i = n_width;
		char tmp[100];
		while (i > 0) {
			unsigned long long n = value / 10;
			long long r = value % 10;
			tmp[i - 1] = r + '0';
			i--;
			value = n;
		}
		while (i < n_width) {
			OUT(tmp[i]);
			i++;
		}
	} else {
		i = n_width;
		char tmp[100];
		while (i > 0) {
			unsigned long long n = value / 10;
			long long r = value % 10;
			tmp[i - 1] = r + '0';
			i--;
			value = n;
			printed++;
		}
		while (i < n_width) {
			OUT(tmp[i]);
			i++;
		}
		while (printed < (long long)width) {
			OUT(fill_zero ? '0' : ' ');
			printed += 1;
		}
	}

	return written;
}

/*
 * Hexadecimal to string
 */
static size_t print_hex(unsigned long long value, unsigned int width, int (*callback)(void*,char), void* userData, int fill_zero, int alt, int caps, int align) {
	size_t written = 0;
	int i = width;

	unsigned long long n_width = 1;
	unsigned long long j = 0x0F;
	while (value > j && j < UINT64_MAX) {
		n_width += 1;
		j *= 0x10;
		j += 0x0F;
	}

	if (!fill_zero && align == 1) {
		while (i > (long long)n_width + 2*!!alt) {
			OUT(' ');
			i--;
		}
	}

	if (alt) {
		OUT('0');
		OUT(caps ? 'X' : 'x');
	}

	if (fill_zero && align == 1) {
		while (i > (long long)n_width + 2*!!alt) {
			OUT('0');
			i--;
		}
	}

	i = (long long)n_width;
	while (i-- > 0) {
		char c = (caps ? "0123456789ABCDEF" : "0123456789abcdef")[(value>>(i*4))&0xF];
		OUT(c);
	}

	if (align == 0) {
		i = width;
		while (i > (long long)n_width + 2*!!alt) {
			OUT(' ');
			i--;
		}
	}

	return written;
}

/*
 * vasprintf()
 */
size_t xvasprintf(int (*callback)(void *, char), void * userData, const char * fmt, va_list args) {
	const char * s;
	size_t written = 0;
	for (const char *f = fmt; *f; f++) {
		if (*f != '%') {
			OUT(*f);
			continue;
		}
		++f;
		unsigned int arg_width = 0;
		int align = 1; /* right */
		int fill_zero = 0;
		int big = 0;
		int alt = 0;
		int always_sign = 0;
		int precision = -1;
		while (1) {
			if (*f == '-') {
				align = 0;
				++f;
			} else if (*f == '#') {
				alt = 1;
				++f;
			} else if (*f == '*') {
				arg_width = (int)va_arg(args, int);
				++f;
			} else if (*f == '0') {
				fill_zero = 1;
				++f;
			} else if (*f == '+') {
				always_sign = 1;
				++f;
			} else if (*f == ' ') {
				always_sign = 2;
				++f;
			} else {
				break;
			}
		}
		while (*f >= '0' && *f <= '9') {
			arg_width *= 10;
			arg_width += *f - '0';
			++f;
		}
		if (*f == '.') {
			++f;
			precision = 0;
			if (*f == '*') {
				precision = (int)va_arg(args, int);
				++f;
			} else  {
				while (*f >= '0' && *f <= '9') {
					precision *= 10;
					precision += *f - '0';
					++f;
				}
			}
		}
		if (*f == 'l') {
			big = 1;
			++f;
			if (*f == 'l') {
				big = 2;
				++f;
			}
		}
		if (*f == 'j') {
			big = (sizeof(uintmax_t) == sizeof(unsigned long long) ? 2 :
				   sizeof(uintmax_t) == sizeof(unsigned long) ? 1 : 0);
			++f;
		}
		if (*f == 'z') {
			big = (sizeof(size_t) == sizeof(unsigned long long) ? 2 :
				   sizeof(size_t) == sizeof(unsigned long) ? 1 : 0);
			++f;
		}
		if (*f == 't') {
			big = (sizeof(ptrdiff_t) == sizeof(unsigned long long) ? 2 :
				   sizeof(ptrdiff_t) == sizeof(unsigned long) ? 1 : 0);
			++f;
		}
		/* fmt[i] == '%' */
		switch (*f) {
			case 's': /* String pointer -> String */
				{
					size_t count = 0;
					if (big) {
						return written;
					} else {
						s = (char *)va_arg(args, char *);
						if (s == NULL) {
							s = "(null)";
						}
						if (precision >= 0) {
							while (*s && precision > 0) {
								OUT(*s++);
								count++;
								precision--;
								if (arg_width && count == arg_width) break;
							}
						} else {
							while (*s) {
								OUT(*s++);
								count++;
								if (arg_width && count == arg_width) break;
							}
						}
					}
					while (count < arg_width) {
						OUT(' ');
						count++;
					}
				}
				break;
			case 'c': /* Single character */
				OUT((char)va_arg(args,int));
				break;
			case 'p':
				alt = 1;
				if (sizeof(void*) == sizeof(long long)) big = 2;
				/* fallthrough */
			case 'X':
			case 'x': /* Hexadecimal number */
				{
					unsigned long long val;
					if (big == 2) {
						val = (unsigned long long)va_arg(args, unsigned long long);
					} else if (big == 1) {
						val = (unsigned long)va_arg(args, unsigned long);
					} else {
						val = (unsigned int)va_arg(args, unsigned int);
					}
					written += print_hex(val, arg_width, callback, userData, fill_zero, alt, !(*f & 32), align);
				}
				break;
			case 'i':
			case 'd': /* Decimal number */
				{
					long long val;
					if (big == 2) {
						val = (long long)va_arg(args, long long);
					} else if (big == 1) {
						val = (long)va_arg(args, long);
					} else {
						val = (int)va_arg(args, int);
					}
					if (val < 0) {
						OUT('-');
						val = -val;
					} else if (always_sign) {
						OUT(always_sign == 2 ? ' ' : '+');
					}
					written += print_dec(val, arg_width, callback, userData, fill_zero, align, precision);
				}
				break;
			case 'u': /* Unsigned ecimal number */
				{
					unsigned long long val;
					if (big == 2) {
						val = (unsigned long long)va_arg(args, unsigned long long);
					} else if (big == 1) {
						val = (unsigned long)va_arg(args, unsigned long);
					} else {
						val = (unsigned int)va_arg(args, unsigned int);
					}
					written += print_dec(val, arg_width, callback, userData, fill_zero, align, precision);
				}
				break;
			case '%': /* Escape */
				OUT('%');
				break;
			default: /* Nothing at all, just dump it */
				OUT(*f);
				break;
		}
	}
	return written;
}

struct CBData {
	char * str;
	size_t size;
	size_t written;
};

static int cb_sprintf(void * user, char c) {
	struct CBData * data = user;
	if (data->size > data->written + 1) {
		data->str[data->written] = c;
		data->written++;
		if (data->written < data->size) {
			data->str[data->written] = '\0';
		}
	}
	return 0;
}

int vsnprintf(char *str, size_t size, const char *format, va_list ap) {
	struct CBData data = {str,size,0};
	int out = xvasprintf(cb_sprintf, &data, format, ap);
	cb_sprintf(&data, '\0');
	return out;
}

int snprintf(char * str, size_t size, const char * format, ...) {
	struct CBData data = {str,size,0};
	va_list args;
	va_start(args, format);
	int out = xvasprintf(cb_sprintf, &data, format, args);
	va_end(args);
	cb_sprintf(&data, '\0');
	return out;
}

static int cb_printf(void * user, char c) {
	printf_output(1,(uint8_t*)&c);
	return 0;
}

int printf(const char * fmt, ...) {
	va_list args;
	va_start(args, fmt);
	int out = xvasprintf(cb_printf, NULL, fmt, args);
	va_end(args);
	return out;
}
