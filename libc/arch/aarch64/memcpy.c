#include <string.h>
#include <stddef.h>

void * memcpy(void * restrict dest, const void * restrict src, size_t n) {
	char * d = dest;
	const char * s = src;
	for (; n > 0; n--) {
		*d++ = *s++;
	}
	return dest;
}
