#include <stdlib.h>
#include <stddef.h>
#include <sys/types.h>

void qsort(void * base, size_t nmemb, size_t size, int (*compar)(const void *, const void *)) {
	for (size_t i = 0; i < nmemb-1; ++i) {
		for (size_t j = 0; j < nmemb-1; ++j) {
			void * left = (char *)base + size * j;
			void * right = (char *)base + size * (j + 1);
			if (compar(left,right) > 0) {
				char tmp[size];
				memcpy(tmp, right, size);
				memcpy(right, left, size);
				memcpy(left, tmp, size);
			}
		}
	}
}
