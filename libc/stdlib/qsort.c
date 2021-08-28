#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <sys/types.h>

extern char * _argv_0;
extern int __libc_debug;

struct SortableArray {
	void * data;
	size_t size;
	int (*func)(const void *, const void *);
};

static ssize_t partition(struct SortableArray * array, ssize_t lo, ssize_t hi) {
	char pivot[array->size];
	memcpy(pivot, (char *)array->data + array->size * hi, array->size);
	ssize_t i = lo - 1;
	for (ssize_t j = lo; j <= hi; ++j) {
		uint8_t * obj_j = (uint8_t *)array->data + array->size * j;
		if (array->func(obj_j, pivot) <= 0) {
			i++;
			if (j != i) {
				uint8_t * obj_i = (uint8_t *)array->data + array->size * i;
				for (size_t x = 0; x < array->size; ++x) {
					uint8_t tmp = obj_i[x];
					obj_i[x] = obj_j[x];
					obj_j[x] = tmp;
				}
			}
		}
	}
	return i;
}

static void quicksort(struct SortableArray * array, ssize_t lo, ssize_t hi) {
	if (lo >= 0 && hi >= 0) {
		if (lo < hi) {
			ssize_t pivot = partition(array, lo, hi);
			quicksort(array, lo, pivot - 1);
			quicksort(array, pivot + 1, hi);
		}
	}
}

void qsort(void * base, size_t nmemb, size_t size, int (*compar)(const void *, const void *)) {
	if (nmemb < 2) return;
	if (!size) return;
	struct SortableArray array = {base,size,compar};
	quicksort(&array, 0, nmemb - 1);
}
