#include <stddef.h>
#include <stdint.h>
#include <string.h>

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
