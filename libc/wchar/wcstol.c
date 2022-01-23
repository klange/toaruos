#include <stdlib.h>
#include <ctype.h>
#include <limits.h>
#include <wchar.h>
#include <errno.h>

static int is_valid(int base, wchar_t c) {
	if (c < '0') return 0;
	if (base <= 10) {
		return c < ('0' + (wchar_t)base);
	}

	if (c >= 'a' && c < 'a' + ((wchar_t)base - 10)) return 1;
	if (c >= 'A' && c < 'A' + ((wchar_t)base - 10)) return 1;
	if (c >= '0' && c <= '9') return 1;
	return 0;
}

static int convert_digit(wchar_t c) {
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

#define strtox(max, type) \
	if (base < 0 || base == 1 || base > 36) { \
		errno = EINVAL; \
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
		*endptr = (wchar_t *)nptr; \
	} \
	if (sign == -1) { \
		return -val; \
	} else { \
		return val; \
	}

unsigned long int wcstoul(const wchar_t *nptr, wchar_t **endptr, int base) {
	strtox(ULONG_MAX, unsigned long int);
}

unsigned long long int wcstoull(const char *nptr, wchar_t **endptr, int base) {
	strtox(ULLONG_MAX, unsigned long int);
}

long int wcstol(const wchar_t *nptr, wchar_t **endptr, int base) {
	strtox(LONG_MAX, unsigned long int);
}

long long int wcstoll(const wchar_t *nptr, wchar_t **endptr, int base) {
	strtox(LLONG_MAX, unsigned long long int);
}

