#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include "text.h"

void * memset(void * dest, int c, size_t n) {
	asm volatile("cld; rep stosb"
	             : "=c"((int){0})
	             : "D"(dest), "a"(c), "c"(n)
	             : "flags", "memory");
	return dest;
}

void * memcpy(void * restrict dest, const void * restrict src, size_t n) {
	asm volatile("cld; rep movsb"
	            : "=c"((int){0})
	            : "D"(dest), "S"(src), "c"(n)
	            : "flags", "memory");
	return dest;
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
