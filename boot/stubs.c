#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

extern void print_(char * str);
extern void print_hex_(unsigned int value);

void abort(void) {print_("ABORT\n"); while(1); }
void exit(int status) {print_("EXIT\n"); while(1); }

int memcmp(const void * vl, const void * vr, size_t n) {
	const unsigned char *l = vl;
	const unsigned char *r = vr;
	for (; n && *l == *r; n--, l++, r++);
	return n ? *l-*r : 0;
}

void * memmove(void * dest, const void * src, size_t n) {
	char * d = dest;
	const char * s = src;

	if (d==s) {
		return d;
	}

	if (s+n <= d || d+n <= s) {
		return memcpy(d, s, n);
	}

	if (d<s) {
		if ((uintptr_t)s % sizeof(size_t) == (uintptr_t)d % sizeof(size_t)) {
			while ((uintptr_t)d % sizeof(size_t)) {
				if (!n--) {
					return dest;
				}
				*d++ = *s++;
			}
			for (; n >= sizeof(size_t); n -= sizeof(size_t), d += sizeof(size_t), s += sizeof(size_t)) {
				*(size_t *)d = *(size_t *)s;
			}
		}
		for (; n; n--) {
			*d++ = *s++;
		}
	} else {
		if ((uintptr_t)s % sizeof(size_t) == (uintptr_t)d % sizeof(size_t)) {
			while ((uintptr_t)(d+n) % sizeof(size_t)) {
				if (!n--) {
					return dest;
				}
				d[n] = s[n];
			}
			while (n >= sizeof(size_t)) {
				n -= sizeof(size_t);
				*(size_t *)(d+n) = *(size_t *)(s+n);
			}
		}
		while (n) {
			n--;
			d[n] = s[n];
		}
	}

	return dest;
}

int strcmp(const char * l, const char * r) {
	for (; *l == *r && *l; l++, r++);
	return *(unsigned char *)l - *(unsigned char *)r;
}

char * strchr(const char * s, int c) {
	while (*s && *s != c) s++;
	return *s == c ? (char*)s : NULL;
}

void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *)) {
	if (!nmemb) return;
	if (!size) return;
	for (size_t i = 0; i < nmemb-1; ++i) {
		for (size_t j = 0; j < nmemb-1; ++j) {
			void * left = (char *)base + size * j;
			void * right = (char *)base + size * (j + 1);
			if (compar(left,right) > 0) {
				char tmp[size];
				memcpy(tmp, right, size);
				memcpy(right, left, size);
				memcpy(left, tmp, size);
			}
		}
	}
}

static int isspace(int c) {
	return c == ' ';
}

static int is_valid(int base, char c) {
	if (c < '0') return 0;
	if (base <= 10) {
		return c < ('0' + base);
	}

	if (c >= 'a' && c < 'a' + (base - 10)) return 1;
	if (c >= 'A' && c < 'A' + (base - 10)) return 1;
	if (c >= '0' && c <= '9') return 1;
	return 0;
}

static int convert_digit(char c) {
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'a' && c <= 'z') {
		return c - 'a' + 0xa;
	}
	if (c >= 'A' && c <= 'Z') {
		return c - 'A' + 0xa;
	}
	return 0;
}

#define LONG_MAX 2147483647

#define strtox(max, type) \
	if (base < 0 || base == 1 || base > 36) { \
		return max; \
	} \
	while (*nptr && isspace(*nptr)) nptr++; \
	int sign = 1; \
	if (*nptr == '-') { \
		sign = -1; \
		nptr++; \
	} else if (*nptr == '+') { \
		nptr++; \
	} \
	if (base == 16) { \
		if (*nptr == '0') { \
			nptr++; \
			if (*nptr == 'x') { \
				nptr++; \
			} \
		} \
	} \
	if (base == 0) { \
		if (*nptr == '0') { \
			base = 8; \
			nptr++; \
			if (*nptr == 'x') { \
				base = 16; \
				nptr++; \
			} \
		} else { \
			base = 10; \
		} \
	} \
	type val = 0; \
	while (is_valid(base, *nptr)) { \
		val *= base; \
		val += convert_digit(*nptr); \
		nptr++; \
	} \
	if (endptr) { \
		*endptr = (char *)nptr; \
	} \
	if (sign == -1) { \
		return -val; \
	} else { \
		return val; \
	}

long int strtol(const char *nptr, char **endptr, int base) {
	strtox(LONG_MAX, unsigned long int);
}

double strtod(const char *nptr, char **endptr) {
	return strtol(nptr,endptr,10);
}


FILE * stdout = NULL;
FILE * stderr = NULL;

int fputc(int c, FILE * stream) {
	if (stream == stdout) {
		char tmp[2] = {c,0};
		print_(tmp);
	}
	return c;
}

int fputs(const char * s, FILE * stream) {
	while (*s) {
		fputc(*s,stream);
		s++;
	}
	return 0;
}

