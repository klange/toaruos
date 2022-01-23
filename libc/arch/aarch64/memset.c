#include <stddef.h>

void * memset(void * dest, int c, size_t n) {
	size_t i = 0;
	for ( ; i < n; ++i ) {
		((char *)dest)[i] = c;
	}
	return dest;
}
