#include <string.h>

char * stpncpy(char * restrict dest, const char * restrict src, size_t n) {
	while (n && (*dest = *src)) {
		n--;
		dest++;
		src++;
	}
	memset(dest, 0, n);
	return dest;
}

char * strncpy(char * restrict dest, const char * restrict src, size_t n) {
	stpncpy(dest, src, n);
	return dest;
}
