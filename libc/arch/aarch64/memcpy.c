#include <stdint.h>
#include <string.h>
#include <stddef.h>

void * memcpy(void * restrict dest, const void * restrict src, size_t n) {
	uint64_t * d_64 = dest;
	const uint64_t * s_64 = src;

	for (; n >= 8; n -= 8) {
		*d_64++ = *s_64++;
	}

	uint32_t * d_32 = (void*)d_64;
	const uint32_t * s_32 = (const void*)s_64;

	for (; n >= 4; n -= 4) {
		*d_32++ = *s_32++;
	}

	uint8_t * d = (void*)d_32;
	const uint8_t * s = (const void*)s_32;

	for (; n > 0; n--) {
		*d++ = *s++;
	}

	return dest;
}
