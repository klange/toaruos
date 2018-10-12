#include <string.h>

int strncmp(const char *s1, const char *s2, size_t n) {
	if (n == 0) return 0;

	while (n-- && *s1 == *s2) {
		if (!n || !*s1) break;
		s1++;
		s2++;
	}
	return (*(unsigned char *)s1) - (*(unsigned char *)s2);
}
