#include <stddef.h>

void * memset(void * dest, int c, size_t n) {
	asm volatile("cld; rep stosb"
	             : "=c"((int){0})
	             : "rdi"(dest), "a"(c), "c"(n)
	             : "flags", "memory", "rdi");
	return dest;
}