int puts(const char * s) {
	fputs(s,stdout);
	fputc('\n',stdout);
	return 0;
}

static void print_dec(unsigned int value, unsigned int width, char * buf, int * ptr, int fill_zero, int align_right, int precision) {
	unsigned int n_width = 1;
	unsigned int i = 9;
	if (precision == -1) precision = 1;

	if (value == 0) {
		n_width = 0;
	} else if (value < 10UL) {
		n_width = 1;
	} else if (value < 100UL) {
		n_width = 2;
	} else if (value < 1000UL) {
		n_width = 3;
	} else if (value < 10000UL) {
		n_width = 4;
	} else if (value < 100000UL) {
		n_width = 5;
	} else if (value < 1000000UL) {
		n_width = 6;
	} else if (value < 10000000UL) {
		n_width = 7;
	} else if (value < 100000000UL) {
		n_width = 8;
	} else if (value < 1000000000UL) {
		n_width = 9;
	} else {
		n_width = 10;
	}

	if (n_width < (unsigned int)precision) n_width = precision;

	int printed = 0;
	if (align_right) {
		while (n_width + printed < width) {
			buf[*ptr] = fill_zero ? '0' : ' ';
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
	} else {
		i = n_width;
		while (i > 0) {
			unsigned int n = value / 10;
			int r = value % 10;
			buf[*ptr + i - 1] = r + '0';
			i--;
			value = n;
			printed++;
		}
		*ptr += n_width;
		while (printed < (int)width) {
			buf[*ptr] = fill_zero ? '0' : ' ';
			*ptr += 1;
			printed += 1;
		}
	}
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
int xvasprintf(char * buf, const char * fmt, va_list args) {
	int i = 0;
	char * s;
	char * b = buf;
	int precision = -1;
	for (const char *f = fmt; *f; f++) {
		if (*f != '%') {
			*b++ = *f;
			continue;
		}
		++f;
		unsigned int arg_width = 0;
		int align = 1; /* right */
		int fill_zero = 0;
		int big = 0;
		int alt = 0;
		int always_sign = 0;
		while (1) {
			if (*f == '-') {
				align = 0;
				++f;
			} else if (*f == '#') {
				alt = 1;
				++f;
			} else if (*f == '*') {
				arg_width = (char)va_arg(args, int);
				++f;
			} else if (*f == '0') {
				fill_zero = 1;
				++f;
			} else if (*f == '+') {
				always_sign = 1;
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
		if (*f == 'z') {
			big = 1;
			++f;
		}
		/* fmt[i] == '%' */
		switch (*f) {
			case 's': /* String pointer -> String */
				{
					size_t count = 0;
					if (big) {
						return -1;
					} else {
						s = (char *)va_arg(args, char *);
						if (s == NULL) {
							s = "(null)";
						}
						if (precision >= 0) {
							while (*s && precision > 0) {
								*b++ = *s++;
								count++;
								precision--;
								if (arg_width && count == arg_width) break;
							}
						} else {
							while (*s) {
								*b++ = *s++;
								count++;
								if (arg_width && count == arg_width) break;
							}
						}
					}
					while (count < arg_width) {
						*b++ = ' ';
						count++;
					}
				}
				break;
			case 'c': /* Single character */
				*b++ = (char)va_arg(args, int);
				break;
			case 'p':
				if (!arg_width) {
					arg_width = 8;
					alt = 1;
				}
			case 'x': /* Hexadecimal number */
				if (alt) {
					*b++ = '0';
					*b++ = 'x';
				}
				i = b - buf;
				if (big == 2) {
					unsigned long long val = (unsigned long long)va_arg(args, unsigned long long);
					if (val > 0xFFFFFFFF) {
						print_hex(val >> 32, arg_width > 8 ? (arg_width - 8) : 0, buf, &i);
					}
					print_hex(val & 0xFFFFFFFF, arg_width > 8 ? 8 : arg_width, buf, &i);
				} else {
					print_hex((unsigned long)va_arg(args, unsigned long), arg_width, buf, &i);
				}
				b = buf + i;
				break;
			case 'i':
			case 'd': /* Decimal number */
			case 'g': /* supposed to also support e */
			case 'f':
				{
					long long val;
					if (big == 2) {
						val = (long long)va_arg(args, long long);
					} else {
						val = (long)va_arg(args, long);
					}
					if (val < 0) {
						*b++ = '-';
						val = -val;
					} else if (always_sign) {
						*b++ = '+';
					}
					i = b - buf;
					print_dec(val, arg_width, buf, &i, fill_zero, align, precision);
					b = buf + i;
				}
				break;
			case 'u': /* Unsigned ecimal number */
				i = b - buf;
				{
					unsigned long long val;
					if (big == 2) {
						val = (unsigned long long)va_arg(args, unsigned long long);
					} else {
						val = (unsigned long)va_arg(args, unsigned long);
					}
					print_dec(val, arg_width, buf, &i, fill_zero, align, precision);
				}
				b = buf + i;
				break;
#if 0
			case 'g': /* supposed to also support e */
			case 'f':
				{
					double val = (double)va_arg(args, double);
					if (val < 0) {
						*b++ = '-';
						val = -val;
					}
					i = b - buf;
					print_dec((long)val, arg_width, buf, &i, fill_zero, align, 1);
					b = buf + i;
					*b++ = '.';
					i = b - buf;
					for (int j = 0; j < ((precision > -1 && precision < 8) ? precision : 8); ++j) {
						if ((int)(val * 100000.0) % 100000 == 0 && j != 0) break;
						val *= 10.0;
						print_dec((int)(val) % 10, 0, buf, &i, 0, 0, 1);
					}
					b = buf + i;
				}
				break;
#endif
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


int vsnprintf(char *str, size_t size, const char *format, va_list ap) {
	return xvasprintf(str, format, ap);
}

int snprintf(char * str, size_t size, const char * format, ...) {
	(void)size;
	va_list args;
	va_start(args, format);
	int out = xvasprintf(str, format, args);
	va_end(args);
	return out;
}

int fprintf(FILE *stream, const char * fmt, ...) {
	static char str[1024] = {0};
	va_list args;
	va_start(args, fmt);
	int out = xvasprintf(str, fmt, args);
	fputs(str,stream);
	va_end(args);
	return out;
}

struct BadMallocHeader {
	size_t actual;
	size_t space;
	char data[];
};

static void * _heap_start = NULL;
static struct BadMallocHeader * first = NULL;
static struct BadMallocHeader * last  = NULL;

void bump_heap_setup(void * start) {
	_heap_start = first = last = start;
}

#define FIT(size) (size <= 16 ? 16 : \
				  (size <= 64 ? 64 : \
				  (size <= 256 ? 256 : \
				  (size <= 1024 ? 1024 : \
				  (size <= 4096 ? 4096 : \
				  (size <= 16384 ? 16384 : \
				   -1))))))

static void * findFirstFit(size_t size) {
	struct BadMallocHeader * ptr = first;

	while (ptr != last && (ptr->actual || ptr->space < size)) {
		ptr = (struct BadMallocHeader*)((char*)ptr + sizeof(struct BadMallocHeader) + ptr->space);
	}

	if (ptr == last) {
		ptr->actual = size;
		ptr->space = FIT(size);
		if (ptr->space == -1) {
			print_("[alloc of size ");
			print_hex_(size);
			print_(" is too big]\n");
		}
		last = (struct BadMallocHeader*)((char*)ptr + sizeof(struct BadMallocHeader) + ptr->space);
		return ptr;
	} else {
		ptr->actual = size;
		return ptr;
	}
}

static struct BadMallocHeader nil = {0,0};

void * realloc(void * ptr, size_t size) {
	if (!ptr) {
		if (size == 0) return &nil;
		struct BadMallocHeader * this = findFirstFit(size);
		return ((char*)this) + sizeof(struct BadMallocHeader);
	} else {
		struct BadMallocHeader * this = (void*)((char*)ptr - sizeof(struct BadMallocHeader));
		if (size < this->space) {
			this->actual = size;
			if (size == 0) return &nil;
			return ptr;
		}
		struct BadMallocHeader * new = findFirstFit(size);
		memmove(&new->data, &this->data, this->actual < size ? this->actual : size);
		this->actual = 0;
		return ((char*)new) + sizeof(struct BadMallocHeader);
	}
}
void free(void *ptr) {
	realloc(ptr,0);
}
void * malloc(size_t size) {
	return realloc(NULL, size);
}
void * calloc(size_t nmemb, size_t size) {
	char * out = realloc(NULL, nmemb * size);
	for (size_t i = 0; i < nmemb * size; ++i) {
		out[i] = '\0';
	}
	return out;
}
size_t strlen(const char *s) {
	size_t out = 0;
	while (*s) {
		out++;
		s++;
	}
	return out;
}
char * strdup(const char * src) {
	char * out = malloc(strlen(src)+1);
	char * c = out;
	while (*src) {
		*c = *src;
		c++; src++;
	}
	*c = 0;
	return out;
}

extern void set_attr(int _attr);

char *strstr(const char *haystack, const char *needle) {
	size_t s = strlen(needle);
	const char * end = haystack + strlen(haystack);

	while (haystack + s <= end) {
		if (!memcmp(haystack,needle,s)) return (char*)haystack;
		haystack++;
	}

	return NULL;
}

char * strcat(char *dest, const char *src) {
	char * end = dest;
	while (*end != '\0') {
		++end;
	}
	while (*src) {
		*end = *src;
		end++;
		src++;
	}
	*end = '\0';
	return dest;
}
