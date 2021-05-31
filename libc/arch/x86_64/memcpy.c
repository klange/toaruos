#include <stddef.h>

void * memcpy(void * restrict dest, const void * restrict src, size_t n) {
	asm volatile("cld; rep movsb"
	            : "=c"((int){0})
	            : "D"(dest), "S"(src), "c"(n)
	            : "flags", "memory");
	return dest;
}
