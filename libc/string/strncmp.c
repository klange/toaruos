#include <string.h>

int strncmp(const char *s1, const char *s2, size_t n) {
	size_t i = 0;
	while (i < n && *s1 && *s2) {
		if (*s1 < *s2) return -1;
		if (*s1 > *s2) return 1;
		s1++;
		s2++;
		i++;
	}
	return 0;
}
