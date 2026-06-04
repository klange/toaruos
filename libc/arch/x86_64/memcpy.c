#include <string.h>
#include <stddef.h>

void * memcpy(void * restrict dest, const void * restrict src, size_t n) {
	asm volatile("cld; rep movsb"
	            : "=c"((unsigned long){0}), "=rdi"((unsigned long){0})
	            : "rdi"(dest), "S"(src), "c"(n)
	            : "flags", "memory", "rdi");
	return dest;
}
