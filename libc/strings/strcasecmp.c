#include <strings.h>
#include <ctype.h>

int strcasecmp(const char * s1, const char * s2) {
	for (; tolower(*s1) == tolower(*s2) && *s1; s1++, s2++);
	return *(unsigned char *)s1 - *(unsigned char *)s2;
}

int strncasecmp(const char *s1, const char *s2, size_t n) {
	if (n == 0) return 0;

	while (n-- && tolower(*s1) == tolower(*s2)) {
		if (!n || !*s1) break;
		s1++;
		s2++;
	}
	return (unsigned int)tolower(*s1) - (unsigned int)tolower(*s2);
}
