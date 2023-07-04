#include <stdio.h>
#include <string.h>
#include <va_list.h>

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
	char * s;
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
				if (sizeof(void*) == sizeof(long long)) big = 2; /* fallthrough */
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
			case 'G':
			case 'F':
			case 'g': /* supposed to also support e */
			case 'f':
				{
					if (precision == -1) precision = 8;
					double val = (double)va_arg(args, double);
					uint64_t asBits;
					memcpy(&asBits,&val,sizeof(double));
#define SIGNBIT(d) (d & 0x8000000000000000UL)

					/* Extract exponent */
					int64_t exponent = (asBits & 0x7ff0000000000000UL) >> 52;

					/* Fraction part */
					uint64_t fraction = (asBits & 0x000fffffffffffffUL);

					if (exponent == 0x7ff) {
						if (!fraction) {
							if (SIGNBIT(asBits)) {
								OUT('-');
							}
							OUT('i');
							OUT('n');
							OUT('f');
						} else {
							OUT('n');
							OUT('a');
							OUT('n');
						}
						break;
					} else if ((*f == 'g' || *f == 'G') && exponent == 0 && fraction == 0) {
						if (SIGNBIT(asBits)) {
							OUT('-');
						}
						OUT('0');
						break;
					}

					/* Okay, now we can do some real work... */

					int isNegative = !!SIGNBIT(asBits);
					if (isNegative) {
						OUT('-');
						val = -val;
					}

					written += print_dec((unsigned long long)val, arg_width, callback, userData, fill_zero, align, 1);
					OUT('.');
					for (int j = 0; j < ((precision > -1 && precision < 16) ? precision : 16); ++j) {
						if ((unsigned long long)(val * 100000.0) % 100000 == 0 && j != 0) break;
						val = val - (unsigned long long)val;
						val *= 10.0;
						double roundy = ((double)(val - (unsigned long long)val) - 0.99999);
						if (roundy < 0.00001 && roundy > -0.00001 && ((unsigned long long)(val) % 10) != 9) {
							written += print_dec((unsigned long long)(val) % 10 + 1, 0, callback, userData, 0, 0, 1);
							break;
						}
						written += print_dec((unsigned long long)(val) % 10, 0, callback, userData, 0, 0, 1);
					}
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

/* Strings */
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

/* Unlimited strings */
static int cb_sxprintf(void * user, char c) {
	struct CBData * data = user;
	data->str[data->written] = c;
	data->written++;
	return 0;
}

int vsprintf(char *str, const char *format, va_list ap) {
	struct CBData data = {str,0,0};
	int out = xvasprintf(cb_sxprintf, &data, format, ap);
	cb_sxprintf(&data, '\0');
	return out;
}

int sprintf(char * str, const char * format, ...) {
	struct CBData data = {str,0,0};
	va_list args;
	va_start(args, format);
	int out = xvasprintf(cb_sxprintf, &data, format, args);
	va_end(args);
	cb_sxprintf(&data, '\0');
	return out;
}

/**
 * String that needs to reallocate as it goes
 */
static int cb_asprintf(void * user, char c) {
	struct CBData * data = user;

	if (data->written + 1 > data->size) {
		data->size = data->size < 8 ? 8 : data->size * 2;
		data->str = realloc(data->str, data->size);
	}

	data->str[data->written] = c;
	data->written++;
	return 0;
}

int vasprintf(char ** buf, const char * fmt, va_list args) {
	struct CBData data = {NULL,0,0};
	xvasprintf(cb_asprintf, &data, fmt, args);
	cb_asprintf(&data, '\0');
	*buf = data.str;
	return 0;
}

int asprintf(char ** ret, const char * fmt, ...) {
	va_list args;
	va_start(args, fmt);
	int out = vasprintf(ret,fmt,args);
	va_end(args);
	return out;
}

/* Streams */

static int cb_fprintf(void * user, char c) {
	fputc(c,(FILE*)user);
	return 0;
}

int fprintf(FILE *stream, const char * fmt, ...) {
	va_list args;
	va_start(args, fmt);
	int out = xvasprintf(cb_fprintf, stream, fmt, args);
	va_end(args);
	return out;
}

int printf(const char * fmt, ...) {
	va_list args;
	va_start(args, fmt);
	int out = xvasprintf(cb_fprintf, stdout, fmt, args);
	va_end(args);
	return out;
}

int vfprintf(FILE * stream, const char *fmt, va_list args) {
	return xvasprintf(cb_fprintf, stream, fmt, args);
}

int vprintf(const char *fmt, va_list args) {
	return xvasprintf(cb_fprintf, stdout, fmt, args);
}

