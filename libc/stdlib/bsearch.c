#include <stdlib.h>

void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
	int (*compar)(const void *, const void *)) {
	/* Stupid naive implementation */
	const char * b = base;
	size_t i = 0;
	while (i < nmemb) {
		const void * a = b;
		if (!compar(key,a)) return (void *)a;
		i++;
		b += size;
	}
	return NULL;
}
