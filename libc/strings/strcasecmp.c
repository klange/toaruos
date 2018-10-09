#include <strings.h>
#include <ctype.h>

int strcasecmp(const char * s1, const char * s2) {
	for (; tolower(*s1) == tolower(*s2) && *s1; s1++, s2++);
	return *(unsigned char *)s1 - *(unsigned char *)s2;
}

int strncasecmp(const char *s1, const char *s2, size_t n) {
	size_t i = 0;
	while (i < n && tolower(*s1) && tolower(*s2)) {
		if (tolower(*s1) < tolower(*s2)) return -1;
		if (tolower(*s1) > tolower(*s2)) return 1;
		s1++;
		s2++;
		i++;
	}
	return 0;
}
